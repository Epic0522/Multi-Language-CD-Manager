#pragma once

#include <QWidget>

#include "cdmanager/presentation/ui/UiPreferences.h"

class QLabel;

namespace cdmanager::presentation::editor {

class AlbumDetailsWidget final : public QWidget {
    Q_OBJECT

public:
    explicit AlbumDetailsWidget(QWidget* parent = nullptr);

    void setAlbumTitle(const QString& title);
    void setAlbumArtist(const QString& artist);
    void setLanguage(cdmanager::presentation::ui::UiLanguage language);

signals:
    // Reserved for future CD-TEXT editing (burn mode).
    void albumTitleChanged(const QString& title);
    void albumArtistChanged(const QString& artist);

private:
    void retranslateUi();

    cdmanager::presentation::ui::UiLanguage m_language {
        cdmanager::presentation::ui::UiLanguage::Chinese
    };
    QLabel* m_headingLabel {nullptr};
    QLabel* m_albumTitleCaptionLabel {nullptr};
    QLabel* m_albumArtistCaptionLabel {nullptr};
    QLabel* m_albumTitleLabel {nullptr};
    QLabel* m_albumArtistLabel {nullptr};
};

}  // namespace cdmanager::presentation::editor
