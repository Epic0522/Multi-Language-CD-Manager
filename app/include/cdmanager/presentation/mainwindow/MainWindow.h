#pragma once

#include <QMainWindow>
#include <QVector>

#include "cdmanager/application/ProjectOverviewBuilder.h"
#include "cdmanager/application/CdTextPreparationService.h"
#include "cdmanager/application/CdTextPreviewBuilder.h"
#include "cdmanager/application/CdTextWritePayloadBuilder.h"
#include "cdmanager/application/burn/CdTextPackAssembler.h"
#include "cdmanager/application/import/DiscImportResult.h"
#include "cdmanager/application/import/DiscImportService.h"
#include "cdmanager/application/ProjectValidationService.h"
#include "cdmanager/application/CdTextWritePlanBuilder.h"
#include "cdmanager/application/PlaybackService.h"
#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/presentation/ui/UiPreferences.h"
#include <memory>

#include "cdmanager/infrastructure/disc/DiscDeviceGateway.h"
#include "cdmanager/infrastructure/disc/DiscKeepAlive.h"
#include "cdmanager/infrastructure/disc/DiscMediaMonitor.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QCloseEvent;
class QShowEvent;

namespace cdmanager::presentation::editor {
class AlbumDetailsWidget;
class BurnWidget;
class CdTextPreviewWidget;
class ExportWidget;
class TrackTableWidget;
}

namespace cdmanager::presentation::mainwindow {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void applyCurrentTheme();
    void retranslateUi();
    QString uiText(QStringView chinese, QStringView english) const;
    void refreshProjectView();
    QString currentSourceModeText() const;
    QString importStatusText() const;
    QString buildFeaturesText() const;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    cdmanager::domain::project::CdProject m_project;
    cdmanager::application::import::DiscImportResult m_initialImportResult;
    std::unique_ptr<cdmanager::infrastructure::disc::DiscDeviceGateway> m_gateway;
    cdmanager::application::import::DiscImportService m_discImportService;
    cdmanager::application::ProjectOverviewBuilder m_overviewBuilder;
    cdmanager::application::CdTextPreparationService m_preparationService;
    cdmanager::application::CdTextPreviewBuilder m_cdTextPreviewBuilder;
    cdmanager::application::CdTextWritePlanBuilder m_cdTextWritePlanBuilder;
    cdmanager::application::CdTextWritePayloadBuilder m_cdTextWritePayloadBuilder;
    cdmanager::application::burn::CdTextPackAssembler m_cdTextPackAssembler;
    cdmanager::application::ProjectValidationService m_validationService;
    QLabel* m_driveStatusLabel {nullptr};
    QLabel* m_buildFeaturesLabel {nullptr};
    QLabel* m_importSummaryLabel {nullptr};
    QLabel* m_trackCountLabel {nullptr};
    QLabel* m_validationStatusLabel {nullptr};
    QWidget* m_centralSurfaceWidget {nullptr};
    QTabWidget* m_tabWidget {nullptr};
    cdmanager::presentation::editor::AlbumDetailsWidget* m_albumDetailsWidget {nullptr};
    cdmanager::presentation::editor::CdTextPreviewWidget* m_cdTextPreviewWidget {nullptr};
    cdmanager::presentation::editor::TrackTableWidget* m_trackTableWidget {nullptr};
    cdmanager::presentation::editor::ExportWidget* m_exportWidget {nullptr};
    cdmanager::presentation::editor::BurnWidget* m_burnWidget {nullptr};
    QPlainTextEdit* m_validationDetails {nullptr};

    // Playback
    cdmanager::application::PlaybackService m_playbackService;
    QSlider* m_positionSlider {nullptr};
    QPushButton* m_playPauseButton {nullptr};
    QPushButton* m_stopButton {nullptr};
    QPushButton* m_prevButton {nullptr};
    QPushButton* m_nextButton {nullptr};
    QPushButton* m_modeButton {nullptr};
    QPushButton* m_ejectButton {nullptr};
    QLabel* m_playbackStatusLabel {nullptr};

    cdmanager::infrastructure::disc::DiscKeepAlive m_discKeepAlive;
    cdmanager::infrastructure::disc::DiscMediaMonitor m_discMediaMonitor;
    QTimer* m_mediaRefreshDebounceTimer {nullptr};
    QTimer* m_mediaRefreshCooldownTimer {nullptr};
    bool m_mediaRefreshCoolingDown {false};
    int m_lastTrackCount = 0;
    QString m_lastDeviceId;
    QString m_lastMediaStatusSignature;
    QString m_lastBurnDiagnostics;
    QTimer* m_burnProgressTimer {nullptr};
    qint64 m_burnStartedAtMs {0};
    QVector<int> m_activeBurnDurationsSeconds;
    int m_activeBurnSpeedX {16};
    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    cdmanager::presentation::ui::UiThemeMode m_themeMode {
        cdmanager::presentation::ui::UiThemeMode::System
    };
    bool m_darkMode {false};
    void refreshFromCurrentDriveState();
    void startEstimatedBurnProgress();
    void stopEstimatedBurnProgress();

    bool m_isSeeking = false;
    bool m_isExporting = false;

private slots:
    void onPlayPauseClicked();
    void onStopClicked();
    void onPlaybackStateChanged(cdmanager::application::PlaybackState state);
    void onPlaybackPositionChanged(int elapsedSeconds, int totalSeconds);
    void onPlaybackFinished();
};

}  // namespace cdmanager::presentation::mainwindow
