#include "cdmanager/presentation/editor/TrackTableWidget.h"

#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QTableWidget>
#include <QVBoxLayout>

namespace cdmanager::presentation::editor {

TrackTableWidget::TrackTableWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_table);
    retranslateUi();

    connect(m_table, &QTableWidget::cellDoubleClicked, this,
        [this](int row, int /*column*/) {
            if (row < 0 || row >= m_table->rowCount()) {
                return;
            }
            const auto* item = m_table->item(row, 0);
            if (item == nullptr) {
                return;
            }
            bool ok = false;
            const int trackNumber = item->text().toInt(&ok);
            if (ok) {
                Q_EMIT trackDoubleClicked(trackNumber);
            }
        });
}

void TrackTableWidget::setTracks(const QVector<cdmanager::application::TrackOverviewRow>& tracks) {
    m_table->setRowCount(tracks.size());
    for (int row = 0; row < tracks.size(); ++row) {
        const auto& track = tracks.at(row);
        auto* numberItem = new QTableWidgetItem(QString::number(track.number));
        auto* titleItem = new QTableWidgetItem(track.title);
        auto* artistItem = new QTableWidgetItem(track.artist);
        auto* durationItem = new QTableWidgetItem(track.duration);
        m_table->setItem(row, 0, numberItem);
        m_table->setItem(row, 1, titleItem);
        m_table->setItem(row, 2, artistItem);
        m_table->setItem(row, 3, durationItem);

        const bool suspicious = m_suspiciousTracks.contains(track.number);
        const QColor bg = suspicious ? QColor(255, 232, 239) : QColor(Qt::transparent);
        const QColor fg = suspicious ? QColor(131, 34, 67) : m_table->palette().text().color();
        for (auto* item : {numberItem, titleItem, artistItem, durationItem}) {
            item->setBackground(QBrush(bg));
            item->setForeground(QBrush(fg));
            if (suspicious) {
                item->setToolTip(
                    cdmanager::presentation::ui::text(
                        m_language,
                        u"该音轨位于疑似异常区段，继续播放可能导致光驱反复寻道或卡住。",
                        u"This track lies in a suspected abnormal region and may cause repeated seek retries."
                    )
                );
            }
        }
    }
}

void TrackTableWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
}

void TrackTableWidget::setSuspiciousTracks(const QSet<int>& trackNumbers) {
    m_suspiciousTracks = trackNumbers;
}

void TrackTableWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_table->setHorizontalHeaderLabels({
        text(m_language, u"音轨", u"Track"),
        text(m_language, u"标题", u"Title"),
        text(m_language, u"艺术家", u"Artist"),
        text(m_language, u"时长", u"Duration"),
    });
}

}  // namespace cdmanager::presentation::editor
