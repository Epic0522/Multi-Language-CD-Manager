#include "cdmanager/presentation/editor/DiscAnalysisWidget.h"

#include <algorithm>

#include <QFrame>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace cdmanager::presentation::editor {

namespace {

QString msfFromSeconds(int totalSeconds) {
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString probeSummaryText(const cdmanager::application::import::DiscAnalysisTrackProbe& probe) {
    QStringList parts;
    for (const auto& point : probe.points) {
        if (point.inferred) {
            if (point.error == QStringLiteral("tail-unwritten")) {
                parts.append(QStringLiteral("尾段疑似未写入"));
            } else if (point.error == QStringLiteral("tail-overlap")) {
                parts.append(QStringLiteral("尾段疑似截断"));
            } else {
                parts.append(QStringLiteral("结构推定正常"));
            }
            continue;
        }
        QString marker;
        if (!point.attempted) {
            marker = QStringLiteral("未执行");
        } else if (point.error == QStringLiteral("tail-overlap")) {
            marker = QStringLiteral("起始区疑似截断");
        } else if (point.error == QStringLiteral("tail-unwritten")) {
            marker = QStringLiteral("起始区疑似未写入");
        } else if (point.error == QStringLiteral("probe-soft-fail")) {
            marker = QStringLiteral("边界读样不稳");
        } else if (!point.ok) {
            marker = QStringLiteral("读取失败");
        } else if (point.allZero) {
            marker = QStringLiteral("零填充");
        } else {
            marker = QStringLiteral("有效");
        }
        parts.append(QStringLiteral("%1：%2").arg(point.label, marker));
    }
    return parts.join(QStringLiteral(" / "));
}

bool isGenericTrackTitle(const QString& title) {
    static const QRegularExpression genericTrackRe(QStringLiteral(R"(^Track\s+\d+$)"));
    return genericTrackRe.match(title.trimmed()).hasMatch();
}

bool isPlaceholderArtist(const QString& artist) {
    const QString normalized = artist.trimmed();
    return normalized.isEmpty()
        || normalized == QStringLiteral("未焼録")
        || normalized == QStringLiteral("未烧录")
        || normalized == QStringLiteral("(empty)");
}

QColor usageRingColor(double ratio) {
    const QColor start(98, 234, 239);
    const QColor end(26, 74, 176);
    const double t = qBound(0.0, ratio, 1.0);
    return QColor(
        static_cast<int>(start.red() + (end.red() - start.red()) * t),
        static_cast<int>(start.green() + (end.green() - start.green()) * t),
        static_cast<int>(start.blue() + (end.blue() - start.blue()) * t)
    );
}

class UsageRingWidget final : public QWidget {
public:
    explicit UsageRingWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumSize(220, 130);
    }

    void setUsageRatio(double ratio) {
        m_usageRatio = qBound(0.0, ratio, 1.0);
        update();
    }

    void setRegionMarkers(const QVector<cdmanager::application::import::DiscAnalysisRegionMarker>& markers) {
        m_regionMarkers = markers;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = rect().adjusted(10, 22, -10, -20);
        const qreal ellipseWidth = qMin(bounds.width(), bounds.height() * 3.45);
        const qreal ellipseHeight = ellipseWidth * 0.33;
        const QRectF outerRect(
            bounds.center().x() - ellipseWidth / 2.0,
            bounds.center().y() - ellipseHeight / 2.0,
            ellipseWidth,
            ellipseHeight
        );
        const qreal innerInsetX = outerRect.width() * 0.23;
        const qreal innerInsetY = outerRect.height() * 0.27;
        const QRectF innerRect = outerRect.adjusted(innerInsetX, innerInsetY, -innerInsetX, -innerInsetY);

        auto donutPathForSpan = [&](double startRatio, double endRatio) {
            const double startDeg = 90.0 - 360.0 * startRatio;
            const double spanDeg = -360.0 * (endRatio - startRatio);
            QPainterPath path;
            path.arcMoveTo(outerRect, startDeg);
            path.arcTo(outerRect, startDeg, spanDeg);
            path.arcTo(innerRect, startDeg + spanDeg, -spanDeg);
            path.closeSubpath();
            return path;
        };

        QPainterPath basePath;
        basePath.addEllipse(outerRect);
        QPainterPath holePath;
        holePath.addEllipse(innerRect);
        basePath = basePath.subtracted(holePath);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(108, 231, 238));
        painter.drawPath(basePath);

        if (m_usageRatio > 0.0) {
            painter.setBrush(usageRingColor(m_usageRatio));
            painter.drawPath(donutPathForSpan(0.0, m_usageRatio));
        }

        painter.setBrush(QColor(244, 194, 214, 190));
        for (const auto& marker : m_regionMarkers) {
            const double start = qBound(0.0, marker.startRatio, 1.0);
            const double end = qBound(0.0, marker.endRatio, 1.0);
            if (end <= start) {
                continue;
            }
            painter.drawPath(donutPathForSpan(start, end));
        }
    }

private:
    double m_usageRatio {0.0};
    QVector<cdmanager::application::import::DiscAnalysisRegionMarker> m_regionMarkers;
};

}  // namespace

