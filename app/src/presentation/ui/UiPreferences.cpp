#include "cdmanager/presentation/ui/UiPreferences.h"

#include <QGuiApplication>
#include <QLocale>
#include <QStyleHints>

namespace cdmanager::presentation::ui {

namespace {

QString baseColors(bool darkMode) {
    if (darkMode) {
        return QStringLiteral(
            "* { color: #f5f7fb; font-size: 13px; }"
            "QMainWindow { background: transparent; }"
            "QWidget#mainSurface {"
              "  background: rgba(17, 19, 24, 0.72);"
            "  border: none;"
            "  border-radius: 22px;"
            "}"
            "QTabWidget::pane, QPlainTextEdit, QTreeWidget, QTableWidget, QLineEdit, QComboBox, QProgressBar {"
            "  background: rgba(8, 10, 14, 0.54);"
            "  border: 1px solid rgba(255, 255, 255, 0.12);"
            "  border-radius: 16px;"
            "}"
            "QTreeWidget::item, QTableWidget::item { background: transparent; }"
            "QTreeWidget::item:selected, QTableWidget::item:selected {"
            "  background: rgba(31, 111, 255, 0.28);"
            "  color: #ffffff;"
            "}"
            "QComboBox QAbstractItemView, QTreeWidget, QTableWidget, QPlainTextEdit {"
            "  background: rgba(16, 18, 24, 0.88);"
            "  selection-background-color: rgba(31, 111, 255, 0.28);"
            "  selection-color: #ffffff;"
            "}"
            "QHeaderView::section {"
            "  background: rgba(255, 255, 255, 0.09);"
            "  color: #d8deea;"
            "  border: none;"
            "  padding: 8px;"
            "}"
            "QTableCornerButton::section { background: rgba(255, 255, 255, 0.06); border: none; }"
            "QPushButton {"
            "  background: rgba(255, 255, 255, 0.11);"
            "  border: 1px solid rgba(255, 255, 255, 0.16);"
            "  border-radius: 12px;"
            "  padding: 8px 14px;"
            "}"
            "QPushButton:hover { background: rgba(255, 255, 255, 0.16); }"
            "QPushButton:pressed { background: rgba(255, 255, 255, 0.22); }"
            "QLabel { background: transparent; }"
            "QStatusBar { background: rgba(12, 14, 19, 0.55); color: #d8deea; }"
        );
    }

    return QStringLiteral(
        "* { color: #2a261d; font-size: 13px; }"
        "QMainWindow { background: transparent; }"
        "QWidget#mainSurface {"
        "  background: #e9e3c9;"
        "  border: none;"
        "  border-radius: 0px;"
        "}"
        "QTabWidget::pane {"
        "  background: #e9e3c9;"
        "  border: 1px solid #8e886e;"
        "  top: -1px;"
        "}"
        "QTabBar::tab {"
        "  background: #8f8f8f;"
        "  color: #ffffff;"
        "  border: 1px solid #6e6e6e;"
        "  border-bottom: none;"
        "  border-top-left-radius: 10px;"
        "  border-top-right-radius: 10px;"
        "  padding: 7px 18px;"
        "  margin-right: 1px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #0c68d6;"
        "  color: #ffffff;"
        "}"
        "QTabBar::tab:!selected:hover { background: #7c8ba0; }"
        "QPlainTextEdit, QTreeWidget, QTableWidget, QLineEdit, QComboBox, QProgressBar {"
        "  background: #fffdf5;"
        "  border: 1px solid #9c967c;"
        "  border-radius: 0px;"
        "}"
        "QTreeWidget::item, QTableWidget::item { background: transparent; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected {"
        "  background: #d5e7ff;"
        "  color: #24334f;"
        "}"
        "QComboBox QAbstractItemView, QTreeWidget, QTableWidget, QPlainTextEdit {"
        "  background: #fffdf5;"
        "  selection-background-color: #d5e7ff;"
        "  selection-color: #24334f;"
        "}"
        "QHeaderView::section {"
        "  background: #efe7ca;"
        "  color: #4d4637;"
        "  border: 1px solid #9c967c;"
        "  padding: 6px 8px;"
        "}"
        "QTableCornerButton::section { background: #efe7ca; border: 1px solid #9c967c; }"
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #ddd8c8);"
        "  border: 1px solid #8e886e;"
        "  border-radius: 10px;"
        "  padding: 7px 14px;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #d8edf8); }"
        "QPushButton:pressed { background: #c7d9f4; }"
        "QPushButton:disabled { color: #9b978c; background: #dfdbcf; }"
        "QLabel { background: transparent; }"
        "QStatusBar { background: #e9e3c9; color: #4d4637; border-top: 1px solid #9c967c; }"
    );
}

}  // namespace

UiLanguage detectInitialLanguage() {
    const QString localeName = QLocale::system().name().toLower();
    if (localeName.startsWith(QStringLiteral("zh"))) {
        return UiLanguage::Chinese;
    }
    return UiLanguage::English;
}

UiThemeMode detectInitialThemeMode() {
#ifdef Q_OS_MACOS
    return UiThemeMode::Light;
#else
    return UiThemeMode::Light;
#endif
}

bool resolveDarkMode(UiThemeMode mode) {
    if (mode == UiThemeMode::Dark) {
        return true;
    }
    if (mode == UiThemeMode::Light) {
        return false;
    }

    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString text(UiLanguage language, QStringView chinese, QStringView english) {
    return language == UiLanguage::Chinese ? chinese.toString() : english.toString();
}

QString buildApplicationStyleSheet(bool darkMode) {
    return baseColors(darkMode);
}

QPalette buildApplicationPalette(bool darkMode) {
    QPalette palette;
    if (darkMode) {
        palette.setColor(QPalette::Window, QColor(18, 22, 29));
        palette.setColor(QPalette::WindowText, QColor(245, 247, 251));
        palette.setColor(QPalette::Base, QColor(16, 18, 24));
        palette.setColor(QPalette::AlternateBase, QColor(24, 27, 35));
        palette.setColor(QPalette::Text, QColor(245, 247, 251));
        palette.setColor(QPalette::Button, QColor(36, 40, 50));
        palette.setColor(QPalette::ButtonText, QColor(245, 247, 251));
        palette.setColor(QPalette::Highlight, QColor(31, 111, 255));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::PlaceholderText, QColor(182, 192, 210));
    } else {
        palette.setColor(QPalette::Window, QColor(233, 227, 201));
        palette.setColor(QPalette::WindowText, QColor(42, 38, 29));
        palette.setColor(QPalette::Base, QColor(255, 253, 245));
        palette.setColor(QPalette::AlternateBase, QColor(239, 231, 202));
        palette.setColor(QPalette::Text, QColor(42, 38, 29));
        palette.setColor(QPalette::Button, QColor(246, 242, 229));
        palette.setColor(QPalette::ButtonText, QColor(42, 38, 29));
        palette.setColor(QPalette::Highlight, QColor(12, 104, 214));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::PlaceholderText, QColor(128, 118, 96));
    }
    return palette;
}

}  // namespace cdmanager::presentation::ui
