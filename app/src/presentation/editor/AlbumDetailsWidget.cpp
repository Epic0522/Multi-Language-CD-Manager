#include "cdmanager/presentation/editor/AlbumDetailsWidget.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace cdmanager::presentation::editor {

AlbumDetailsWidget::AlbumDetailsWidget(QWidget* parent)
    : QWidget(parent) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(4);

    m_headingLabel = new QLabel(QStringLiteral("Album Details"), this);

    m_albumTitleCaptionLabel = new QLabel(this);
    m_albumTitleLabel = new QLabel(this);
    m_albumTitleLabel->setTextFormat(Qt::PlainText);
    m_albumTitleLabel->setMinimumWidth(400);
    m_albumTitleLabel->setWordWrap(true);

    m_albumArtistCaptionLabel = new QLabel(this);
    m_albumArtistLabel = new QLabel(this);
    m_albumArtistLabel->setTextFormat(Qt::PlainText);
    m_albumArtistLabel->setMinimumWidth(400);
    m_albumArtistLabel->setWordWrap(true);

    auto* formLayout = new QFormLayout();
    formLayout->addRow(m_albumTitleCaptionLabel, m_albumTitleLabel);
    formLayout->addRow(m_albumArtistCaptionLabel, m_albumArtistLabel);

    rootLayout->addWidget(m_headingLabel);
    rootLayout->addLayout(formLayout);

    retranslateUi();
}

void AlbumDetailsWidget::setAlbumTitle(const QString& title) {
    m_albumTitleLabel->setText(title.isEmpty() ? QStringLiteral("—") : title);
}

void AlbumDetailsWidget::setAlbumArtist(const QString& artist) {
    m_albumArtistLabel->setText(artist.isEmpty() ? QStringLiteral("—") : artist);
}

void AlbumDetailsWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
}

void AlbumDetailsWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_headingLabel->setText(text(m_language, u"专辑信息", u"Album Details"));
    m_albumTitleCaptionLabel->setText(text(m_language, u"专辑名", u"Album Title"));
    m_albumArtistCaptionLabel->setText(text(m_language, u"专辑艺术家", u"Album Artist"));
}

}  // namespace cdmanager::presentation::editor