DiscAnalysisWidget::DiscAnalysisWidget(QWidget* parent)
    : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    setObjectName(QStringLiteral("discAnalysisSurface"));

    m_headerLabel = new QLabel(this);
    QFont f = m_headerLabel->font();
    f.setBold(true);
    m_headerLabel->setFont(f);
    root->addWidget(m_headerLabel);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    root->addWidget(m_hintLabel);

    auto* buttonLayout = new QHBoxLayout();
    m_analyzeButton = new QPushButton(this);
    m_exportJsonButton = new QPushButton(this);
    m_exportJsonButton->setEnabled(false);
    m_statusLabel = new QLabel(this);
    buttonLayout->addWidget(m_analyzeButton);
    buttonLayout->addWidget(m_exportJsonButton);
    buttonLayout->addWidget(m_statusLabel, 1);
    root->addLayout(buttonLayout);

    auto* topFrame = new QFrame(this);
    topFrame->setObjectName(QStringLiteral("discAnalysisTopFrame"));
    auto* topLayout = new QHBoxLayout(topFrame);
    topLayout->setContentsMargins(18, 16, 18, 16);
    topLayout->setSpacing(18);

    auto* infoFrame = new QFrame(topFrame);
    infoFrame->setObjectName(QStringLiteral("discAnalysisInfoFrame"));
    auto* infoLayout = new QVBoxLayout(infoFrame);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(10);

    m_summaryLabel = new QLabel(infoFrame);
    m_summaryLabel->setWordWrap(true);
    infoLayout->addWidget(m_summaryLabel);

    auto addInfoRow = [&](const QString& objectName, QLabel** outKeyLabel, QLabel** outValueLabel) {
        auto* row = new QHBoxLayout();
        auto* keyLabel = new QLabel(infoFrame);
        keyLabel->setObjectName(objectName + QStringLiteral("Key"));
        auto* valueLabel = new QLabel(infoFrame);
        valueLabel->setObjectName(objectName + QStringLiteral("Value"));
        valueLabel->setWordWrap(true);
        row->addWidget(keyLabel, 0);
        row->addWidget(valueLabel, 1);
        infoLayout->addLayout(row);
        *outKeyLabel = keyLabel;
        *outValueLabel = valueLabel;
    };

    addInfoRow(QStringLiteral("discTitle"), &m_discTitleKeyLabel, &m_discTitleValueLabel);
    addInfoRow(QStringLiteral("discPerformer"), &m_discPerformerKeyLabel, &m_discPerformerValueLabel);
    addInfoRow(QStringLiteral("drive"), &m_driveKeyLabel, &m_driveValueLabel);
    addInfoRow(QStringLiteral("medium"), &m_mediumKeyLabel, &m_mediumValueLabel);
    addInfoRow(QStringLiteral("trackCount"), &m_trackCountKeyLabel, &m_trackCountValueLabel);
    addInfoRow(QStringLiteral("runtime"), &m_runtimeKeyLabel, &m_runtimeValueLabel);
    addInfoRow(QStringLiteral("cdtext"), &m_cdTextKeyLabel, &m_cdTextValueLabel);

    m_verdictLabel = new QLabel(infoFrame);
    m_verdictLabel->setObjectName(QStringLiteral("discVerdictLabel"));
    m_verdictLabel->setWordWrap(true);
    infoLayout->addWidget(m_verdictLabel);

    topLayout->addWidget(infoFrame, 3);

    auto* usageFrame = new QFrame(topFrame);
    usageFrame->setObjectName(QStringLiteral("discUsageFrame"));
    auto* usageLayout = new QVBoxLayout(usageFrame);
    usageLayout->setContentsMargins(0, 0, 0, 0);
    usageLayout->setSpacing(6);

    auto* usageHeaderLayout = new QHBoxLayout();
    m_usageCaptionLabel = new QLabel(usageFrame);
    m_freeTextLabel = new QLabel(usageFrame);
    m_freeTextLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    usageHeaderLayout->addWidget(m_usageCaptionLabel, 1);
    usageHeaderLayout->addWidget(m_freeTextLabel, 1);
    usageLayout->addLayout(usageHeaderLayout);

    m_usageRingWidget = new UsageRingWidget(usageFrame);
    usageLayout->addWidget(m_usageRingWidget, 1);

    m_usageTextLabel = new QLabel(usageFrame);
    m_usageTextLabel->setAlignment(Qt::AlignCenter);
    usageLayout->addWidget(m_usageTextLabel);

    topLayout->addWidget(usageFrame, 2);
    root->addWidget(topFrame);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto* trackFrame = new QFrame(splitter);
    trackFrame->setObjectName(QStringLiteral("discTrackFrame"));
    auto* trackLayout = new QVBoxLayout(trackFrame);
    trackLayout->setContentsMargins(0, 0, 0, 0);
    trackLayout->setSpacing(8);

    m_trackTree = new QTreeWidget(trackFrame);
    m_trackTree->setRootIsDecorated(false);
    m_trackTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_trackTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_trackTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(6, QHeaderView::Stretch);
    trackLayout->addWidget(m_trackTree, 1);
    splitter->addWidget(trackFrame);

    auto* detailFrame = new QFrame(splitter);
    detailFrame->setObjectName(QStringLiteral("discDetailFrame"));
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);

    m_detailsEdit = new QPlainTextEdit(detailFrame);
    m_detailsEdit->setReadOnly(true);
    detailLayout->addWidget(m_detailsEdit, 1);
    splitter->addWidget(detailFrame);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    connect(&m_analysisWatcher,
            &QFutureWatcher<cdmanager::application::import::DiscAnalysisResult>::finished,
            this,
            [this]() {
                m_analyzeButton->setEnabled(true);
                applyResult(m_analysisWatcher.result());
                emit analysisFinished(
                    m_analysisWatcher.result().looksHealthy,
                    m_analysisWatcher.result().performedDeepAnalysis
                );
            });

    connect(m_analyzeButton, &QPushButton::clicked, this, [this]() {
        analyzeCurrentDisc(cdmanager::application::import::DiscAnalysisDepth::Deep);
    });

    connect(m_exportJsonButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this,
            cdmanager::presentation::ui::text(m_language, u"导出分析 JSON", u"Export analysis JSON"),
            QStringLiteral("disc-analysis.json"),
            QStringLiteral("JSON (*.json)")
        );
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_statusLabel->setText(
                cdmanager::presentation::ui::text(m_language, u"无法写入 JSON 文件。", u"Could not write JSON file.")
            );
            return;
        }
        file.write(m_lastResult.jsonReport.toJson(QJsonDocument::Indented));
        m_statusLabel->setText(
            cdmanager::presentation::ui::text(m_language, u"分析 JSON 已导出。", u"Analysis JSON exported.")
        );
    });

    retranslateUi();
}

void DiscAnalysisWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
}

void DiscAnalysisWidget::setCurrentDriveId(const QString& driveId) {
    m_currentDriveId = driveId;
}

void DiscAnalysisWidget::setReferenceProject(const cdmanager::domain::project::CdProject& project) {
    m_referenceProject = project;
}

void DiscAnalysisWidget::analyzeCurrentDisc(cdmanager::application::import::DiscAnalysisDepth depth) {
    if (m_analysisWatcher.isRunning()) {
        return;
    }

    m_analyzeButton->setEnabled(false);
    m_exportJsonButton->setEnabled(false);
    m_statusLabel->setText(
        cdmanager::presentation::ui::text(m_language, u"正在分析光碟…", u"Analyzing disc...")
    );
    m_summaryLabel->clear();
    m_discTitleValueLabel->clear();
    m_discPerformerValueLabel->clear();
    m_driveValueLabel->clear();
    m_mediumValueLabel->clear();
    m_trackCountValueLabel->clear();
    m_runtimeValueLabel->clear();
    m_cdTextValueLabel->clear();
    m_verdictLabel->clear();
    m_usageTextLabel->clear();
    m_freeTextLabel->clear();
    m_trackTree->clear();
    m_detailsEdit->clear();
    static_cast<UsageRingWidget*>(m_usageRingWidget)->setUsageRatio(0.0);
    static_cast<UsageRingWidget*>(m_usageRingWidget)->setRegionMarkers({});
    emit analysisStarted();
    m_analysisWatcher.setFuture(QtConcurrent::run([service = m_service, driveId = m_currentDriveId, depth]() {
        return service.analyzeLiveDisc(driveId, depth);
    }));
}

