#pragma once

#include <QWidget>

#include "cdmanager/application/CdTextPreviewBuilder.h"
#include "cdmanager/presentation/ui/UiPreferences.h"

class QTableWidget;

namespace cdmanager::presentation::editor {

class CdTextPreviewWidget final : public QWidget {
public:
    explicit CdTextPreviewWidget(QWidget* parent = nullptr);

    void setRows(const QVector<cdmanager::application::CdTextPreviewRow>& rows);
    void setLanguage(cdmanager::presentation::ui::UiLanguage language);

private:
    void retranslateUi();

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    QTableWidget* m_table {nullptr};
};

}  // namespace cdmanager::presentation::editor
