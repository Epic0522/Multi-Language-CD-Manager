#pragma once

#include <QWidget>

#include <QFutureWatcher>

#include "cdmanager/application/import/DiscAnalysisService.h"
#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/presentation/ui/UiPreferences.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QWidget;

namespace cdmanager::presentation::editor {

class DiscAnalysisWidget final : public QWidget {
    Q_OBJECT

public:
    explicit DiscAnalysisWidget(QWidget* parent = nullptr);

    void setLanguage(cdmanager::presentation::ui::UiLanguage language);
    void setCurrentDriveId(const QString& driveId);
    void setReferenceProject(const cdmanager::domain::project::CdProject& project);
    void analyzeCurrentDisc(
        cdmanager::application::import::DiscAnalysisDepth depth =
            cdmanager::application::import::DiscAnalysisDepth::Deep
    );
    const cdmanager::application::import::DiscAnalysisResult& lastResult() const;

signals:
    void analysisStarted();
    void analysisFinished(bool healthy, bool performedDeepAnalysis);

private:
    void retranslateUi();
    void applyResult(const cdmanager::application::import::DiscAnalysisResult& result);

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    cdmanager::application::import::DiscAnalysisService m_service;
    QFutureWatcher<cdmanager::application::import::DiscAnalysisResult> m_analysisWatcher;
    QString m_currentDriveId;
    cdmanager::application::import::DiscAnalysisResult m_lastResult;
    cdmanager::domain::project::CdProject m_referenceProject;

    QLabel* m_headerLabel {nullptr};
    QLabel* m_hintLabel {nullptr};
    QPushButton* m_analyzeButton {nullptr};
    QPushButton* m_exportJsonButton {nullptr};
    QLabel* m_statusLabel {nullptr};
    QLabel* m_summaryLabel {nullptr};
    QLabel* m_discTitleKeyLabel {nullptr};
    QLabel* m_discTitleValueLabel {nullptr};
    QLabel* m_discPerformerKeyLabel {nullptr};
    QLabel* m_discPerformerValueLabel {nullptr};
    QLabel* m_driveKeyLabel {nullptr};
    QLabel* m_driveValueLabel {nullptr};
    QLabel* m_mediumKeyLabel {nullptr};
    QLabel* m_mediumValueLabel {nullptr};
    QLabel* m_trackCountKeyLabel {nullptr};
    QLabel* m_trackCountValueLabel {nullptr};
    QLabel* m_runtimeKeyLabel {nullptr};
    QLabel* m_runtimeValueLabel {nullptr};
    QLabel* m_cdTextKeyLabel {nullptr};
    QLabel* m_cdTextValueLabel {nullptr};
    QLabel* m_verdictLabel {nullptr};
    QLabel* m_usageCaptionLabel {nullptr};
    QLabel* m_usageTextLabel {nullptr};
    QLabel* m_freeTextLabel {nullptr};
    QWidget* m_usageRingWidget {nullptr};
    QTreeWidget* m_trackTree {nullptr};
    QPlainTextEdit* m_detailsEdit {nullptr};
};

}  // namespace cdmanager::presentation::editor