const cdmanager::application::import::DiscAnalysisResult& DiscAnalysisWidget::lastResult() const {
    return m_lastResult;
}

void DiscAnalysisWidget::applyResult(const cdmanager::application::import::DiscAnalysisResult& result) {
    m_lastResult = result;
    m_exportJsonButton->setEnabled(!result.jsonReport.isNull());

    m_statusLabel->setText(
        result.looksHealthy
            ? cdmanager::presentation::ui::text(m_language, u"分析状态：未检出明确异常。", u"Analysis status: no explicit anomaly detected.")
            : cdmanager::presentation::ui::text(m_language, u"分析状态：检出异常结构特征。", u"Analysis status: abnormal structural indicators detected.")
    );

    int totalSeconds = 0;
    for (const auto& track : result.tracks) {
        totalSeconds += track.durationSeconds;
    }
    const QString cdTextStatus =
        result.drutilCdTextAvailable || !result.cdTextTitle.isEmpty() || !result.cdTextPerformer.isEmpty()
            ? cdmanager::presentation::ui::text(m_language, u"可读", u"readable")
            : cdmanager::presentation::ui::text(m_language, u"不可读", u"unreadable");

    m_summaryLabel->setText(
        cdmanager::presentation::ui::text(
            m_language,
            result.performedDeepAnalysis
                ? u"已完成扩展结构分析。当前结果综合了目录布局、容量矛盾和异常区段推定，可用于判断盘片是否存在中断写入或程序区尾段缺失。"
                : u"已完成基础结构分析。当前结果汇总了目录布局、文本读取状态与盘片容量信息。",
            result.performedDeepAnalysis
                ? u"Extended structural analysis is complete. The current result combines TOC geometry, contradictory capacity metadata, and inferred abnormal ranges."
                : u"Baseline structural analysis is complete. The current result summarizes TOC geometry, CD-TEXT readability, and disc capacity metadata."
        )
    );

    const QString effectiveDiscTitle = !result.cdTextTitle.trimmed().isEmpty()
        ? result.cdTextTitle
        : m_referenceProject.albumTitle;
    const QString effectiveDiscPerformer = !result.cdTextPerformer.trimmed().isEmpty()
        ? result.cdTextPerformer
        : m_referenceProject.albumArtist;

    m_discTitleValueLabel->setText(effectiveDiscTitle.isEmpty()
                                       ? cdmanager::presentation::ui::text(m_language, u"(空)", u"(empty)")
                                       : effectiveDiscTitle);
    m_discPerformerValueLabel->setText(effectiveDiscPerformer.isEmpty()
                                           ? cdmanager::presentation::ui::text(m_language, u"(空)", u"(empty)")
                                           : effectiveDiscPerformer);
    m_driveValueLabel->setText(result.rawDevicePath.isEmpty() ? QStringLiteral("(auto)") : result.rawDevicePath);
    m_mediumValueLabel->setText(result.mediumType.isEmpty()
                                    ? (result.mediaType.isEmpty() ? QStringLiteral("Audio CD") : result.mediaType)
                                    : result.mediumType);
    m_trackCountValueLabel->setText(QString::number(result.tracks.isEmpty() ? result.tracksReported : result.tracks.size()));
    m_runtimeValueLabel->setText(msfFromSeconds(totalSeconds));
    m_cdTextValueLabel->setText(cdTextStatus);
    m_verdictLabel->setText(
        result.looksHealthy
            ? cdmanager::presentation::ui::text(m_language, u"结构判定：未检出明确异常结构特征。", u"Structural assessment: no explicit structural anomaly detected.")
            : cdmanager::presentation::ui::text(m_language, u"结构判定：已检出异常结构特征，盘片存在中断写入或程序区尾段缺失的高度嫌疑。", u"Structural assessment: abnormal structural traits detected; interrupted writing or missing program-area tail is highly suspected.")
    );

    const int usedPercent = qRound(result.usageRatio * 100.0);
    m_usageTextLabel->setText(
        cdmanager::presentation::ui::text(m_language, u"占用率：%1%", u"Usage Ratio: %1%").arg(usedPercent)
    );
    m_freeTextLabel->setText(
        cdmanager::presentation::ui::text(m_language, u"剩余容量：%1", u"Remaining: %1")
            .arg(result.spaceFree.isEmpty() ? QStringLiteral("n/a") : result.spaceFree)
    );
    static_cast<UsageRingWidget*>(m_usageRingWidget)->setUsageRatio(result.usageRatio);
    static_cast<UsageRingWidget*>(m_usageRingWidget)->setRegionMarkers(result.regionMarkers);

    m_trackTree->clear();
    for (const auto& track : result.tracks) {
        QString effectiveTrackTitle = track.title;
        QString effectiveTrackArtist = track.artist;
        const auto projectTrackIt = std::find_if(
            m_referenceProject.tracks.begin(),
            m_referenceProject.tracks.end(),
            [&](const auto& projectTrack) { return projectTrack.number == track.number; }
        );
        if (projectTrackIt != m_referenceProject.tracks.end()) {
            if (effectiveTrackTitle.trimmed().isEmpty() || isGenericTrackTitle(effectiveTrackTitle)) {
                effectiveTrackTitle = projectTrackIt->title;
            }
            if (isPlaceholderArtist(effectiveTrackArtist) && !projectTrackIt->artist.trimmed().isEmpty()) {
                effectiveTrackArtist = projectTrackIt->artist;
            }
        }

        auto* item = new QTreeWidgetItem();
        item->setText(0, QString::number(track.number));
        item->setText(1, effectiveTrackTitle);
        item->setText(2, effectiveTrackArtist.isEmpty()
                             ? cdmanager::presentation::ui::text(m_language, u"(空)", u"(empty)")
                             : effectiveTrackArtist);
        item->setText(3, msfFromSeconds(track.durationSeconds));

        const auto layoutIt = std::find_if(result.cdrdaoTracks.begin(), result.cdrdaoTracks.end(), [&](const auto& cdrdaoTrack) {
            return cdrdaoTrack.trackNumber == track.number;
        });
        if (layoutIt != result.cdrdaoTracks.end()) {
            item->setText(4, layoutIt->startMsf);
            item->setText(5, QString::number(layoutIt->lengthSectors));
        } else {
            item->setText(4, cdmanager::presentation::ui::text(m_language, u"未取得", u"n/a"));
            item->setText(5, cdmanager::presentation::ui::text(m_language, u"未取得", u"n/a"));
        }

        const auto probeIt = std::find_if(result.audioProbeResults.begin(), result.audioProbeResults.end(), [&](const auto& probe) {
            return probe.trackNumber == track.number;
        });
        if (probeIt != result.audioProbeResults.end()) {
            item->setText(6, probeSummaryText(*probeIt));
        } else {
            item->setText(6, cdmanager::presentation::ui::text(m_language, u"未执行", u"not executed"));
        }

        m_trackTree->addTopLevelItem(item);
    }

    m_detailsEdit->setPlainText(result.textReport);
}

void DiscAnalysisWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_headerLabel->setText(text(m_language, u"光盘分析", u"Disc Analysis"));
    m_hintLabel->setText(text(
        m_language,
        u"对当前光盘执行结构分析，汇总 TOC、CD-TEXT、容量信息与异常区段推定结果，适用于排查中断写入、尾段缺失及读取不一致等问题。",
        u"Run a structural disc analysis that combines TOC, CD-TEXT, capacity metadata, and inferred abnormal ranges."
    ));
    m_analyzeButton->setText(text(m_language, u"分析当前光盘", u"Analyze Current Disc"));
    m_exportJsonButton->setText(text(m_language, u"导出 JSON", u"Export JSON"));
    m_discTitleKeyLabel->setText(text(m_language, u"盘标题", u"Disc Title"));
    m_discPerformerKeyLabel->setText(text(m_language, u"盘艺术家", u"Performer"));
    m_driveKeyLabel->setText(text(m_language, u"设备", u"Drive"));
    m_mediumKeyLabel->setText(text(m_language, u"介质", u"Medium"));
    m_trackCountKeyLabel->setText(text(m_language, u"音轨数", u"Tracks"));
    m_runtimeKeyLabel->setText(text(m_language, u"总时长", u"Runtime"));
    m_cdTextKeyLabel->setText(text(m_language, u"文本状态", u"CD-TEXT"));
    m_usageCaptionLabel->setText(text(m_language, u"容量占用", u"Capacity Usage"));
    m_trackTree->setHeaderLabels({
        text(m_language, u"音轨", u"Track"),
        text(m_language, u"标题", u"Title"),
        text(m_language, u"艺术家", u"Artist"),
        text(m_language, u"时长", u"Duration"),
        text(m_language, u"起始地址", u"Start Address"),
        text(m_language, u"区段长度", u"Region Length"),
        text(m_language, u"检测结论", u"Assessment"),
    });
    m_detailsEdit->setPlaceholderText(text(
        m_language,
        u"分析完成后，详细诊断报告会显示在这里。",
        u"Detailed analysis output appears here after the scan."
    ));

    setStyleSheet(QStringLiteral(
        "#discAnalysisSurface {"
        "  background: rgba(241, 236, 214, 0.95);"
        "  border: 1px solid rgba(138, 130, 104, 0.55);"
        "  border-radius: 10px;"
        "}"
        "#discAnalysisTopFrame, #discTrackFrame, #discDetailFrame {"
        "  background: rgba(255,251,241,0.82);"
        "  border: 1px solid rgba(148, 139, 112, 0.55);"
        "  border-radius: 8px;"
        "}"
        "#discAnalysisInfoFrame, #discUsageFrame { background: transparent; border: none; }"
        "#discVerdictLabel {"
        "  background: rgba(154, 197, 245, 0.20);"
        "  border: 1px solid rgba(80, 116, 177, 0.42);"
        "  border-radius: 10px;"
        "  padding: 8px 10px;"
        "  color: #29477c;"
        "}"
    ));
}

}  // namespace cdmanager::presentation::editor
