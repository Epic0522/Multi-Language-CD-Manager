#pragma once

#include <QWidget>

#include "cdmanager/application/ProjectOverview.h"
#include "cdmanager/presentation/ui/UiPreferences.h"

class QTableWidget;

namespace cdmanager::presentation::editor {

class TrackTableWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TrackTableWidget(QWidget* parent = nullptr);

    void setTracks(const QVector<cdmanager::application::TrackOverviewRow>& tracks);
    void setLanguage(cdmanager::presentation::ui::UiLanguage language);

signals:
    void trackDoubleClicked(int trackNumber);

private:
    void retranslateUi();

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    QTableWidget* m_table {nullptr};
};

}  // namespace cdmanager::presentation::editor
