#include "cdmanager/presentation/ui/UiPreferences.h"

#include <QGuiApplication>
#include <QLocale>
#include <QStyleHints>

namespace cdmanager::presentation::ui {

namespace {

QString baseColors(bool darkMode) {
    if (darkMode) {
        return QStringLiteral(
            "* { color: #f5f7fb; }"
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
        "* { color: #18202d; }"
        "QMainWindow { background: transparent; }"
        "QWidget#mainSurface {"
            "  background: rgba(246, 248, 251, 0.82);"
        "  border: none;"
        "  border-radius: 22px;"
        "}"
        "QTabWidget::pane, QPlainTextEdit, QTreeWidget, QTableWidget, QLineEdit, QComboBox, QProgressBar {"
        "  background: rgba(255, 255, 255, 0.88);"
        "  border: 1px solid rgba(24, 32, 45, 0.12);"
        "  border-radius: 16px;"
        "}"
        "QTreeWidget::item, QTableWidget::item { background: transparent; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected {"
        "  background: rgba(31, 111, 255, 0.20);"
        "  color: #13233a;"
        "}"
        "QComboBox QAbstractItemView, QTreeWidget, QTableWidget, QPlainTextEdit {"
        "  background: rgba(255, 255, 255, 0.94);"
        "  selection-background-color: rgba(31, 111, 255, 0.20);"
        "  selection-color: #13233a;"
        "}"
        "QHeaderView::section {"
        "  background: rgba(24, 32, 45, 0.07);"
        "  color: #435068;"
        "  border: none;"
        "  padding: 8px;"
        "}"
        "QTableCornerButton::section { background: rgba(24, 32, 45, 0.04); border: none; }"
        "QPushButton {"
        "  background: rgba(255, 255, 255, 0.52);"
        "  border: 1px solid rgba(24, 32, 45, 0.10);"
        "  border-radius: 12px;"
        "  padding: 8px 14px;"
        "}"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.74); }"
        "QPushButton:pressed { background: rgba(235, 242, 255, 0.88); }"
        "QLabel { background: transparent; }"
        "QStatusBar { background: rgba(255, 255, 255, 0.52); color: #435068; }"
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
    return UiThemeMode::System;
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
        palette.setColor(QPalette::Window, QColor(245, 247, 251));
        palette.setColor(QPalette::WindowText, QColor(24, 32, 45));
        palette.setColor(QPalette::Base, QColor(255, 255, 255));
        palette.setColor(QPalette::AlternateBase, QColor(243, 246, 250));
        palette.setColor(QPalette::Text, QColor(24, 32, 45));
        palette.setColor(QPalette::Button, QColor(255, 255, 255));
        palette.setColor(QPalette::ButtonText, QColor(24, 32, 45));
        palette.setColor(QPalette::Highlight, QColor(31, 111, 255));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::PlaceholderText, QColor(106, 115, 135));
    }
    return palette;
}

}  // namespace cdmanager::presentation::ui
