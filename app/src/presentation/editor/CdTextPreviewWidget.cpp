#include "cdmanager/presentation/editor/CdTextPreviewWidget.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

namespace cdmanager::presentation::editor {

CdTextPreviewWidget::CdTextPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_table);
    retranslateUi();
}

void CdTextPreviewWidget::setRows(const QVector<cdmanager::application::CdTextPreviewRow>& rows) {
    m_table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto& item = rows.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(item.fieldLabel));
        m_table->setItem(row, 1, new QTableWidgetItem(item.languageLabel));
        m_table->setItem(row, 2, new QTableWidgetItem(item.byteCountLabel));
        m_table->setItem(row, 3, new QTableWidgetItem(item.sourceLabel));
        m_table->setItem(row, 4, new QTableWidgetItem(item.stateLabel));
        m_table->setItem(row, 5, new QTableWidgetItem(item.hexPreview));
    }
}

void CdTextPreviewWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
}

void CdTextPreviewWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_table->setHorizontalHeaderLabels({
        text(m_language, u"字段", u"Field"),
        text(m_language, u"语言", u"Language"),
        text(m_language, u"字节", u"Bytes"),
        text(m_language, u"来源", u"Source"),
        text(m_language, u"状态", u"State"),
        text(m_language, u"十六进制预览", u"Hex Preview"),
    });
}

}  // namespace cdmanager::presentation::editor
