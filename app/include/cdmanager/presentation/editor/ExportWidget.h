#pragma once

#include <QWidget>

#include "cdmanager/domain/project/CdProject.h"
#include "cdmanager/presentation/ui/UiPreferences.h"

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace cdmanager::presentation::editor {

class ExportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ExportWidget(QWidget* parent = nullptr);

    void setProject(const cdmanager::domain::project::CdProject& project,
                    const QString& devicePath);
    void setLanguage(cdmanager::presentation::ui::UiLanguage language);

public slots:
    void onExportFinished();

signals:
    void exportRequested(const QString& outputDir,
                         const QVector<int>& selectedTrackNumbers);

private:
    void selectAll();
    void deselectAll();
    void retranslateUi();

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    QTreeWidget* m_trackTree = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_selectAllButton = nullptr;
    QPushButton* m_deselectAllButton = nullptr;
    QPushButton* m_browseButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_headerLabel = nullptr;
    QLabel* m_outputLabel = nullptr;
    cdmanager::domain::project::CdProject m_project;
    QString m_devicePath;
};

}  // namespace cdmanager::presentation::editor
