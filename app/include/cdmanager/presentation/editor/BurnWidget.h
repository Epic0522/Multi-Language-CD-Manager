#pragma once

#include <QWidget>

#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/presentation/ui/UiPreferences.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace cdmanager::presentation::editor {

class BurnWidget final : public QWidget {
    Q_OBJECT

public:
    explicit BurnWidget(QWidget* parent = nullptr);

    void setProject(const cdmanager::domain::project::CdProject& project,
                    const QString& devicePath);
    cdmanager::domain::project::CdProject currentProject() const;
    bool hasUserAuthoredContent() const;
    void setLanguage(cdmanager::presentation::ui::UiLanguage language);

signals:
    void burnRequested(const cdmanager::domain::project::CdProject& project,
                       const QString& devicePath,
                       const QVector<int>& selectedTracks,
                       bool simulation,
                       int speedX);

public slots:
    void onBurnFinished(bool ok);
    void onBurnProgress(int percent, const QString& phase);

private:
    void collectEditedProject(cdmanager::domain::project::CdProject& out) const;
    void clearTrackList();
    void renumberTracks();
    void sortTracksBySourceFile();
    void updateCapacitySummary();
    void retranslateUi();
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    QLabel* m_headerLabel = nullptr;
    QLabel* m_formatHintLabel = nullptr;
    QLabel* m_albumTitleLabel = nullptr;
    QLabel* m_albumArtistLabel = nullptr;
    QLabel* m_gapLabel = nullptr;
    QLabel* m_speedLabel = nullptr;
    QLabel* m_cdTextLabel = nullptr;
    QLineEdit* m_albumTitleEdit = nullptr;
    QLineEdit* m_albumArtistEdit = nullptr;
    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_simulationCheck = nullptr;
    QCheckBox* m_overburnCheck = nullptr;
    QComboBox* m_speedCombo = nullptr;
    QComboBox* m_gapCombo = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QPushButton* m_burnButton = nullptr;
    QPushButton* m_removeSelectedButton = nullptr;
    QPushButton* m_clearAllButton = nullptr;
    QPushButton* m_sortButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_capacityLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QString m_devicePath;
};

}  // namespace cdmanager::presentation::editor
