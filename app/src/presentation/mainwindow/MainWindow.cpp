#include "cdmanager/presentation/mainwindow/MainWindow.h"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QSlider>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QProcess>
#include <QFile>
#include <QJsonDocument>
#include <QPalette>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QShowEvent>

#include "cdmanager/presentation/editor/AlbumDetailsWidget.h"
#include "cdmanager/presentation/editor/BurnWidget.h"
#include "cdmanager/presentation/editor/CdTextPreviewWidget.h"
#include "cdmanager/presentation/editor/DiscAnalysisWidget.h"
#include "cdmanager/presentation/editor/ExportWidget.h"
#include "cdmanager/presentation/editor/TrackTableWidget.h"
#include "cdmanager/presentation/ui/MacVisualEffectHelper.h"
#include "cdmanager/infrastructure/build/BuildFeatures.h"
#include "cdmanager/infrastructure/disc/DiscDeviceGatewayFactory.h"
#include "cdmanager/application/ExportService.h"
#include "cdmanager/infrastructure/audio/AudioBurnSourcePreparer.h"
#include "cdmanager/infrastructure/burn/CdrecordBurner.h"
#include "cdmanager/infrastructure/burn/CdrdaoBurner.h"
#include "cdmanager/infrastructure/burn/DiscRecordingBurner.h"
#include "cdmanager/infrastructure/burn/DrutilBurner.h"
#include "cdmanager/infrastructure/burn/TocFileWriter.h"
#include "cdmanager/tools/cdtextdiff/CdTextDocumentParser.h"
#include "cdmanager/tools/cdtextdiff/CdTextDiffEngine.h"
#include "cdmanager/tools/cdtextdiff/CdTextDiffTypes.h"
#include <QTemporaryDir>
#include "cdmanager/infrastructure/disc/DrutilCommandRunner.h"
#include "cdmanager/infrastructure/disc/DrutilOutputParser.h"

