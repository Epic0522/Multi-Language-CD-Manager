#include "cdmanager/presentation/editor/TrackTableWidget.h"

#include <QHeaderView>
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
        m_table->setItem(row, 0, new QTableWidgetItem(QString::number(track.number)));
        m_table->setItem(row, 1, new QTableWidgetItem(track.title));
        m_table->setItem(row, 2, new QTableWidgetItem(track.artist));
        m_table->setItem(row, 3, new QTableWidgetItem(track.duration));
    }
}

void TrackTableWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
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
