#include "cdmanager/presentation/editor/BurnWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTreeWidget>

#include <QVBoxLayout>

#include "cdmanager/infrastructure/audio/AudioBurnSourcePreparer.h"

namespace cdmanager::presentation::editor {

namespace {

bool containsJapaneseText(const QString& text) {
    for (const QChar ch : text) {
        const char32_t code = ch.unicode();
        if ((code >= 0x3040 && code <= 0x30FF) ||   // 平假名 / 片假名
            (code >= 0x4E00 && code <= 0x9FFF) ||   // CJK 统一汉字
            (code >= 0xFF01 && code <= 0xFFEF) ||   // 全角符号 / 全角字母数字 / 半角片假名区
            code == 0x3000) {                       // 全角空格
            return true;
        }
    }
    return false;
}

cdmanager::domain::cdtext::CdTextLanguage detectBurnLanguage(
    const QString& albumTitle,
    const QString& albumArtist,
    const QTreeWidget* tree
) {
    if (containsJapaneseText(albumTitle) || containsJapaneseText(albumArtist)) {
        return cdmanager::domain::cdtext::CdTextLanguage::Japanese;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const auto* item = tree->topLevelItem(i);
        if (containsJapaneseText(item->text(2)) || containsJapaneseText(item->text(3))) {
            return cdmanager::domain::cdtext::CdTextLanguage::Japanese;
        }
    }
    return cdmanager::domain::cdtext::CdTextLanguage::Latin;
}

}  // namespace

// Read WAV duration from the RIFF header.
static int wavDurationSeconds(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return 0;

    // RIFF header (44 bytes): skip to data size at offset 40.
    if (f.size() < 44) return 0;
    f.seek(40);
    QByteArray d = f.read(4);
    if (d.size() < 4) return 0;
    const uint32_t dataSize = *reinterpret_cast<const uint32_t*>(d.constData());

    // Check format: seek to offset 20 (audio format), expect 1 = PCM.
    f.seek(20);
    d = f.read(2);
    if (d.size() < 2) return 0;
    const uint16_t fmt = *reinterpret_cast<const uint16_t*>(d.constData());
    if (fmt != 1) return 0;  // PCM only

    // Get sample rate and channels.
    f.seek(22);
    d = f.read(2);
    const uint16_t ch = *reinterpret_cast<const uint16_t*>(d.constData());
    f.seek(24);
    d = f.read(4);
    const uint32_t sr = *reinterpret_cast<const uint32_t*>(d.constData());
    f.seek(34);
    d = f.read(2);
    const uint16_t bps = *reinterpret_cast<const uint16_t*>(d.constData());

    const int bytesPerSec = static_cast<int>(sr) * ch * (bps / 8);
    if (bytesPerSec <= 0) return 0;
    return static_cast<int>(dataSize) / bytesPerSec;
}

int probeAudioDurationSeconds(const QString& filePath) {
    const int wavSeconds = wavDurationSeconds(filePath);
    if (wavSeconds > 0) {
        return wavSeconds;
    }

    const QString ffprobePath = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!ffprobePath.isEmpty()) {
        QProcess process;
        process.start(
            ffprobePath,
            {
                QStringLiteral("-v"),
                QStringLiteral("error"),
                QStringLiteral("-show_entries"),
                QStringLiteral("format=duration"),
                QStringLiteral("-of"),
                QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                filePath
            }
        );
        if (process.waitForFinished(5000)) {
            bool ok = false;
            const double seconds = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&ok);
            if (ok && seconds > 0.0) {
                return static_cast<int>(seconds + 0.5);
            }
        }
    }

    return 0;
}