namespace cdmanager::presentation::mainwindow {

namespace {

QString effectiveBurnDevicePath(const QString& requestedDevicePath) {
    if (!requestedDevicePath.trimmed().isEmpty()) {
        return requestedDevicePath.trimmed();
    }

    return cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath();
}

QString burnProgressPhaseLabel(const QString& phase) {
    if (phase.isEmpty()) {
        return QStringLiteral("Burning...");
    }
    return QStringLiteral("Burning: %1").arg(phase);
}

QString trackBurnStatusText(int trackNumber, int trackCount) {
    return QStringLiteral("正在刻录音轨 %1/%2").arg(trackNumber).arg(trackCount);
}

QString conversionStatusText(int currentIndex, int totalCount, const QString& sourceFile) {
    return QStringLiteral("正在转换音频文件 %1/%2：%3")
        .arg(currentIndex)
        .arg(totalCount)
        .arg(QFileInfo(sourceFile).fileName());
}

QString wholeDiskPathForDevice(const QString& devicePath) {
    if (devicePath.startsWith(QStringLiteral("/dev/rdisk"))) {
        return QStringLiteral("/dev/disk") + devicePath.mid(QStringLiteral("/dev/rdisk").size());
    }
    return devicePath;
}

QString unmountDiskForExclusiveBurn(const QString& devicePath, bool skipForBlankWritableDisc) {
    if (skipForBlankWritableDisc) {
        return QStringLiteral("Unmount skipped: blank writable media does not need force-unmount preflight.");
    }

    const QString wholeDiskPath = wholeDiskPathForDevice(devicePath).trimmed();
    if (wholeDiskPath.isEmpty()) {
        return QStringLiteral("Unmount skipped: no disk path.");
    }

    QProcess proc;
    proc.start(QStringLiteral("/usr/sbin/diskutil"),
               {QStringLiteral("unmountDisk"), QStringLiteral("force"), wholeDiskPath});
    if (!proc.waitForStarted(5000)) {
        return QStringLiteral("Unmount failed: could not start diskutil for %1").arg(wholeDiskPath);
    }
    proc.waitForFinished(30000);

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    const QString err = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    QString details = QStringLiteral("Unmount command: diskutil unmountDisk force %1").arg(wholeDiskPath);
    if (!out.isEmpty()) {
        details += QStringLiteral("\nstdout:\n%1").arg(out);
    }
    if (!err.isEmpty()) {
        details += QStringLiteral("\nstderr:\n%1").arg(err);
    }
    details += QStringLiteral("\nexit: %1").arg(proc.exitCode());
    return details;
}

int totalBurnSeconds(const cdmanager::domain::project::CdProject& project) {
    int total = 0;
    for (const auto& track : project.tracks) {
        total += qMax(track.durationSeconds, 0);
    }
    total += qMax(0, project.tracks.size() - 1) * qMax(project.trackGapSeconds, 0);
    return total;
}

bool useDiscRecordingBackend() {
    const QString configured = qEnvironmentVariable("CDMANAGER_BURN_BACKEND").trimmed();
    if (configured.compare(QStringLiteral("cdrdao"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("drutil"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("cdrecord"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    if (configured.compare(QStringLiteral("discrecording"), Qt::CaseInsensitive) == 0) {
        return true;
    }
#ifdef Q_OS_MACOS
    return !cdmanager::infrastructure::burn::CdrdaoBurner::isAvailable()
        && !cdmanager::infrastructure::burn::CdrecordBurner::isAvailable();
#else
    return false;
#endif
}

bool useCdrecordBackend() {
    const QString configured = qEnvironmentVariable("CDMANAGER_BURN_BACKEND").trimmed();
    if (configured.compare(QStringLiteral("cdrecord"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (configured.compare(QStringLiteral("drutil"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("cdrdao"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("discrecording"), Qt::CaseInsensitive) == 0) {
        return false;
    }
#ifdef Q_OS_MACOS
    return !cdmanager::infrastructure::burn::CdrdaoBurner::isAvailable()
        && cdmanager::infrastructure::burn::CdrecordBurner::isAvailable();
#else
    return false;
#endif
}

bool useCdrdaoBackend() {
    const QString configured = qEnvironmentVariable("CDMANAGER_BURN_BACKEND").trimmed();
    if (configured.compare(QStringLiteral("cdrdao"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (configured.compare(QStringLiteral("drutil"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("cdrecord"), Qt::CaseInsensitive) == 0
        || configured.compare(QStringLiteral("discrecording"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return cdmanager::infrastructure::burn::CdrdaoBurner::isAvailable();
}

QString activeBurnBackendName() {
    if (useCdrecordBackend()) {
        return QStringLiteral("cdrecord");
    }
    if (useDiscRecordingBackend()) {
        return QStringLiteral("DiscRecording");
    }
    if (useCdrdaoBackend()) {
        return QStringLiteral("cdrdao");
    }
    return QStringLiteral("drutil");
}

cdmanager::infrastructure::burn::TocWriterTarget activeTocWriterTarget() {
    return useCdrdaoBackend()
        ? cdmanager::infrastructure::burn::TocWriterTarget::Cdrdao
        : cdmanager::infrastructure::burn::TocWriterTarget::Drutil;
}

QString driveIdForCurrentSession(const QString& lastDeviceId,
                                 const QVector<cdmanager::domain::disc::DriveInfo>& drives) {
    if (!lastDeviceId.isEmpty()) {
        return lastDeviceId;
    }
    if (!drives.isEmpty()) {
        return drives.first().deviceId;
    }
    return {};
}

int resolveDrutilDeviceIndex(const QString& driveId) {
    return cdmanager::infrastructure::burn::DrutilBurner::deviceIndexForPath(driveId);
}

QString referenceSampleDirPath() {
    return QStringLiteral(CDMANAGER_SOURCE_DIR)
        + QStringLiteral("/reference_samples/kureha_success_sana_collection_06");
}

cdmanager::tools::cdtextdiff::ParsedCdTextDocument documentFromPackAssembly(
    const cdmanager::application::burn::CdTextPackAssembly& assembly,
    const QString& sourceLabel)
{
    cdmanager::tools::cdtextdiff::ParsedCdTextDocument document;
    document.format = cdmanager::tools::cdtextdiff::InputFormat::PacksJson;
    document.sourcePath = sourceLabel;
    for (int index = 0; index < assembly.packs.size(); ++index) {
        cdmanager::tools::cdtextdiff::ParsedCdTextPack parsedPack;
        parsedPack.bytes = QByteArray(
            reinterpret_cast<const char*>(assembly.packs.at(index).data.data()),
            static_cast<qsizetype>(assembly.packs.at(index).data.size())
        );
        parsedPack.hasCrc = true;
        parsedPack.sourceIndex = index;
        parsedPack.sourceLabel = QStringLiteral("pack %1").arg(index);
        document.packs.append(parsedPack);
    }
    return document;
}

struct ReferenceStructureGateResult {
    bool ok {false};
    QString summary;
    QString details;
};

struct DiscRecordingBinaryGateResult {
    bool ok {false};
    QString summary;
    QString details;
};

ReferenceStructureGateResult validateReferenceStructureGate(
    const cdmanager::application::burn::CdTextPackAssembly& packAssembly)
{
    using namespace cdmanager::tools::cdtextdiff;

    CdTextDocumentParser parser;
    const auto referenceResult = parser.parseFile(
        referenceSampleDirPath(),
        InputFormat::ReferenceSample
    );
    if (!referenceResult.ok) {
        return {
            false,
            QStringLiteral("Burn aborted: failed to load reference sample baseline."),
            referenceResult.errorMessage
        };
    }

    const auto current = documentFromPackAssembly(packAssembly, QStringLiteral("current-pack-assembly"));
    const auto& reference = referenceResult.document;

    const CdTextDiffEngine engine;
    const auto report = engine.compare(reference, current, CompareMode::Schema);

    QStringList details;
    details.append(QStringLiteral("Reference schema gate"));
    details.append(QStringLiteral("Reference sample: %1").arg(referenceSampleDirPath()));
    details.append(QStringLiteral("Rule: text payload and content-length-driven pack counts may differ; only the binary CD-TEXT schema must match the reference baseline."));
    details.append(report.toText());

    if (report.identical) {
        details.append(QStringLiteral("Result: PASS"));
        return {
            true,
            QStringLiteral("Reference schema gate passed."),
            details.join(u'\n')
        };
    }

    details.append(QStringLiteral("Result: FAIL"));
    return {
        false,
        QStringLiteral("Burn aborted: current CD-TEXT pack schema does not match the reference sample baseline."),
        details.join(u'\n')
    };
}

DiscRecordingBinaryGateResult validateDiscRecordingBinaryGate(
    const cdmanager::application::burn::CdTextPackAssembly& packAssembly)
{
    QStringList details;
    details.append(QStringLiteral("DiscRecording binary gate"));
    details.append(QStringLiteral("Rule: the exact raw CD-TEXT pack blob must be accepted by DiscRecording and resolve to the same block count as the reference baseline."));

    cdmanager::tools::cdtextdiff::CdTextDocumentParser parser;
    const auto referenceResult = parser.parseFile(
        referenceSampleDirPath(),
        cdmanager::tools::cdtextdiff::InputFormat::ReferenceSample
    );
    if (!referenceResult.ok) {
        details.append(QStringLiteral("Reference parse error: %1").arg(referenceResult.errorMessage));
        details.append(QStringLiteral("Result: FAIL"));
        return {
            false,
            QStringLiteral("Burn aborted: failed to load reference sample for DiscRecording binary gate."),
            details.join(u'\n')
        };
    }

    const int expectedBlockCount = referenceResult.document.blockCount();
    details.append(QStringLiteral("Expected block count from reference: %1").arg(expectedBlockCount));

    const auto analysis
        = cdmanager::infrastructure::burn::DiscRecordingBurner::analyzeCdTextPacks(packAssembly.packs);
    details.append(analysis.diagnostics);

    if (analysis.ok && analysis.blockCount == expectedBlockCount) {
        details.append(QStringLiteral("Result: PASS"));
        return {
            true,
            QStringLiteral("DiscRecording binary gate passed."),
            details.join(u'\n')
        };
    }

    if (analysis.ok && analysis.blockCount != expectedBlockCount) {
        details.append(QStringLiteral("Block count mismatch: expected %1, got %2.")
                           .arg(expectedBlockCount)
                           .arg(analysis.blockCount));
    }

    details.append(QStringLiteral("Result: FAIL"));
    return {
        false,
        QStringLiteral("Burn aborted: DiscRecording binary gate did not match the reference baseline."),
        details.join(u'\n')
    };
}

QString writeCdTextArtifacts(const QString& dirPath,
                             const cdmanager::application::burn::CdTextPackAssembly& packAssembly) {
    QByteArray rawBlob;
    QByteArray sonyLeadInBlob;
    rawBlob.reserve(packAssembly.packs.size() * cdmanager::application::burn::kPackTotalSize);
    sonyLeadInBlob.reserve(packAssembly.packs.size() * cdmanager::application::burn::kPackTotalSize + 1);

    QStringList hexLines;
    hexLines.append(packAssembly.diagnosticSummary());
    hexLines.append(QString());

    for (int index = 0; index < packAssembly.packs.size(); ++index) {
        const auto& pack = packAssembly.packs.at(index);
        rawBlob.append(reinterpret_cast<const char*>(pack.data.data()),
                       cdmanager::application::burn::kPackTotalSize);
        sonyLeadInBlob.append(reinterpret_cast<const char*>(pack.data.data()),
                              cdmanager::application::burn::kPackTotalSize);
        hexLines.append(QStringLiteral("Pack %1: %2")
                            .arg(index)
                            .arg(pack.diagnosticString()));
    }
    sonyLeadInBlob.append('\0');

    const QString rawPath = dirPath + QStringLiteral("/cdtext-packs.bin");
    const QString cdtPath = dirPath + QStringLiteral("/cdtext-packs.cdt");
    const QString sonyBinPath = dirPath + QStringLiteral("/cdtext-leadin-sony.bin");
    const QString textPath = dirPath + QStringLiteral("/cdtext-packs.txt");

    QFile rawFile(rawPath);
    if (rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        rawFile.write(rawBlob);
    }

    QFile cdtFile(cdtPath);
    if (cdtFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        cdtFile.write(rawBlob);
    }

    QFile sonyBinFile(sonyBinPath);
    if (sonyBinFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sonyBinFile.write(sonyLeadInBlob);
    }

    QFile textFile(textPath);
    if (textFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        textFile.write(hexLines.join(u'\n').toUtf8());
    }

    return QStringLiteral("CD-TEXT artifacts:\n- raw: %1\n- cdt: %2\n- sony-bin: %3\n- text: %4")
        .arg(rawPath, cdtPath, sonyBinPath, textPath);
}

QString summarizeBurnRequest(const cdmanager::domain::project::CdProject& project,
                             const QString& backend,
                             const QString& requestedDevicePath,
                             const QString& resolvedDevicePath,
                             const QString& driveId,
                             const QString& tocLanguage,
                             bool simulation,
                             int speedX,
                             const QStringList& sourceFiles,
                             const QStringList& preparedWavFiles,
                             const QString& tocText,
                             const cdmanager::application::CdTextPreparationResult& preparation,
                             const cdmanager::application::CdTextWritePlan& writePlan,
                             const cdmanager::application::CdTextWritePayload& writePayload,
                             const cdmanager::application::burn::CdTextPackAssembly& packAssembly) {
    QString details;
    details += QStringLiteral("Last burn request:\n");
    details += QStringLiteral("Backend: %1\n").arg(backend);
    details += QStringLiteral("Simulation: %1\n").arg(simulation ? QStringLiteral("yes")
                                                                : QStringLiteral("no"));
    details += QStringLiteral("Speed: %1\n")
        .arg(speedX > 0 ? QStringLiteral("%1x").arg(speedX) : QStringLiteral("max"));
    details += QStringLiteral("Gap: %1 second(s)\n").arg(project.trackGapSeconds);
    details += QStringLiteral("Allow overburn: %1\n").arg(project.allowOverburn ? QStringLiteral("yes")
                                                                                 : QStringLiteral("no"));
    details += QStringLiteral("Requested device path: %1\n")
        .arg(requestedDevicePath.isEmpty() ? QStringLiteral("(empty)") : requestedDevicePath);
    details += QStringLiteral("Resolved device path: %1\n")
        .arg(resolvedDevicePath.isEmpty() ? QStringLiteral("(empty)") : resolvedDevicePath);
    details += QStringLiteral("Drive id: %1\n")
        .arg(driveId.isEmpty() ? QStringLiteral("(unknown)") : driveId);
    details += QStringLiteral("Album title: %1\n")
        .arg(project.albumTitle.isEmpty() ? QStringLiteral("(empty)") : project.albumTitle);
    details += QStringLiteral("Album artist: %1\n")
        .arg(project.albumArtist.isEmpty() ? QStringLiteral("(empty)") : project.albumArtist);
    details += QStringLiteral("Track count: %1\n").arg(project.tracks.size());
    details += QStringLiteral("CD-TEXT language: %1\n").arg(tocLanguage);
    if (useCdrecordBackend()) {
        details += QStringLiteral("Layout target: cdrecord cue\n");
    } else {
        details += QStringLiteral("TOC target: %1\n").arg(
            activeTocWriterTarget() == cdmanager::infrastructure::burn::TocWriterTarget::Cdrdao
                ? QStringLiteral("cdrdao")
                : QStringLiteral("drutil")
        );
    }
    details += QStringLiteral("Prepared fields: %1\n").arg(preparation.preparedFields.size());
    details += QStringLiteral("Write plan: writable=%1, skipped=%2\n")
        .arg(writePlan.writableFieldCount())
        .arg(writePlan.skippedFieldCount());
    details += QStringLiteral("Write payload: album writable=%1, track groups=%2, writable bytes=%3\n")
        .arg(writePayload.albumWritableFields.size())
        .arg(writePayload.tracks.size())
        .arg(writePayload.writableByteCount());
    details += QStringLiteral("%1\n").arg(packAssembly.diagnosticSummary());
    if (!sourceFiles.isEmpty()) {
        details += QStringLiteral("Source audio files:\n");
        for (const auto& sourceFile : sourceFiles) {
            details += QStringLiteral("- %1\n").arg(sourceFile);
        }
    }
    if (!preparedWavFiles.isEmpty()) {
        details += QStringLiteral("Prepared audio files:\n");
        for (const auto& preparedFile : preparedWavFiles) {
            details += QStringLiteral("- %1\n").arg(preparedFile);
        }
    }
    if (!tocText.isEmpty()) {
        details += QStringLiteral("\nGenerated TOC:\n%1").arg(tocText.trimmed());
    }
    return details.trimmed();
}

bool statusOutputLooksWritableBlankMedia(const QString& statusOutput) {
    const QString normalized = statusOutput.toLower();
    if (normalized.contains(QStringLiteral("type: cd-rw")) ||
        normalized.contains(QStringLiteral("type: dvd-rw")) ||
        normalized.contains(QStringLiteral("type: bd-re"))) {
        return true;
    }

    const QRegularExpression usedRe(
        QStringLiteral(R"(space used:\s+([0-9:]+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto usedMatch = usedRe.match(statusOutput);
    if (usedMatch.hasMatch() && usedMatch.captured(1) != QStringLiteral("00:00:00")) {
        return false;
    }

    const QRegularExpression tracksRe(
        QStringLiteral(R"(tracks:\s+(\d+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto tracksMatch = tracksRe.match(statusOutput);
    if (tracksMatch.hasMatch() && tracksMatch.captured(1).toInt() > 0) {
        return false;
    }

    const QRegularExpression freeRe(
        QStringLiteral(R"(space free:\s+([0-9:]+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto freeMatch = freeRe.match(statusOutput);
    if (freeMatch.hasMatch()) {
        return freeMatch.captured(1) != QStringLiteral("00:00:00");
    }

    return false;
}

QString mediaStatusSignature(const QString& statusOutput) {
    const QString normalized = statusOutput.simplified().toLower();

    const QRegularExpression typeRe(
        QStringLiteral(R"(type:\s*([^\n]+?)(?:name:|$))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpression tracksRe(
        QStringLiteral(R"(tracks:\s*(\d+))"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpression usedRe(
        QStringLiteral(R"(space used:\s*([0-9:]+))"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QString type = typeRe.match(normalized).captured(1).simplified();
    const QString tracks = tracksRe.match(normalized).captured(1);
    const QString used = usedRe.match(normalized).captured(1);

    return QStringLiteral("type=%1|tracks=%2|used=%3")
        .arg(type)
        .arg(tracks)
        .arg(used);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_project(),
      m_initialImportResult(),
      m_gateway(cdmanager::infrastructure::disc::DiscDeviceGatewayFactory::create()),
      m_discImportService(*m_gateway) {
    m_language = cdmanager::presentation::ui::detectInitialLanguage();
    m_themeMode = cdmanager::presentation::ui::detectInitialThemeMode();
    m_darkMode = cdmanager::presentation::ui::resolveDarkMode(m_themeMode);
    statusBar()->showMessage(QStringLiteral("正在读取光碟目录信息…"));

    m_initialImportResult = m_discImportService.initialImport();
    m_project = m_initialImportResult.project;
    m_lastTrackCount = m_project.tracks.size();
    buildUi();

    statusBar()->showMessage(
        QStringLiteral("界面已就绪 — 当前音轨数：%1").arg(m_project.tracks.size()), 3000
    );

    const auto drives = m_discImportService.availableDrives();
    const bool hasSystemDrive = !drives.isEmpty()
        && m_gateway->mode() == cdmanager::infrastructure::disc::GatewayMode::System;
    if (hasSystemDrive) {
        m_lastDeviceId = drives.first().deviceId;
        m_discMediaMonitor.start();
    }

    connect(
        &m_discMediaMonitor,
        &cdmanager::infrastructure::disc::DiscMediaMonitor::mediaChanged,
        this,
        [this]() {
            if (m_mediaRefreshDebounceTimer != nullptr) {
                m_mediaRefreshDebounceTimer->start();
            }
        }
    );

    refreshProjectView();
    if (m_initialImportResult.status == cdmanager::application::import::DiscImportStatus::Success
        && m_discAnalysisWidget != nullptr) {
        m_discAnalysisWidget->analyzeCurrentDisc();
    }

    connect(
        QGuiApplication::styleHints(),
        &QStyleHints::colorSchemeChanged,
        this,
        [this](Qt::ColorScheme) {
            if (m_themeMode == cdmanager::presentation::ui::UiThemeMode::System) {
                applyCurrentTheme();
            }
        }
    );
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("MultiLanguageCDManager"));
    resize(1100, 700);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainSurface"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    m_tabWidget = new QTabWidget(central);
    rootLayout->addWidget(m_tabWidget);

    m_mediaRefreshDebounceTimer = new QTimer(this);
    m_mediaRefreshDebounceTimer->setSingleShot(true);
    m_mediaRefreshDebounceTimer->setInterval(1200);
    connect(
        m_mediaRefreshDebounceTimer,
        &QTimer::timeout,
        this,
        [this]() { refreshFromCurrentDriveState(); }
    );

    m_mediaRefreshCooldownTimer = new QTimer(this);
    m_mediaRefreshCooldownTimer->setSingleShot(true);
    m_mediaRefreshCooldownTimer->setInterval(4000);
    connect(
        m_mediaRefreshCooldownTimer,
        &QTimer::timeout,
        this,
        [this]() { m_mediaRefreshCoolingDown = false; }
    );

    m_burnProgressTimer = new QTimer(this);
    m_burnProgressTimer->setInterval(500);
    connect(
        m_burnProgressTimer,
        &QTimer::timeout,
        this,
        [this]() { startEstimatedBurnProgress(); }
    );

    // ---- Player tab ----
    auto* playerTab = new QWidget();
    auto* playerLayout = new QVBoxLayout(playerTab);
    playerLayout->setContentsMargins(12, 12, 12, 12);
    playerLayout->setSpacing(8);

    m_albumDetailsWidget = new cdmanager::presentation::editor::AlbumDetailsWidget(this);
    m_trackCountLabel = new QLabel(this);
    m_trackTableWidget = new cdmanager::presentation::editor::TrackTableWidget(this);

    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setEnabled(false);
    m_positionSlider->setTracking(true);

    auto* playbackLayout = new QHBoxLayout();
    m_playPauseButton = new QPushButton(QStringLiteral("Play"), this);
    m_playPauseButton->setEnabled(false);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), this);
    m_stopButton->setEnabled(false);
    m_prevButton = new QPushButton(QStringLiteral("Prev"), this);
    m_nextButton = new QPushButton(QStringLiteral("Next"), this);
    m_modeButton = new QPushButton(QStringLiteral("Normal"), this);
    m_ejectButton = new QPushButton(QStringLiteral("Eject"), this);
    m_playbackStatusLabel = new QLabel(QStringLiteral("No track playing"), this);
    playbackLayout->addWidget(m_playPauseButton);
    playbackLayout->addWidget(m_stopButton);
    playbackLayout->addWidget(m_prevButton);
    playbackLayout->addWidget(m_nextButton);
    playbackLayout->addWidget(m_modeButton);
    playbackLayout->addWidget(m_ejectButton);
    playbackLayout->addWidget(m_playbackStatusLabel, 1);

    playerLayout->addWidget(m_albumDetailsWidget);
    playerLayout->addWidget(m_trackCountLabel);
    playerLayout->addWidget(m_positionSlider);
    playerLayout->addLayout(playbackLayout);
    playerLayout->addWidget(m_trackTableWidget, 1);

    m_tabWidget->addTab(playerTab, QString());

    // ---- Console tab ----
    auto* consoleTab = new QWidget();
    auto* consoleLayout = new QVBoxLayout(consoleTab);
    consoleLayout->setContentsMargins(12, 12, 12, 12);
    consoleLayout->setSpacing(6);

    m_driveStatusLabel = new QLabel(this);
    m_buildFeaturesLabel = new QLabel(this);
    m_importSummaryLabel = new QLabel(this);
    m_validationStatusLabel = new QLabel(this);
    m_validationStatusLabel->setWordWrap(true);
    m_cdTextPreviewWidget = new cdmanager::presentation::editor::CdTextPreviewWidget(this);

    m_validationDetails = new QPlainTextEdit(this);
    m_validationDetails->setReadOnly(true);
    m_validationDetails->setPlaceholderText(QString());

    consoleLayout->addWidget(m_driveStatusLabel);
    consoleLayout->addWidget(m_buildFeaturesLabel);
    consoleLayout->addWidget(m_importSummaryLabel);
    consoleLayout->addWidget(m_validationStatusLabel);
    consoleLayout->addWidget(m_cdTextPreviewWidget, 1);
    consoleLayout->addWidget(m_validationDetails, 3);

    m_tabWidget->addTab(consoleTab, QString());

    // ---- Analysis tab ----
    m_discAnalysisWidget = new cdmanager::presentation::editor::DiscAnalysisWidget(this);
    m_tabWidget->addTab(m_discAnalysisWidget, QString());
    connect(
        m_discAnalysisWidget,
        &cdmanager::presentation::editor::DiscAnalysisWidget::analysisStarted,
        this,
        [this]() {
            statusBar()->showMessage(QStringLiteral("正在分析光碟结构与音轨信息…"));
        }
    );
    connect(
        m_discAnalysisWidget,
        &cdmanager::presentation::editor::DiscAnalysisWidget::analysisFinished,
        this,
        [this](bool healthy) {
            statusBar()->showMessage(
                healthy
                    ? QStringLiteral("光碟分析完成：未见明显结构异常。")
                    : QStringLiteral("光碟分析完成：检测到可疑结构特征。"),
                5000
            );
        }
    );

    // ---- Export tab ----
    m_exportWidget = new cdmanager::presentation::editor::ExportWidget(this);
    m_tabWidget->addTab(m_exportWidget, QString());

    // ---- Burn tab ----
    m_burnWidget = new cdmanager::presentation::editor::BurnWidget(this);
    m_tabWidget->addTab(m_burnWidget, QString());

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Ready."));

    // Playback connections
    connect(m_playPauseButton, &QPushButton::clicked,
            this, &MainWindow::onPlayPauseClicked);
    connect(m_stopButton, &QPushButton::clicked,
            this, &MainWindow::onStopClicked);
    connect(m_prevButton, &QPushButton::clicked, [this]() {
        m_playbackService.previousTrack();
    });
    connect(m_nextButton, &QPushButton::clicked, [this]() {
        m_playbackService.nextTrack();
    });
    connect(m_modeButton, &QPushButton::clicked, [this]() {
        m_playbackService.cyclePlaybackMode();
    });
    connect(&m_playbackService, &cdmanager::application::PlaybackService::playbackModeChanged,
            this, [this](cdmanager::application::PlaybackMode mode) {
                m_modeButton->setText(m_playbackService.playbackModeLabel());
                Q_UNUSED(mode)
            });
    connect(m_ejectButton, &QPushButton::clicked,
            this, [this]() {
                statusBar()->showMessage(QStringLiteral("正在弹出光碟…"));
                m_playbackService.stop();
                const cdmanager::infrastructure::disc::DrutilCommandRunner runner;
                const auto drives = m_discImportService.availableDrives();
                if (!drives.isEmpty()) {
                    const QString idx = drives.first().deviceId.mid(QStringLiteral("drutil-index://").size());
                    const auto ejectResult = runner.run({QStringLiteral("-drive"), idx, QStringLiteral("eject")});
                    if (ejectResult.ok) {
                        statusBar()->showMessage(QStringLiteral("光碟弹出指令已发送，等待设备反馈…"), 3000);
                    } else {
                        statusBar()->showMessage(QStringLiteral("弹出光碟失败：设备未响应。"), 5000);
                    }
                }
                m_discKeepAlive.stop();
            });
    connect(&m_playbackService, &cdmanager::application::PlaybackService::stateChanged,
            this, &MainWindow::onPlaybackStateChanged);
    connect(&m_playbackService, &cdmanager::application::PlaybackService::positionChanged,
            this, &MainWindow::onPlaybackPositionChanged);
    connect(&m_playbackService, &cdmanager::application::PlaybackService::playbackFinished,
            this, &MainWindow::onPlaybackFinished);
    connect(&m_playbackService, &cdmanager::application::PlaybackService::playbackError,
            this, [this](const QString& msg) {
                m_playbackStatusLabel->setText(QStringLiteral("Error: %1").arg(msg));
            });

    // Slider dragging — pause while user scrubs, then seek.
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() {
        m_isSeeking = true;
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
        m_isSeeking = false;
        m_playbackService.seek(m_positionSlider->value());
    });
    connect(m_trackTableWidget, &cdmanager::presentation::editor::TrackTableWidget::trackDoubleClicked,
            this, [this](int trackNumber) {
                m_playbackService.playTrack(trackNumber);
            });

    retranslateUi();
    applyCurrentTheme();
    // Burn request handler.
    connect(m_burnWidget, &cdmanager::presentation::editor::BurnWidget::burnRequested,
            this, [this](const cdmanager::domain::project::CdProject& burnProject,
                         const QString& devicePath,
                         const QVector<int>& selectedTracks,
                         bool simulation,
                         int speedX) {
                Q_UNUSED(selectedTracks)
                m_isExporting = true;
                m_lastBurnDiagnostics.clear();
                statusBar()->showMessage(QStringLiteral("Preparing burn..."));
                QCoreApplication::processEvents();

                cdmanager::domain::project::CdProject preparedBurnProject = burnProject;

                const QString resolvedDevicePath = effectiveBurnDevicePath(devicePath);
                if (resolvedDevicePath.isEmpty()) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    statusBar()->showMessage(
                        QStringLiteral("No writable optical drive path available."), 8000
                    );
                    return;
                }

                const auto drives = m_discImportService.availableDrives();
                const QString activeDriveId = driveIdForCurrentSession(m_lastDeviceId, drives);

                if (!useDiscRecordingBackend()) {
                    if (!activeDriveId.isEmpty()) {
                        const QString idx = activeDriveId.mid(QStringLiteral("drutil-index://").size());
                        const cdmanager::infrastructure::disc::DrutilCommandRunner runner;
                        const auto statusResult = runner.run(
                            {QStringLiteral("-drive"), idx, QStringLiteral("status")}
                        );
                        if (!statusResult.ok || !statusOutputLooksWritableBlankMedia(statusResult.stdOut)) {
                            m_isExporting = false;
                            m_burnWidget->onBurnFinished(false);
                            statusBar()->showMessage(
                                QStringLiteral("Burn aborted: insert a blank writable CD before burning."),
                                10000
                            );
                            return;
                        }
                    }
                }

                const auto validationReport = m_validationService.validateCdText(preparedBurnProject);
                if (!validationReport.ok) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    statusBar()->showMessage(
                        QStringLiteral("Burn aborted: CD-TEXT validation failed."), 8000
                    );
                    return;
                }

                QStringList sourceFiles;
                for (const auto& track : preparedBurnProject.tracks) {
                    if (!track.filePath.isEmpty()) {
                        sourceFiles.append(track.filePath);
                    }
                }
                if (sourceFiles.isEmpty()) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    statusBar()->showMessage(QStringLiteral("No audio files in track list."), 5000);
                    return;
                }

                const int projectedSeconds = totalBurnSeconds(preparedBurnProject);
                if (!preparedBurnProject.allowOverburn && projectedSeconds > 80 * 60) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    statusBar()->showMessage(
                        QStringLiteral("Burn aborted: project exceeds standard 80-minute CD capacity. Enable overburn to continue."),
                        12000
                    );
                    return;
                }

                statusBar()->showMessage(QStringLiteral("正在转换音频文件。。。"));
                m_burnWidget->onBurnProgress(5, QStringLiteral("Preparing CD-TEXT..."));
                QCoreApplication::processEvents();

                const auto preparation = m_preparationService.prepare(preparedBurnProject);
                const auto writePlan = m_cdTextWritePlanBuilder.build(preparation);
                const auto writePayload = m_cdTextWritePayloadBuilder.build(writePlan);
                const auto packAssembly = m_cdTextPackAssembler.assemble(writePayload);
                const auto referenceGate = validateReferenceStructureGate(packAssembly);
                const auto discRecordingBinaryGate = validateDiscRecordingBinaryGate(packAssembly);

                cdmanager::infrastructure::burn::BurnResult burnResult;
                auto tempDir = std::make_shared<QTemporaryDir>();
                tempDir->setAutoRemove(true);
                const cdmanager::infrastructure::audio::AudioBurnSourcePreparer audioPreparer;
                const auto preferredAudioContainer = (useCdrecordBackend() || useCdrdaoBackend())
                    ? cdmanager::infrastructure::audio::AudioBurnSourcePreparer::OutputContainer::Wave
                    : cdmanager::infrastructure::audio::AudioBurnSourcePreparer::OutputContainer::Aiff;
                m_burnWidget->onBurnProgress(15, QStringLiteral("正在转换音频文件。。。"));
                statusBar()->showMessage(QStringLiteral("正在转换音频文件。。。"));
                QCoreApplication::processEvents();
                const auto audioPreparation = audioPreparer.prepare(
                    sourceFiles,
                    tempDir->path(),
                    preferredAudioContainer,
                    [this](int currentIndex, int totalCount, const QString& sourceFile) {
                        const int percent = 10 + ((currentIndex - 1) * 20 / qMax(totalCount, 1));
                        const QString label = conversionStatusText(currentIndex, totalCount, sourceFile);
                        m_burnWidget->onBurnProgress(percent, label);
                        statusBar()->showMessage(label);
                        QCoreApplication::processEvents();
                    }
                );
                if (!audioPreparation.ok) {
                    burnResult.error = audioPreparation.error.isEmpty()
                        ? QStringLiteral("Could not prepare audio sources.")
                        : audioPreparation.error;
                    burnResult.diagnostics = audioPreparation.diagnostics;
                    m_lastBurnDiagnostics = QStringLiteral("Last burn request:\nAudio source preparation failed.\n");
                    if (!audioPreparation.diagnostics.trimmed().isEmpty()) {
                        m_lastBurnDiagnostics += QStringLiteral("\nAudio preparation diagnostics:\n%1")
                            .arg(audioPreparation.diagnostics.trimmed());
                    }
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    refreshProjectView();
                    statusBar()->showMessage(
                        QStringLiteral("Burn error: %1").arg(burnResult.error),
                        10000
                    );
                    return;
                }

                const QStringList wavFiles = audioPreparation.preparedAudioFiles;
                m_activeBurnDurationsSeconds = audioPreparation.preparedDurationsSeconds;
                m_activeBurnSpeedX = speedX > 0 ? speedX : 24;
                const bool useStandardPregap = preparedBurnProject.trackGapSeconds == 2;
                const QString tocPath = tempDir->filePath(QStringLiteral("disc.toc"));
                const QString cuePath = tempDir->filePath(QStringLiteral("disc.cue"));
                const QString cdtPath = tempDir->filePath(QStringLiteral("cdtext-packs.cdt"));
                const QString cdrecordCdTextPath = tempDir->filePath(QStringLiteral("cdrecord-cdtext.dat"));
                const QString cdTextArtifactSummary = writeCdTextArtifacts(tempDir->path(), packAssembly);
                const QString tocLanguage = preparedBurnProject.cdTextLanguage
                    == cdmanager::domain::cdtext::CdTextLanguage::Japanese
                    ? QStringLiteral("JP")
                    : QStringLiteral("EN");
                const auto tocTarget = activeTocWriterTarget();
                const QString tocText = (useCdrdaoBackend() || (!useCdrecordBackend() && !useDiscRecordingBackend()))
                    ? cdmanager::infrastructure::burn::TocFileWriter::buildTocText(
                        preparedBurnProject, writePayload, wavFiles, tocLanguage, tocTarget
                    )
                    : QString();
                const QString cueText = cdmanager::infrastructure::burn::CdrecordBurner::buildCueText(
                    wavFiles,
                    preparedBurnProject.trackGapSeconds
                );
                m_lastBurnDiagnostics = summarizeBurnRequest(
                    preparedBurnProject,
                    activeBurnBackendName(),
                    devicePath,
                    resolvedDevicePath,
                    activeDriveId,
                    tocLanguage,
                    simulation,
                    speedX,
                    sourceFiles,
                    wavFiles,
                    tocText,
                    preparation,
                    writePlan,
                    writePayload,
                    packAssembly
                );
                m_lastBurnDiagnostics += QStringLiteral("\n\n%1").arg(referenceGate.details);
                m_lastBurnDiagnostics += QStringLiteral("\n\n%1").arg(discRecordingBinaryGate.details);
                m_lastBurnDiagnostics += QStringLiteral("\n\n%1").arg(cdTextArtifactSummary);
                if (useCdrecordBackend()) {
                    const QString cdrecordDeviceSpec =
                        cdmanager::infrastructure::burn::CdrecordBurner::deviceSpecFor(
                            activeDriveId,
                            resolvedDevicePath
                        );
                    m_lastBurnDiagnostics += QStringLiteral(
                        "\n\ncdrecord RAW96 preview:\n"
                        "Executable: %1\n"
                        "Device spec: %2\n"
                        "Cue path: %3\n"
                        "CD-TEXT blob: %4\n"
                        "Cue text:\n%5")
                        .arg(cdmanager::infrastructure::burn::CdrecordBurner::executablePath(),
                             cdrecordDeviceSpec,
                             cuePath,
                             cdrecordCdTextPath,
                             cueText.trimmed());
                }
                if (useCdrdaoBackend()) {
                    const QString cdrdaoDeviceSpec =
                        cdmanager::infrastructure::burn::CdrdaoBurner::deviceSpecFor(
                            activeDriveId,
                            resolvedDevicePath
                        );
                    m_lastBurnDiagnostics += QStringLiteral(
                        "\n\ncdrdao write preview:\n"
                        "Driver: %1\n"
                        "Device spec: %2\n"
                        "TOC path: %3\n"
                        "Expected CD-TEXT blob: %4\n"
                        "TOC CD-TEXT mode: UTF-8 source text with ENCODING_MS_JIS for Japanese fields.")
                        .arg(cdmanager::infrastructure::burn::CdrdaoBurner::currentDriverSpec(),
                             cdrdaoDeviceSpec,
                             tocPath,
                             cdtPath);
                }
                if (!referenceGate.ok) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    refreshProjectView();
                    statusBar()->showMessage(referenceGate.summary, 12000);
                    return;
                }
                if (!discRecordingBinaryGate.ok) {
                    m_isExporting = false;
                    m_burnWidget->onBurnFinished(false);
                    refreshProjectView();
                    statusBar()->showMessage(discRecordingBinaryGate.summary, 12000);
                    return;
                }
                if (!audioPreparation.diagnostics.trimmed().isEmpty()) {
                    m_lastBurnDiagnostics += QStringLiteral("\n\nAudio preparation diagnostics:\n%1")
                        .arg(audioPreparation.diagnostics.trimmed());
                }
                bool layoutOk = true;
                QString layoutError;
                if (useCdrecordBackend()) {
                    layoutOk = cdmanager::infrastructure::burn::CdrecordBurner::writeCueFile(
                        cuePath,
                        wavFiles,
                        preparedBurnProject.trackGapSeconds,
                        &layoutError
                    );
                    if (layoutOk) {
                        layoutOk = cdmanager::infrastructure::burn::CdrecordBurner::writeCdTextFile(
                            cdrecordCdTextPath,
                            packAssembly.packs,
                            &layoutError
                        );
                    }
                } else {
                    layoutOk = cdmanager::infrastructure::burn::TocFileWriter::write(
                        tocPath, preparedBurnProject, writePayload, wavFiles, tocLanguage, tocTarget
                    );
                    if (!layoutOk) {
                        layoutError = QStringLiteral("Could not create temporary TOC file.");
                    }
                }
                if (!layoutOk) {
                    burnResult.error = layoutError;
                    burnResult.diagnostics = m_lastBurnDiagnostics + QStringLiteral("\n\nBurn result:\n%1").arg(layoutError);
                } else {
                    m_burnWidget->onBurnProgress(30, QStringLiteral("正在开始刻录，请勿触碰光驱、连接线、电源按钮"));
                    statusBar()->showMessage(QStringLiteral("正在开始刻录，请勿触碰光驱、连接线、电源按钮"));
                    QCoreApplication::processEvents();

#ifdef Q_OS_MACOS
                    if (useCdrecordBackend()) {
                        const QString cdrecordDeviceSpec =
                            cdmanager::infrastructure::burn::CdrecordBurner::deviceSpecFor(
                                activeDriveId,
                                resolvedDevicePath
                            );
                        auto guard = QPointer<MainWindow>(this);
                        auto future = QtConcurrent::run(
                            [guard,
                             cdrecordDeviceSpec,
                             resolvedDevicePath,
                             cuePath,
                             cdrecordCdTextPath,
                             simulation,
                             speedX,
                             allowOverburn = preparedBurnProject.allowOverburn,
                             skipUnmountPreflight = (m_initialImportResult.status
                                 == cdmanager::application::import::DiscImportStatus::BlankWritableMedia),
                             tempDir]() -> cdmanager::infrastructure::burn::BurnResult {
                                Q_UNUSED(tempDir)
                                QString preflightDiagnostics = unmountDiskForExclusiveBurn(
                                    resolvedDevicePath,
                                    skipUnmountPreflight
                                );
                                cdmanager::infrastructure::burn::CdrecordBurner burner;
                                burner.setSimulationMode(simulation);
                                burner.setBurnSpeed(speedX);
                                burner.setAllowOverburn(allowOverburn);
                                burner.setProgressCallback([guard](const cdmanager::infrastructure::burn::BurnProgress& progress) {
                                    if (!guard) {
                                        return;
                                    }
                                    QMetaObject::invokeMethod(
                                        guard,
                                        [guard, progress]() {
                                            if (!guard) {
                                                return;
                                            }
                                            QString label;
                                            if (progress.phase.contains(QStringLiteral("Writing track"), Qt::CaseInsensitive)) {
                                                const QRegularExpression re(QStringLiteral(R"(Writing track\s+(\d+))"),
                                                                            QRegularExpression::CaseInsensitiveOption);
                                                const auto match = re.match(progress.phase);
                                                if (match.hasMatch()) {
                                                    label = trackBurnStatusText(match.captured(1).toInt(),
                                                                                qMax(guard->m_activeBurnDurationsSeconds.size(), 1));
                                                }
                                            } else if (progress.phase.contains(QStringLiteral("Closing"), Qt::CaseInsensitive)) {
                                                label = QStringLiteral("正在进行终结处理");
                                            } else {
                                                label = QStringLiteral("正在刻录，请勿触碰光驱、连接线、电源按钮");
                                            }
                                            const int percent = progress.overallPercent > 0.f
                                                ? static_cast<int>(progress.overallPercent)
                                                : 50;
                                            guard->m_burnWidget->onBurnProgress(percent, label);
                                            guard->statusBar()->showMessage(label);
                                        },
                                        Qt::QueuedConnection
                                    );
                                });
                                auto result = burner.burn(cdrecordDeviceSpec, cuePath, cdrecordCdTextPath);
                                result.diagnostics = QStringLiteral("Preflight:\n%1\n\n%2")
                                    .arg(preflightDiagnostics.trimmed(),
                                         result.diagnostics.trimmed());
                                return result;
                            }
                        );
                        auto* watcher = new QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>(this);
                        connect(
                            watcher,
                            &QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>::finished,
                            this,
                            [this, guard, watcher]() {
                                const auto asyncResult = watcher->result();
                                watcher->deleteLater();
                                if (!guard) {
                                    return;
                                }

                                stopEstimatedBurnProgress();
                                m_isExporting = false;
                                if (!asyncResult.diagnostics.isEmpty()) {
                                    m_lastBurnDiagnostics += QStringLiteral("\n\nBackend diagnostics:\n%1")
                                        .arg(asyncResult.diagnostics);
                                }
                                m_burnWidget->onBurnFinished(asyncResult.ok);
                                refreshProjectView();
                                statusBar()->showMessage(
                                    asyncResult.ok
                                        ? QStringLiteral("Burn complete.")
                                        : QStringLiteral("Burn error: %1").arg(asyncResult.error),
                                    10000
                                );
                            }
                        );
                        watcher->setFuture(future);
                        return;
                    } else if (useDiscRecordingBackend()) {
                        cdmanager::infrastructure::burn::DiscRecordingBurner burner;
                        burner.setSimulationMode(simulation);
                        burner.setBurnSpeed(speedX);
                        burner.setProgressCallback([this](const cdmanager::infrastructure::burn::BurnProgress& progress) {
                            const int percent = progress.overallPercent > 0.f
                                ? static_cast<int>(progress.overallPercent)
                                : 50;
                            const QString label = percent < 100
                                ? QStringLiteral("正在刻录，请勿触碰光驱、连接线、电源按钮")
                                : burnProgressPhaseLabel(progress.phase);
                            m_burnWidget->onBurnProgress(percent, label);
                            statusBar()->showMessage(label);
                            QCoreApplication::processEvents();
                        });
                        burnResult = burner.burn(
                            resolvedDevicePath,
                            packAssembly.packs,
                            wavFiles
                        );
                        if (!burnResult.diagnostics.isEmpty()) {
                            m_lastBurnDiagnostics += QStringLiteral("\n\nBackend diagnostics:\n%1")
                                .arg(burnResult.diagnostics.trimmed());
                        }
                    } else if (useCdrdaoBackend()) {
                        const QString cdrdaoDeviceSpec =
                            cdmanager::infrastructure::burn::CdrdaoBurner::deviceSpecFor(
                                activeDriveId,
                                resolvedDevicePath
                            );
                        auto guard = QPointer<MainWindow>(this);
                        auto future = QtConcurrent::run(
                            [guard,
                             cdrdaoDeviceSpec,
                             resolvedDevicePath,
                             tocPath,
                             simulation,
                             speedX,
                             allowOverburn = preparedBurnProject.allowOverburn,
                             skipUnmountPreflight = (m_initialImportResult.status
                                 == cdmanager::application::import::DiscImportStatus::BlankWritableMedia),
                             tempDir]() -> cdmanager::infrastructure::burn::BurnResult {
                                Q_UNUSED(tempDir)
                                QString preflightDiagnostics = unmountDiskForExclusiveBurn(
                                    resolvedDevicePath,
                                    skipUnmountPreflight
                                );
                                cdmanager::infrastructure::burn::CdrdaoBurner burner;
                                burner.setSimulationMode(simulation);
                                burner.setBurnSpeed(speedX);
                                burner.setAllowOverburn(allowOverburn);
                                burner.setProgressCallback([guard](const cdmanager::infrastructure::burn::BurnProgress& progress) {
                                    if (!guard) {
                                        return;
                                    }
                                    QMetaObject::invokeMethod(
                                        guard,
                                        [guard, progress]() {
                                            if (!guard) {
                                                return;
                                            }
                                            QString label;
                                            if (progress.phase.contains(QStringLiteral("Writing track"), Qt::CaseInsensitive)) {
                                                const QRegularExpression re(QStringLiteral(R"(Writing track\s+(\d+))"),
                                                                            QRegularExpression::CaseInsensitiveOption);
                                                const auto match = re.match(progress.phase);
                                                if (match.hasMatch()) {
                                                    label = trackBurnStatusText(match.captured(1).toInt(),
                                                                                qMax(guard->m_activeBurnDurationsSeconds.size(), 1));
                                                }
                                            } else if (progress.phase.contains(QStringLiteral("Closing"), Qt::CaseInsensitive)) {
                                                label = QStringLiteral("正在进行终结处理");
                                            } else {
                                                label = QStringLiteral("正在刻录，请勿触碰光驱、连接线、电源按钮");
                                            }
                                            const int percent = progress.overallPercent > 0.f
                                                ? static_cast<int>(progress.overallPercent)
                                                : 50;
                                            guard->m_burnWidget->onBurnProgress(percent, label);
                                            guard->statusBar()->showMessage(label);
                                        },
                                        Qt::QueuedConnection
                                    );
                                });
                                auto result = burner.burn(cdrdaoDeviceSpec, tocPath);
                                result.diagnostics = QStringLiteral("Preflight:\n%1\n\n%2")
                                    .arg(preflightDiagnostics.trimmed(),
                                         result.diagnostics.trimmed());
                                return result;
                            }
                        );
                        auto* watcher = new QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>(this);
                        connect(
                            watcher,
                            &QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>::finished,
                            this,
                            [this, guard, watcher]() {
                                const auto asyncResult = watcher->result();
                                watcher->deleteLater();
                                if (!guard) {
                                    return;
                                }

                                stopEstimatedBurnProgress();
                                m_isExporting = false;
                                if (!asyncResult.diagnostics.isEmpty()) {
                                    m_lastBurnDiagnostics += QStringLiteral("\n\nBackend diagnostics:\n%1")
                                        .arg(asyncResult.diagnostics);
                                }
                                m_burnWidget->onBurnFinished(asyncResult.ok);
                                refreshProjectView();
                                statusBar()->showMessage(
                                    asyncResult.ok
                                        ? QStringLiteral("Burn complete.")
                                        : QStringLiteral("Burn error: %1").arg(asyncResult.error),
                                    10000
                                );
                            }
                        );
                        watcher->setFuture(future);
                        return;
                    } else
#endif
                    {
                        const int driveIdx = resolveDrutilDeviceIndex(activeDriveId);
                        m_burnWidget->onBurnProgress(35, trackBurnStatusText(1, qMax(wavFiles.size(), 1)));
                        statusBar()->showMessage(trackBurnStatusText(1, qMax(wavFiles.size(), 1)));
                        m_burnStartedAtMs = QDateTime::currentMSecsSinceEpoch();
                        if (m_burnProgressTimer != nullptr) {
                            m_burnProgressTimer->start();
                        }
                        QCoreApplication::processEvents();

                        auto guard = QPointer<MainWindow>(this);
                        auto future = QtConcurrent::run(
                            [driveIdx, tocPath, simulation, speedX, useStandardPregap, tempDir]() -> cdmanager::infrastructure::burn::BurnResult {
                                Q_UNUSED(tempDir)
                                cdmanager::infrastructure::burn::BurnResult asyncResult;
                                cdmanager::infrastructure::burn::DrutilBurner drutilBurner;
                                const auto drutilResult = drutilBurner.burn(
                                    driveIdx,
                                    tocPath,
                                    simulation,
                                    speedX,
                                    useStandardPregap
                                );
                                asyncResult.ok = drutilResult.ok;
                                asyncResult.error = drutilResult.error;
                                QString diagnostics;
                                diagnostics += QStringLiteral("Backend: drutil\n");
                                diagnostics += QStringLiteral("Drive index: %1\n").arg(driveIdx);
                                diagnostics += QStringLiteral("Speed: %1\n")
                                    .arg(speedX > 0 ? QStringLiteral("%1x").arg(speedX)
                                                    : QStringLiteral("max"));
                                diagnostics += QStringLiteral("TOC path: %1\n").arg(tocPath);
                                if (!drutilResult.stdOut.trimmed().isEmpty()) {
                                    diagnostics += QStringLiteral("\nstdout:\n%1\n").arg(drutilResult.stdOut.trimmed());
                                }
                                if (!drutilResult.stdErr.trimmed().isEmpty()) {
                                    diagnostics += QStringLiteral("\nstderr:\n%1\n").arg(drutilResult.stdErr.trimmed());
                                }
                                asyncResult.diagnostics = diagnostics.trimmed();
                                return asyncResult;
                            }
                        );
                        auto* watcher = new QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>(this);
                        connect(
                            watcher,
                            &QFutureWatcher<cdmanager::infrastructure::burn::BurnResult>::finished,
                            this,
                            [this, guard, watcher]() {
                                const auto asyncResult = watcher->result();
                                watcher->deleteLater();
                                if (!guard) {
                                    return;
                                }

                                stopEstimatedBurnProgress();
                                m_isExporting = false;
                                if (!asyncResult.diagnostics.isEmpty()) {
                                    m_lastBurnDiagnostics += QStringLiteral("\n\nBackend diagnostics:\n%1")
                                        .arg(asyncResult.diagnostics);
                                }
                                m_burnWidget->onBurnFinished(asyncResult.ok);
                                refreshProjectView();
                                statusBar()->showMessage(
                                    asyncResult.ok
                                        ? QStringLiteral("Burn complete.")
                                        : QStringLiteral("Burn error: %1").arg(asyncResult.error),
                                    10000
                                );
                            }
                        );
                        watcher->setFuture(future);
                        return;
                    }
                }

                m_isExporting = false;
                stopEstimatedBurnProgress();
                if (!burnResult.diagnostics.isEmpty()) {
                    m_lastBurnDiagnostics += QStringLiteral("\n\nBackend diagnostics:\n%1")
                        .arg(burnResult.diagnostics.trimmed());
                }
                m_burnWidget->onBurnFinished(burnResult.ok);
                refreshProjectView();
                statusBar()->showMessage(
                    burnResult.ok ? QStringLiteral("Burn complete.")
                                  : QStringLiteral("Burn error: %1").arg(burnResult.error),
                    10000);
            });

    connect(m_exportWidget, &cdmanager::presentation::editor::ExportWidget::exportRequested,
            this, [this](const QString& dir, const QVector<int>& trackNumbers) {
                m_isExporting = true;
                statusBar()->showMessage(QStringLiteral("Exporting..."));
                QCoreApplication::processEvents();

                // Build a filtered project containing only selected tracks.
                cdmanager::domain::project::CdProject exportProject = m_project;
                QVector<cdmanager::domain::project::Track> selectedTracks;
                for (int tn : trackNumbers) {
                    for (const auto& t : m_project.tracks) {
                        if (t.number == tn) {
                            selectedTracks.append(t);
                            break;
                        }
                    }
                }
                exportProject.tracks = selectedTracks;

                // Pre-compute all track locations on the main thread
                // (libcdio is not thread-safe, so locateTrack must stay here).
                const auto firstLoc = cdmanager::infrastructure::audio::AudioCdReader::locateTrack(
                    exportProject.tracks.isEmpty() ? 1 : exportProject.tracks.first().number
                );
                const QString devPath = firstLoc.devicePath.isEmpty()
                    ? cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath()
                    : firstLoc.devicePath;
                QHash<int, cdmanager::infrastructure::audio::CdTrackLocation> locations;
                for (const auto& track : exportProject.tracks) {
                    locations.insert(track.number,
                        cdmanager::infrastructure::audio::AudioCdReader::locateTrack(track.number));
                }

                auto* exportService = new cdmanager::application::ExportService();
                QObject::connect(exportService, &cdmanager::application::ExportService::finished,
                                 this, [exportService, this]() {
                    m_isExporting = false;
                    m_exportWidget->onExportFinished();
                    statusBar()->showMessage(QStringLiteral("Export complete."), 5000);
                    exportService->deleteLater();
                }, Qt::QueuedConnection);
                auto future = QtConcurrent::run([exportService, exportProject, devPath, dir, locations]() {
                    exportService->startExport(exportProject, devPath, dir, locations);
                });
                Q_UNUSED(future)
            });
}

void MainWindow::startEstimatedBurnProgress() {
    if (m_activeBurnDurationsSeconds.isEmpty()) {
        return;
    }

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_burnStartedAtMs;
    if (elapsedMs < 0) {
        return;
    }

    int totalAudioSeconds = 0;
    for (const int duration : m_activeBurnDurationsSeconds) {
        totalAudioSeconds += qMax(duration, 1);
    }
    const int effectiveSpeed = qMax(m_activeBurnSpeedX, 1);
    const int estimatedBurnSeconds = qMax(8, totalAudioSeconds / effectiveSpeed + 8);
    const double ratio = qBound(0.0, static_cast<double>(elapsedMs) / (estimatedBurnSeconds * 1000.0), 1.0);

    if (ratio >= 0.92) {
        m_burnWidget->onBurnProgress(95, QStringLiteral("正在进行终结处理"));
        statusBar()->showMessage(QStringLiteral("正在进行终结处理"));
        return;
    }

    const int trackCount = m_activeBurnDurationsSeconds.size();
    const int trackIndex = qMin(trackCount - 1, static_cast<int>((ratio / 0.92) * trackCount));
    const int progress = 35 + static_cast<int>(ratio * 60.0);
    const QString label = trackBurnStatusText(trackIndex + 1, trackCount);
    m_burnWidget->onBurnProgress(progress, label);
    statusBar()->showMessage(label);
}

void MainWindow::stopEstimatedBurnProgress() {
    if (m_burnProgressTimer != nullptr) {
        m_burnProgressTimer->stop();
    }
    m_burnStartedAtMs = 0;
    m_activeBurnDurationsSeconds.clear();
    m_activeBurnSpeedX = 16;
}

void MainWindow::refreshProjectView() {
    const auto overview = m_overviewBuilder.build(m_project);
    const auto drives = m_discImportService.availableDrives();
    if (!drives.isEmpty()) {
        const auto& drive = drives.first();
        m_driveStatusLabel->setText(
            uiText(u"来源：%1 | 模式：%2 | 读取：%3 | 写入：%4 | 介质：%5 | 导入：%6",
                   u"Source: %1 | Mode: %2 | Read: %3 | Write: %4 | Media: %5 | Import: %6")
                .arg(drive.displayName)
                .arg(currentSourceModeText())
                .arg(drive.canRead ? uiText(u"是", u"yes") : uiText(u"否", u"no"))
                .arg(drive.canWrite ? uiText(u"是", u"yes") : uiText(u"否", u"no"))
                .arg(drive.hasMediaLoaded ? uiText(u"已装载", u"loaded") : uiText(u"空", u"empty"))
                .arg(importStatusText())
        );
    } else {
        m_driveStatusLabel->setText(
            uiText(u"来源：没有可用光驱 | 模式：%1 | 导入：%2",
                   u"Source: no drive available | Mode: %1 | Import: %2")
                .arg(currentSourceModeText())
                .arg(importStatusText())
        );
    }
    m_buildFeaturesLabel->setText(buildFeaturesText());
    m_importSummaryLabel->setText(
        uiText(u"导入摘要：%1", u"Import Summary: %1").arg(
            m_initialImportResult.summary.isEmpty()
                ? uiText(u"无", u"n/a")
                : m_initialImportResult.summary
        )
    );
    if (m_initialImportResult.status == cdmanager::application::import::DiscImportStatus::BlankWritableMedia) {
        m_albumDetailsWidget->setAlbumTitle(uiText(u"空白可写光盘", u"Blank writable disc"));
        m_albumDetailsWidget->setAlbumArtist(uiText(u"可直接前往刻录页开始创建音频 CD", u"Ready for creating an audio CD"));
        m_trackCountLabel->setText(
            uiText(u"空白光盘已就绪 | 音频设备：%1", u"Blank writable disc ready | Audio device: %1")
                .arg(cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath().isEmpty()
                    ? uiText(u"无", u"none")
                    : cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath())
        );
    } else {
        m_albumDetailsWidget->setAlbumTitle(overview.albumTitle);
        m_albumDetailsWidget->setAlbumArtist(overview.albumArtist);
        m_trackCountLabel->setText(
            uiText(u"%1 | 音频设备：%2", u"%1 | Audio device: %2")
                .arg(overview.trackCountText)
                .arg(cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath().isEmpty()
                    ? uiText(u"无", u"none")
                    : cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath())
        );
    }
    m_trackTableWidget->setTracks(overview.trackRows);
    if (m_initialImportResult.status == cdmanager::application::import::DiscImportStatus::BlankWritableMedia) {
        m_playbackStatusLabel->setText(uiText(u"空白可写光盘已就绪，请前往刻录页。", u"Blank writable disc ready. Open the burn page."));
    }

    // Keep the playback service in sync with available tracks.
    QVector<int> trackNumbers;
    for (const auto& row : overview.trackRows) {
        trackNumbers.append(row.number);
    }
    m_playbackService.setTrackList(trackNumbers);

    const QString devPath = cdmanager::infrastructure::audio::AudioCdReader::defaultDevicePath();
    if (m_discAnalysisWidget != nullptr) {
        m_discAnalysisWidget->setCurrentDriveId(m_lastDeviceId);
        m_discAnalysisWidget->setReferenceProject(m_project);
    }
    m_exportWidget->setProject(m_project, devPath);
    const bool preserveBurnDraft =
        m_burnWidget->hasUserAuthoredContent() &&
        (m_initialImportResult.status == cdmanager::application::import::DiscImportStatus::BlankWritableMedia ||
         m_initialImportResult.status == cdmanager::application::import::DiscImportStatus::NoMediaLoaded);
    if (!preserveBurnDraft) {
        m_burnWidget->setProject(m_project, devPath);
    }

    const auto preparation = m_preparationService.prepare(m_project);
    const auto writePlan = m_cdTextWritePlanBuilder.build(preparation);
    const auto writePayload = m_cdTextWritePayloadBuilder.build(writePlan);
    m_cdTextPreviewWidget->setRows(m_cdTextPreviewBuilder.build(preparation));
    const auto report = m_validationService.validateCdText(m_project);
    int reusedFieldCount = 0;
    for (const auto& preparedField : preparation.preparedFields) {
        if (preparedField.reusedPreservedBytes) {
            ++reusedFieldCount;
        }
    }

    if (report.ok) {
        m_validationStatusLabel->setText(
            QStringLiteral("MS-JIS validation: pass, %1 field(s) prepared, %2 field(s) reusing imported bytes")
                .arg(preparation.preparedFields.size())
                .arg(reusedFieldCount)
        );
    } else {
        m_validationStatusLabel->setText(
            QStringLiteral("MS-JIS validation: %1 issue(s)").arg(report.issues.size())
        );
    }

    QString details = report.summary();
    if (!m_initialImportResult.message.isEmpty()) {
        details += QStringLiteral("\n\nImport message:\n%1").arg(m_initialImportResult.message);
    }
    if (!preparation.preparedFields.isEmpty()) {
        details += QStringLiteral("\n\nPrepared fields:\n");
        for (const auto& preparedField : preparation.preparedFields) {
            const QString normalizedHint = preparedField.normalizedToFullwidth
                ? QStringLiteral(" {normalized to fullwidth}")
                : QString();
            const QString effectiveValue = preparedField.effectiveValue.isEmpty()
                ? QStringLiteral("(empty)")
                : preparedField.effectiveValue;
            details += QStringLiteral("- %1: %2 byte(s)%3 [%4]%5 value=%6\n")
                .arg(preparedField.field.label)
                .arg(preparedField.encodedBytes.size())
                .arg(
                    preparedField.reusedPreservedBytes
                        ? QStringLiteral(" [reused imported bytes]")
                        : QString()
                )
                .arg(preparedField.sourceNote)
                .arg(normalizedHint)
                .arg(effectiveValue);
        }
    }
    if (!writePlan.entries.isEmpty()) {
        details += QStringLiteral(
            "\n\nWrite plan: writable=%1, skipped=%2\n"
        ).arg(writePlan.writableFieldCount()).arg(writePlan.skippedFieldCount());
        for (const auto& entry : writePlan.entries) {
            QString actionLabel;
            switch (entry.action) {
                case cdmanager::application::CdTextWriteAction::WriteEncodedBytes:
                    actionLabel = QStringLiteral("write");
                    break;
                case cdmanager::application::CdTextWriteAction::ReuseImportedBytes:
                    actionLabel = QStringLiteral("reuse");
                    break;
                case cdmanager::application::CdTextWriteAction::SkipMissing:
                    actionLabel = QStringLiteral("skip-missing");
                    break;
                case cdmanager::application::CdTextWriteAction::SkipEmpty:
                    actionLabel = QStringLiteral("skip-empty");
                    break;
            }

            details += QStringLiteral("- %1 -> %2 : %3\n")
                .arg(entry.preparedField.field.label)
                .arg(actionLabel)
                .arg(entry.reason);
        }
    }
    details += QStringLiteral(
        "\n\nWrite payload: album writable=%1, album skipped=%2, track groups=%3, writable bytes=%4\n"
    )
        .arg(writePayload.albumWritableFields.size())
        .arg(writePayload.albumSkippedFields.size())
        .arg(writePayload.tracks.size())
        .arg(writePayload.writableByteCount());
    for (const auto& field : writePayload.albumWritableFields) {
        details += QStringLiteral("- Album -> %1 : %2 byte(s) value=%3\n")
            .arg(field.preparedField.field.label)
            .arg(field.preparedField.encodedBytes.size())
            .arg(field.preparedField.effectiveValue.isEmpty()
                     ? QStringLiteral("(empty)")
                     : field.preparedField.effectiveValue);
    }
    for (const auto& field : writePayload.albumSkippedFields) {
        details += QStringLiteral("- Album -> %1 : skipped (%2)\n")
            .arg(field.preparedField.field.label)
            .arg(field.reason);
    }
    for (const auto& trackPayload : writePayload.tracks) {
        details += QStringLiteral("  Track %1: writable=%2, skipped=%3\n")
            .arg(trackPayload.trackNumber)
            .arg(trackPayload.writableFields.size())
            .arg(trackPayload.skippedFields.size());
        for (const auto& field : trackPayload.writableFields) {
            details += QStringLiteral("    - %1 : %2 byte(s) value=%3\n")
                .arg(field.preparedField.field.label)
                .arg(field.preparedField.encodedBytes.size())
                .arg(field.preparedField.effectiveValue.isEmpty()
                         ? QStringLiteral("(empty)")
                         : field.preparedField.effectiveValue);
        }
        for (const auto& field : trackPayload.skippedFields) {
            details += QStringLiteral("    - %1 : skipped (%2)\n")
                .arg(field.preparedField.field.label)
                .arg(field.reason);
        }
    }
    const auto packAssembly = m_cdTextPackAssembler.assemble(writePayload);
    details += QStringLiteral("\n\n%1\n").arg(packAssembly.diagnosticSummary());
    details += packAssembly.diagnosticDetail();
    if (!m_lastBurnDiagnostics.isEmpty()) {
        details += QStringLiteral("\n\nBurn diagnostics:\n%1").arg(m_lastBurnDiagnostics);
    }
    if (!m_initialImportResult.diagnostics.isEmpty()) {
        details += QStringLiteral("\n\nImport diagnostics:\n%1").arg(m_initialImportResult.diagnostics);
    }
    m_validationDetails->setPlainText(details.trimmed());
}

QString MainWindow::currentSourceModeText() const {
    switch (m_gateway->mode()) {
        case cdmanager::infrastructure::disc::GatewayMode::System:
            return uiText(u"系统", u"system");
        case cdmanager::infrastructure::disc::GatewayMode::Sample:
            return uiText(u"样本", u"sample");
    }
    return uiText(u"未知", u"unknown");
}

QString MainWindow::importStatusText() const {
    switch (m_initialImportResult.status) {
        case cdmanager::application::import::DiscImportStatus::Success:
            return uiText(u"成功", u"success");
        case cdmanager::application::import::DiscImportStatus::FallbackSample:
            return uiText(u"样本回退", u"sample-fallback");
        case cdmanager::application::import::DiscImportStatus::NoMediaLoaded:
            return uiText(u"无介质", u"no-media");
        case cdmanager::application::import::DiscImportStatus::BlankWritableMedia:
            return uiText(u"空白可写", u"blank-writable");
        case cdmanager::application::import::DiscImportStatus::DriveVisibleButReadNotImplemented:
            return uiText(u"系统占位", u"system-placeholder");
        case cdmanager::application::import::DiscImportStatus::NoDriveAvailable:
            return uiText(u"无光驱", u"no-drive");
    }
    return uiText(u"未知", u"unknown");
}

QString MainWindow::buildFeaturesText() const {
    return uiText(u"构建特性：libcdio=%1，libcdio-paranoia=%2，multimedia=on",
                  u"Build Features: libcdio=%1, libcdio-paranoia=%2, multimedia=on")
        .arg(
            cdmanager::infrastructure::build::kHasLibcdio
                ? uiText(u"开", u"on")
                : uiText(u"关", u"off")
        )
        .arg(
            cdmanager::infrastructure::build::kHasLibcdioParanoia
                ? uiText(u"开", u"on")
                : uiText(u"关", u"off")
        );
}

void MainWindow::applyCurrentTheme() {
    m_darkMode = cdmanager::presentation::ui::resolveDarkMode(m_themeMode);
    qApp->setPalette(cdmanager::presentation::ui::buildApplicationPalette(m_darkMode));
    qApp->setStyleSheet(cdmanager::presentation::ui::buildApplicationStyleSheet(m_darkMode));
}

void MainWindow::retranslateUi() {
    setWindowTitle(QStringLiteral("MultiLanguageCDManager"));
    if (m_tabWidget != nullptr) {
        m_tabWidget->setTabText(0, uiText(u"播放器", u"Player"));
        m_tabWidget->setTabText(1, uiText(u"控制台", u"Console"));
        m_tabWidget->setTabText(2, uiText(u"分析", u"Analyze"));
        m_tabWidget->setTabText(3, uiText(u"导出", u"Export"));
        m_tabWidget->setTabText(4, uiText(u"刻录", u"Burn"));
    }
    if (m_albumDetailsWidget != nullptr) {
        m_albumDetailsWidget->setLanguage(m_language);
    }
    if (m_trackTableWidget != nullptr) {
        m_trackTableWidget->setLanguage(m_language);
    }
    if (m_cdTextPreviewWidget != nullptr) {
        m_cdTextPreviewWidget->setLanguage(m_language);
    }
    if (m_discAnalysisWidget != nullptr) {
        m_discAnalysisWidget->setLanguage(m_language);
    }
    if (m_exportWidget != nullptr) {
        m_exportWidget->setLanguage(m_language);
    }
    if (m_burnWidget != nullptr) {
        m_burnWidget->setLanguage(m_language);
    }
    if (m_playPauseButton != nullptr && m_playbackService.state() == cdmanager::application::PlaybackState::Stopped) {
        m_playPauseButton->setText(uiText(u"播放", u"Play"));
    }
    if (m_stopButton != nullptr) {
        m_stopButton->setText(uiText(u"停止", u"Stop"));
    }
    if (m_prevButton != nullptr) {
        m_prevButton->setText(uiText(u"上一曲", u"Prev"));
    }
    if (m_nextButton != nullptr) {
        m_nextButton->setText(uiText(u"下一曲", u"Next"));
    }
    if (m_ejectButton != nullptr) {
        m_ejectButton->setText(uiText(u"弹出", u"Eject"));
    }
    if (m_playbackStatusLabel != nullptr &&
        m_playbackService.state() == cdmanager::application::PlaybackState::Stopped) {
        m_playbackStatusLabel->setText(uiText(u"当前没有播放中的音轨", u"No track playing"));
    }
    if (m_validationDetails != nullptr) {
        m_validationDetails->setPlaceholderText(uiText(
            u"CD-TEXT 校验与导入诊断会显示在这里。",
            u"CD-TEXT validation and import diagnostics appear here."
        ));
    }
}

QString MainWindow::uiText(QStringView chinese, QStringView english) const {
    return cdmanager::presentation::ui::text(m_language, chinese, english);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    applyCurrentTheme();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QMainWindow::closeEvent(event);
}

void MainWindow::onPlayPauseClicked() {
    switch (m_playbackService.state()) {
        case cdmanager::application::PlaybackState::Playing:
            m_playbackService.pause();
            break;
        case cdmanager::application::PlaybackState::Paused:
            m_playbackService.resume();
            break;
        case cdmanager::application::PlaybackState::Stopped:
            m_playbackService.playTrack(m_playbackService.currentTrackNumber());
            break;
    }
}

void MainWindow::onStopClicked() {
    m_playbackService.stop();
}

void MainWindow::onPlaybackStateChanged(cdmanager::application::PlaybackState state) {
    switch (state) {
        case cdmanager::application::PlaybackState::Playing:
            m_playPauseButton->setText(QStringLiteral("Pause"));
            m_playPauseButton->setEnabled(true);
            m_stopButton->setEnabled(true);
            m_positionSlider->setEnabled(true);
            break;
        case cdmanager::application::PlaybackState::Paused:
            m_playPauseButton->setText(QStringLiteral("Resume"));
            break;
        case cdmanager::application::PlaybackState::Stopped:
            m_playPauseButton->setText(QStringLiteral("Play"));
            m_stopButton->setEnabled(false);
            m_positionSlider->setEnabled(false);
            m_positionSlider->setValue(0);
            m_positionSlider->setRange(0, 0);
            m_playbackStatusLabel->setText(QStringLiteral("No track playing"));
            break;
    }
}

void MainWindow::onPlaybackPositionChanged(int elapsedSeconds, int totalSeconds) {
    const int elapsedMin = elapsedSeconds / 60;
    const int elapsedSec = elapsedSeconds % 60;
    const int totalMin = totalSeconds / 60;
    const int totalSec = totalSeconds % 60;
    m_playbackStatusLabel->setText(
        QStringLiteral("Track %1  |  %2:%3 / %4:%5")
            .arg(m_playbackService.currentTrackNumber())
            .arg(elapsedMin, 2, 10, QLatin1Char('0'))
            .arg(elapsedSec, 2, 10, QLatin1Char('0'))
            .arg(totalMin, 2, 10, QLatin1Char('0'))
            .arg(totalSec, 2, 10, QLatin1Char('0'))
    );

    // Update slider unless the user is actively dragging it.
    if (!m_isSeeking) {
        if (totalSeconds > 0 && m_positionSlider->maximum() != totalSeconds) {
            m_positionSlider->setRange(0, totalSeconds);
        }
        m_positionSlider->setValue(elapsedSeconds);
    }
}

void MainWindow::onPlaybackFinished() {
    m_playPauseButton->setText(QStringLiteral("Play"));
    m_stopButton->setEnabled(false);
    m_positionSlider->setEnabled(false);
    m_positionSlider->setValue(0);
    m_playbackStatusLabel->setText(
        QStringLiteral("Track %1 finished").arg(m_playbackService.currentTrackNumber())
    );
}

void MainWindow::refreshFromCurrentDriveState() {
    if (m_playbackService.state() == cdmanager::application::PlaybackState::Playing ||
        m_playbackService.state() == cdmanager::application::PlaybackState::Paused ||
        m_isExporting ||
        m_gateway->mode() == cdmanager::infrastructure::disc::GatewayMode::Sample ||
        m_mediaRefreshCoolingDown) {
        return;
    }

    m_mediaRefreshCoolingDown = true;
    if (m_mediaRefreshCooldownTimer != nullptr) {
        m_mediaRefreshCooldownTimer->start();
    }

    const auto drives = m_discImportService.availableDrives();
    if (drives.isEmpty()) {
        m_playbackService.stop();
        m_lastMediaStatusSignature.clear();
        m_project = cdmanager::domain::project::CdProject{};
        m_initialImportResult = {
            cdmanager::application::import::DiscImportStatus::NoDriveAvailable,
            QStringLiteral("未检测到可用光驱。"),
            QStringLiteral("media=no, audio-cd=no, tracks=0, cdtext=no, japanese-cdtext=no"),
            {},
            QString()
        };
        refreshProjectView();
        statusBar()->showMessage(QStringLiteral("光驱已移除或当前不可用。"), 3000);
        return;
    }

    m_lastDeviceId = drives.first().deviceId;
    statusBar()->showMessage(QStringLiteral("正在读取光碟状态…"));

    const QString idx = m_lastDeviceId.mid(QStringLiteral("drutil-index://").size());
    const cdmanager::infrastructure::disc::DrutilCommandRunner runner;
    const auto statusResult = runner.run({QStringLiteral("-drive"), idx, QStringLiteral("status")});
    if (!statusResult.ok) {
        statusBar()->showMessage(QStringLiteral("读取光碟状态失败：无法取得设备反馈。"), 4000);
        return;
    }

    const QString currentSignature = mediaStatusSignature(statusResult.stdOut);
    if (!currentSignature.isEmpty() && currentSignature == m_lastMediaStatusSignature) {
        return;
    }
    m_lastMediaStatusSignature = currentSignature;

    m_playbackService.stop();
    statusBar()->showMessage(QStringLiteral("正在导入光碟目录与文本信息…"));
    m_initialImportResult = m_discImportService.initialImport();
    m_project = m_initialImportResult.project;
    m_lastTrackCount = m_project.tracks.size();
    refreshProjectView();
    m_pendingAutoDiscAnalysis = false;

    switch (m_initialImportResult.status) {
        case cdmanager::application::import::DiscImportStatus::Success:
            statusBar()->showMessage(QStringLiteral("已读取光碟内容，正在刷新媒体资料…"), 3000);
            m_pendingAutoDiscAnalysis = true;
            break;
        case cdmanager::application::import::DiscImportStatus::NoMediaLoaded:
            statusBar()->showMessage(QStringLiteral("光碟已弹出。"), 3000);
            break;
        case cdmanager::application::import::DiscImportStatus::BlankWritableMedia:
            statusBar()->showMessage(QStringLiteral("已装入空白可写光碟。"), 3000);
            break;
        default:
            statusBar()->showMessage(m_initialImportResult.message, 3000);
            break;
    }

    if (m_pendingAutoDiscAnalysis && m_discAnalysisWidget != nullptr) {
        m_pendingAutoDiscAnalysis = false;
        m_discAnalysisWidget->analyzeCurrentDisc();
    }
}

}  // namespace cdmanager::presentation::mainwindow
