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
        QString marker;
        if (!point.attempted) {
            marker = QStringLiteral("未检");
        } else if (!point.ok) {
            marker = QStringLiteral("失败");
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

protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = rect().adjusted(18, 16, -18, -16);
        const qreal ellipseWidth = qMin(bounds.width(), bounds.height() * 2.05);
        const qreal ellipseHeight = ellipseWidth * 0.44;
        const QRectF ringRect(
            bounds.center().x() - ellipseWidth / 2.0,
            bounds.center().y() - ellipseHeight / 2.0,
            ellipseWidth,
            ellipseHeight
        );
        const qreal ringWidth = qMax<qreal>(10.0, ellipseHeight * 0.26);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(187, 238, 243, 120), ringWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(ringRect, 0, 360 * 16);

        const int spanAngle = static_cast<int>(-360.0 * 16.0 * m_usageRatio);
        painter.setPen(QPen(usageRingColor(m_usageRatio), ringWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(ringRect, 90 * 16, spanAngle);

        painter.setPen(QPen(QColor(255, 255, 255, 210), 1.2));
        painter.drawEllipse(ringRect);
    }

private:
    double m_usageRatio {0.0};
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
                emit analysisFinished(m_analysisWatcher.result().looksHealthy);
            });

    connect(m_analyzeButton, &QPushButton::clicked, this, &DiscAnalysisWidget::analyzeCurrentDisc);

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

void DiscAnalysisWidget::analyzeCurrentDisc() {
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
    emit analysisStarted();
    m_analysisWatcher.setFuture(QtConcurrent::run([service = m_service, driveId = m_currentDriveId]() {
        return service.analyzeLiveDisc(driveId);
    }));
}

void DiscAnalysisWidget::applyResult(const cdmanager::application::import::DiscAnalysisResult& result) {
    m_lastResult = result;
    m_exportJsonButton->setEnabled(!result.jsonReport.isNull());

    m_statusLabel->setText(
        result.looksHealthy
            ? cdmanager::presentation::ui::text(m_language, u"分析状态：未检测到明确异常。", u"Analysis status: no explicit anomaly detected.")
            : cdmanager::presentation::ui::text(m_language, u"分析状态：检测到异常线索。", u"Analysis status: anomaly indicators detected.")
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
            u"已完成盘片结构采样，可结合目录信息、音频时长与抽样结果判断盘片是否存在中断写入或后段缺失。",
            u"Disc structure sampling is complete. Combine TOC, runtime, and audio sampling results to determine whether the disc contains interrupted writes or missing later regions."
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
            ? cdmanager::presentation::ui::text(m_language, u"结构判定：盘片结构特征基本完整，可作为常规音频光盘处理。", u"Structural assessment: the disc appears structurally complete and may be handled as a standard audio disc.")
            : cdmanager::presentation::ui::text(m_language, u"结构判定：盘片存在异常结构特征，建议优先排查中断写入、尾段缺失或文本读取失配。", u"Structural assessment: abnormal disc traits detected; prioritize investigation of interrupted writing, missing tail regions, or CD-TEXT read mismatches.")
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
            item->setText(6, cdmanager::presentation::ui::text(m_language, u"未采样", u"not sampled"));
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
        u"分析当前光盘的 TOC、CD-TEXT、音频时长和疑似写坏位置，特别适合检查写到一半断掉的盘。",
        u"Inspect the current disc for TOC, CD-TEXT, audio runtime, and suspicious unwritten or damaged regions."
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
        text(m_language, u"扇区长度", u"Sector Length"),
        text(m_language, u"音频抽样", u"Audio Sampling"),
    });
    m_detailsEdit->setPlaceholderText(text(
        m_language,
        u"分析完成后，详细诊断会显示在这里。",
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