QString mmssText(int totalSeconds) {
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

BurnWidget::BurnWidget(QWidget* parent)
    : QWidget(parent) {
    setAcceptDrops(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    m_headerLabel = new QLabel(this);
    QFont f = m_headerLabel->font();
    f.setBold(true);
    m_headerLabel->setFont(f);
    root->addWidget(m_headerLabel);

    m_formatHintLabel = new QLabel(this);
    m_formatHintLabel->setWordWrap(true);
    root->addWidget(m_formatHintLabel);

    // Album info — empty, user fills in.
    auto* albumLayout = new QHBoxLayout();
    m_albumTitleLabel = new QLabel(this);
    albumLayout->addWidget(m_albumTitleLabel);
    m_albumTitleEdit = new QLineEdit(this);
    albumLayout->addWidget(m_albumTitleEdit, 1);
    m_albumArtistLabel = new QLabel(this);
    albumLayout->addWidget(m_albumArtistLabel);
    m_albumArtistEdit = new QLineEdit(this);
    albumLayout->addWidget(m_albumArtistEdit, 1);
    root->addLayout(albumLayout);

    // Track list
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral(""), QStringLiteral("#"),
                             QStringLiteral("Title"), QStringLiteral("Artist"),
                             QStringLiteral("Duration"), QStringLiteral("Source")});
    m_tree->setRootIsDecorated(false);
    m_tree->setDragDropMode(QAbstractItemView::DropOnly);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    root->addWidget(m_tree, 1);

    auto* trackActionsLayout = new QHBoxLayout();
    m_removeSelectedButton = new QPushButton(QStringLiteral("删除选中"), this);
    m_clearAllButton = new QPushButton(QStringLiteral("全部清空"), this);
    m_sortButton = new QPushButton(QStringLiteral("按文件名排序"), this);
    trackActionsLayout->addWidget(m_removeSelectedButton);
    trackActionsLayout->addWidget(m_clearAllButton);
    trackActionsLayout->addWidget(m_sortButton);
    trackActionsLayout->addStretch(1);
    root->addLayout(trackActionsLayout);

    // Simulation + progress
    auto* optLayout = new QHBoxLayout();
    m_simulationCheck = new QCheckBox(this);
    m_simulationCheck->setChecked(false);
    optLayout->addWidget(m_simulationCheck);
    m_gapLabel = new QLabel(this);
    optLayout->addWidget(m_gapLabel);
    m_gapCombo = new QComboBox(this);
    optLayout->addWidget(m_gapCombo);
    m_speedLabel = new QLabel(this);
    optLayout->addWidget(m_speedLabel);
    m_speedCombo = new QComboBox(this);
    optLayout->addWidget(m_speedCombo);
    m_cdTextLabel = new QLabel(this);
    optLayout->addWidget(m_cdTextLabel);
    m_languageCombo = new QComboBox(this);
    optLayout->addWidget(m_languageCombo);
    m_overburnCheck = new QCheckBox(this);
    optLayout->addWidget(m_overburnCheck);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    optLayout->addWidget(m_progressBar);
    root->addLayout(optLayout);

    // Burn button
    auto* bottom = new QHBoxLayout();
    m_burnButton = new QPushButton(this);
    m_statusLabel = new QLabel(this);
    m_capacityLabel = new QLabel(this);
    bottom->addWidget(m_burnButton);
    bottom->addWidget(m_capacityLabel, 1);
    bottom->addWidget(m_statusLabel, 1);
    root->addLayout(bottom);

    connect(m_gapCombo, &QComboBox::currentIndexChanged, this, [this]() { updateCapacitySummary(); });
    connect(m_overburnCheck, &QCheckBox::checkStateChanged, this, [this]() { updateCapacitySummary(); });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) { updateCapacitySummary(); });
    connect(m_removeSelectedButton, &QPushButton::clicked, this, [this]() {
        const auto selectedItems = m_tree->selectedItems();
        if (selectedItems.isEmpty()) {
            m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"没有选中的音轨。", u"No tracks selected."));
            return;
        }

        for (auto* item : selectedItems) {
            delete item;
        }
        renumberTracks();
        updateCapacitySummary();
        m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"已删除选中的音轨。", u"Selected tracks removed."));
    });
    connect(m_clearAllButton, &QPushButton::clicked, this, [this]() {
        clearTrackList();
        m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"已清空刻录列表。", u"Burn list cleared."));
    });
    connect(m_sortButton, &QPushButton::clicked, this, [this]() {
        sortTracksBySourceFile();
        updateCapacitySummary();
        m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"已按文件名排序。", u"Sorted by filename."));
    });

    connect(m_burnButton, &QPushButton::clicked, this, [this]() {
        if (m_tree->topLevelItemCount() == 0) {
            m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"还没有可刻录的音轨。", u"No tracks to burn."));
            return;
        }
        QVector<int> selected;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto* item = m_tree->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) selected.append(i);
        }
        if (selected.isEmpty()) {
            m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"至少选择一条音轨。", u"Select at least one track."));
            return;
        }

        cdmanager::domain::project::CdProject project;
        collectEditedProject(project);

        m_burnButton->setEnabled(false);
        m_progressBar->setValue(0);
        m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"正在刻录…", u"Burning..."));
        // Use track indices to reference items in the tree.
        Q_EMIT burnRequested(project, m_devicePath, selected,
                             m_simulationCheck->isChecked(),
                             m_speedCombo->currentData().toInt());
    });

    retranslateUi();
}

