#include "cdmanager/presentation/editor/ExportWidget.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDebug>

namespace cdmanager::presentation::editor {

ExportWidget::ExportWidget(QWidget* parent)
    : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_headerLabel = new QLabel(this);
    QFont f = m_headerLabel->font();
    f.setBold(true);
    m_headerLabel->setFont(f);
    root->addWidget(m_headerLabel);

    // Track list with checkboxes.
    m_trackTree = new QTreeWidget(this);
    m_trackTree->setHeaderLabels({QStringLiteral(""), QStringLiteral("Track"),
                                  QStringLiteral("Title"), QStringLiteral("Artist"),
                                  QStringLiteral("Duration")});
    m_trackTree->setRootIsDecorated(false);
    m_trackTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_trackTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_trackTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_trackTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_trackTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    root->addWidget(m_trackTree, 1);

    // Select / deselect buttons.
    auto* selLayout = new QHBoxLayout();
    m_selectAllButton = new QPushButton(this);
    m_deselectAllButton = new QPushButton(this);
    selLayout->addWidget(m_selectAllButton);
    selLayout->addWidget(m_deselectAllButton);
    selLayout->addStretch();
    root->addLayout(selLayout);

    connect(m_selectAllButton, &QPushButton::clicked, this, &ExportWidget::selectAll);
    connect(m_deselectAllButton, &QPushButton::clicked, this, &ExportWidget::deselectAll);

    // Output directory.
    auto* dirLayout = new QHBoxLayout();
    m_outputLabel = new QLabel(this);
    dirLayout->addWidget(m_outputLabel);
    m_dirEdit = new QLineEdit(QDir::homePath(), this);
    m_dirEdit->setMinimumWidth(300);
    dirLayout->addWidget(m_dirEdit, 1);
    m_browseButton = new QPushButton(this);
    dirLayout->addWidget(m_browseButton);
    root->addLayout(dirLayout);

    connect(m_browseButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            cdmanager::presentation::ui::text(m_language, u"选择导出文件夹", u"Select export folder"),
            m_dirEdit->text(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        if (!dir.isEmpty()) {
            m_dirEdit->setText(dir);
        }
    });

    // Export button + status.
    auto* bottomLayout = new QHBoxLayout();
    m_exportButton = new QPushButton(this);
    m_statusLabel = new QLabel(this);
    bottomLayout->addWidget(m_exportButton);
    bottomLayout->addWidget(m_statusLabel, 1);
    root->addLayout(bottomLayout);

    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
        QVector<int> selected;
        for (int i = 0; i < m_trackTree->topLevelItemCount(); ++i) {
            auto* item = m_trackTree->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) {
                bool ok = false;
                const int tn = item->text(1).toInt(&ok);
                if (ok) selected.append(tn);
            }
        }
        if (selected.isEmpty()) {
            m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"还没有选中音轨。", u"No tracks selected."));
            return;
        }
        m_exportButton->setEnabled(false);
        m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"正在导出…", u"Exporting..."));
        Q_EMIT exportRequested(m_dirEdit->text(), selected);
    });

    retranslateUi();
}

void ExportWidget::setProject(const cdmanager::domain::project::CdProject& project,
                               const QString& devicePath) {
    m_project = project;
    m_devicePath = devicePath;

    qDebug() << "ExportWidget::setProject: tracks =" << project.tracks.size()
             << "devicePath =" << devicePath;

    m_trackTree->clear();
    for (const auto& track : project.tracks) {
        auto* item = new QTreeWidgetItem();
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setText(1, QString::number(track.number));
        item->setText(2, track.title);
        item->setText(3, track.artist);
        const int min = track.durationSeconds / 60;
        const int sec = track.durationSeconds % 60;
        item->setText(4, QStringLiteral("%1:%2")
            .arg(min).arg(sec, 2, 10, QLatin1Char('0')));
        m_trackTree->addTopLevelItem(item);
    }
}

void ExportWidget::setLanguage(cdmanager::presentation::ui::UiLanguage language) {
    m_language = language;
    retranslateUi();
}

void ExportWidget::selectAll() {
    for (int i = 0; i < m_trackTree->topLevelItemCount(); ++i) {
        m_trackTree->topLevelItem(i)->setCheckState(0, Qt::Checked);
    }
}

void ExportWidget::onExportFinished() {
    m_exportButton->setEnabled(true);
    m_statusLabel->setText(cdmanager::presentation::ui::text(m_language, u"导出完成。", u"Export complete."));
}

void ExportWidget::deselectAll() {
    for (int i = 0; i < m_trackTree->topLevelItemCount(); ++i) {
        m_trackTree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    }
}

void ExportWidget::retranslateUi() {
    using cdmanager::presentation::ui::text;

    m_headerLabel->setText(text(m_language, u"音频 CD 导出", u"Audio CD Export"));
    m_outputLabel->setText(text(m_language, u"输出位置：", u"Output:"));
    m_selectAllButton->setText(text(m_language, u"全选", u"Select All"));
    m_deselectAllButton->setText(text(m_language, u"取消全选", u"Deselect All"));
    m_browseButton->setText(text(m_language, u"浏览…", u"Browse..."));
    m_exportButton->setText(text(m_language, u"导出所选", u"Export Selected"));

    m_trackTree->setHeaderLabels({
        QString(),
        text(m_language, u"音轨", u"Track"),
        text(m_language, u"标题", u"Title"),
        text(m_language, u"艺术家", u"Artist"),
        text(m_language, u"时长", u"Duration"),
    });
}

}  // namespace cdmanager::presentation::editor