void BurnWidget::setProject(const cdmanager::domain::project::CdProject& project,
                             const QString& devicePath) {
    m_devicePath = devicePath;

    m_albumTitleEdit->setText(project.albumTitle);
    m_albumArtistEdit->setText(project.albumArtist);
    m_tree->clear();
    for (const auto& track : project.tracks) {
        auto* item = new QTreeWidgetItem();
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, Qt::Checked);
        item->setText(1, QString::number(track.number));
        item->setText(2, track.title);
        item->setText(3, track.artist);
        if (track.durationSeconds > 0) {
            const int min = track.durationSeconds / 60;
            const int sec = track.durationSeconds % 60;
            item->setText(4, QStringLiteral("%1:%2").arg(min).arg(sec, 2, 10, QLatin1Char('0')));
        } else {
            item->setText(4, QStringLiteral("Auto"));
        }
        item->setData(4, Qt::UserRole, track.durationSeconds);
        item->setText(5, track.filePath);
        m_tree->addTopLevelItem(item);
    }
    updateCapacitySummary();
    m_statusLabel->clear();
}

cdmanager::domain::project::CdProject BurnWidget::currentProject() const {
    cdmanager::domain::project::CdProject project;
    collectEditedProject(project);
    return project;
}

bool BurnWidget::hasUserAuthoredContent() const {
    if (!m_albumTitleEdit->text().trimmed().isEmpty() || !m_albumArtistEdit->text().trimmed().isEmpty()) {
        return true;
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const auto* item = m_tree->topLevelItem(i);
        if (item == nullptr) {
            continue;
        }
        if (!item->text(2).trimmed().isEmpty() || !item->text(3).trimmed().isEmpty() ||
            !item->text(5).trimmed().isEmpty()) {
            return true;
        }
    }

    return false;
}

void BurnWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
    updateCapacitySummary();
}

void BurnWidget::collectEditedProject(
    cdmanager::domain::project::CdProject& out) const {
    out.albumTitle = m_albumTitleEdit->text().trimmed();
    out.albumArtist = m_albumArtistEdit->text().trimmed();
    out.trackGapSeconds = m_gapCombo->currentData().toInt();
    out.allowOverburn = m_overburnCheck->isChecked();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) continue;

        cdmanager::domain::project::Track track;
        track.number = item->text(1).toInt();
        track.title = item->text(2);
        track.artist = item->text(3);
        track.filePath = item->text(5);  // source file path
        track.durationSeconds = item->data(4, Qt::UserRole).toInt();
        out.tracks.append(track);
    }

    const QString mode = m_languageCombo->currentData().toString();
    if (mode == QStringLiteral("jp")) {
        out.cdTextLanguage = cdmanager::domain::cdtext::CdTextLanguage::Japanese;
    } else if (mode == QStringLiteral("latin")) {
        out.cdTextLanguage = cdmanager::domain::cdtext::CdTextLanguage::Latin;
    } else {
        out.cdTextLanguage = detectBurnLanguage(out.albumTitle, out.albumArtist, m_tree);
    }
}

void BurnWidget::onBurnFinished(bool ok) {
    m_burnButton->setEnabled(true);
    m_progressBar->setValue(ok ? 100 : 0);
    m_statusLabel->setText(ok
        ? cdmanager::presentation::ui::text(m_language, u"刻录完成。", u"Burn complete.")
        : cdmanager::presentation::ui::text(m_language, u"刻录失败。", u"Burn failed."));
}

void BurnWidget::onBurnProgress(int percent, const QString& phase) {
    m_progressBar->setValue(percent);
    m_statusLabel->setText(phase);
}

void BurnWidget::clearTrackList() {
    m_tree->clear();
    m_progressBar->setValue(0);
    updateCapacitySummary();
}

void BurnWidget::renumberTracks() {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        m_tree->topLevelItem(i)->setText(1, QString::number(i + 1));
    }
}

void BurnWidget::sortTracksBySourceFile() {
    QVector<QTreeWidgetItem*> items;
    items.reserve(m_tree->topLevelItemCount());
    while (m_tree->topLevelItemCount() > 0) {
        items.append(m_tree->takeTopLevelItem(0));
    }

    std::sort(items.begin(), items.end(), [](const QTreeWidgetItem* lhs, const QTreeWidgetItem* rhs) {
        const QFileInfo leftInfo(lhs->text(5));
        const QFileInfo rightInfo(rhs->text(5));
        return QString::localeAwareCompare(leftInfo.fileName(), rightInfo.fileName()) < 0;
    });

    for (auto* item : items) {
        m_tree->addTopLevelItem(item);
    }
    renumberTracks();
}

void BurnWidget::updateCapacitySummary() {
    int selectedTracks = 0;
    int totalSeconds = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) {
            continue;
        }
        ++selectedTracks;
        totalSeconds += item->data(4, Qt::UserRole).toInt();
    }

    if (selectedTracks == 0) {
        m_capacityLabel->setText(cdmanager::presentation::ui::text(m_language, u"容量预测：暂无选中的音轨", u"Capacity: no selected tracks"));
        return;
    }

    const int gapSeconds = m_gapCombo->currentData().toInt();
    const int gapTotal = qMax(0, selectedTracks - 1) * gapSeconds;
    const int finalSeconds = totalSeconds + gapTotal;
    const QString overburnHint = m_overburnCheck->isChecked()
        ? cdmanager::presentation::ui::text(m_language, u" | 已允许超刻", u" | overburn enabled")
        : QString();

    if (finalSeconds <= 74 * 60) {
        m_capacityLabel->setText(
            cdmanager::presentation::ui::text(m_language,
                u"容量预测：%1（音轨 %2 + 间隔 %3），适合 74/80 分钟盘%4",
                u"Capacity: %1 (tracks %2 + gaps %3), fits 74/80 min discs%4")
                .arg(mmssText(finalSeconds))
                .arg(mmssText(totalSeconds))
                .arg(mmssText(gapTotal))
                .arg(overburnHint)
        );
    } else if (finalSeconds <= 80 * 60) {
        m_capacityLabel->setText(
            cdmanager::presentation::ui::text(m_language,
                u"容量预测：%1（音轨 %2 + 间隔 %3），需要 80 分钟盘%4",
                u"Capacity: %1 (tracks %2 + gaps %3), needs an 80 min disc%4")
                .arg(mmssText(finalSeconds))
                .arg(mmssText(totalSeconds))
                .arg(mmssText(gapTotal))
                .arg(overburnHint)
        );
    } else {
        m_capacityLabel->setText(
            cdmanager::presentation::ui::text(m_language,
                u"容量预测：%1（音轨 %2 + 间隔 %3），超出标准 80 分钟容量%4",
                u"Capacity: %1 (tracks %2 + gaps %3), exceeds standard 80 min capacity%4")
                .arg(mmssText(finalSeconds))
                .arg(mmssText(totalSeconds))
                .arg(mmssText(gapTotal))
                .arg(overburnHint)
        );
    }
}

void BurnWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void BurnWidget::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    int nextNum = m_tree->topLevelItemCount();
    for (const auto& url : urls) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        QFileInfo fi(path);
        if (!cdmanager::infrastructure::audio::AudioBurnSourcePreparer::isSupportedSourceFile(path)) {
            continue;
        }

        ++nextNum;
        auto* item = new QTreeWidgetItem();
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, Qt::Checked);
        item->setText(1, QString::number(nextNum));
        item->setText(2, fi.completeBaseName());  // Title = filename without extension
        item->setText(3, QString());               // Artist = blank
        const int dur = probeAudioDurationSeconds(path);
        if (dur > 0) {
            const int min = dur / 60;
            const int sec = dur % 60;
            item->setText(4, QStringLiteral("%1:%2").arg(min).arg(sec, 2, 10, QLatin1Char('0')));
        } else {
            item->setText(4, cdmanager::presentation::ui::text(m_language, u"自动", u"Auto"));
        }
        item->setData(4, Qt::UserRole, dur);
        item->setText(5, path);                    // Source = full path
        m_tree->addTopLevelItem(item);
    }
    renumberTracks();
    updateCapacitySummary();
}

void BurnWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_headerLabel->setText(text(m_language, u"音频 CD 刻录", u"Audio CD Burn"));
    m_formatHintLabel->setText(text(
        m_language,
        u"支持的输入格式：%1",
        u"Supported input: %1"
    ).arg(cdmanager::infrastructure::audio::AudioBurnSourcePreparer::supportedSourceSummary()));
    m_albumTitleLabel->setText(text(m_language, u"专辑名：", u"Album Title:"));
    m_albumArtistLabel->setText(text(m_language, u"专辑艺术家：", u"Album Artist:"));
    m_gapLabel->setText(text(m_language, u"间隔：", u"Gap:"));
    m_speedLabel->setText(text(m_language, u"速度：", u"Speed:"));
    m_cdTextLabel->setText(text(m_language, u"CD-TEXT：", u"CD-TEXT:"));
    m_simulationCheck->setText(text(m_language, u"模拟刻录（测试流程，不真正写盘）", u"Simulation (test burn, no real write)"));
    m_overburnCheck->setText(text(m_language, u"允许超刻（高级）", u"Allow overburn (advanced)"));
    m_removeSelectedButton->setText(text(m_language, u"删除选中", u"Remove Selected"));
    m_clearAllButton->setText(text(m_language, u"全部清空", u"Clear All"));
    m_sortButton->setText(text(m_language, u"按文件名排序", u"Sort by Filename"));
    m_burnButton->setText(text(m_language, u"开始刻录", u"Burn"));

    m_tree->setHeaderLabels({
        QString(),
        text(m_language, u"#", u"#"),
        text(m_language, u"标题", u"Title"),
        text(m_language, u"艺术家", u"Artist"),
        text(m_language, u"时长", u"Duration"),
        text(m_language, u"来源", u"Source"),
    });

    m_gapCombo->clear();
    m_gapCombo->addItem(text(m_language, u"2 秒（标准）", u"2 sec (standard)"), 2);
    m_gapCombo->addItem(text(m_language, u"0 秒（无间隔）", u"0 sec (gapless)"), 0);

    m_speedCombo->clear();
    m_speedCombo->addItem(text(m_language, u"8x（音频 CD 推荐）", u"8x (recommended)"), 8);
    m_speedCombo->addItem(QStringLiteral("4x"), 4);
    m_speedCombo->addItem(QStringLiteral("16x"), 16);
    m_speedCombo->addItem(QStringLiteral("24x"), 24);
    m_speedCombo->addItem(text(m_language, u"最高速度", u"Maximum"), 0);

    m_languageCombo->clear();
    m_languageCombo->addItem(text(m_language, u"自动", u"Auto"), QStringLiteral("auto"));
    m_languageCombo->addItem(text(m_language, u"日文（MS-JIS）", u"Japanese (MS-JIS)"), QStringLiteral("jp"));
    m_languageCombo->addItem(text(m_language, u"拉丁（ISO-8859-1）", u"Latin (ISO-8859-1)"), QStringLiteral("latin"));
}

}  // namespace cdmanager::presentation::editor
