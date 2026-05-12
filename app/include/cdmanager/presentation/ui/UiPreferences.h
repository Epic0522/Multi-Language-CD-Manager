#pragma once

#include <QPalette>
#include <QString>
#include <QStringView>

namespace cdmanager::presentation::ui {

enum class UiLanguage {
    Chinese,
    English,
};

enum class UiThemeMode {
    System,
    Light,
    Dark,
};

UiLanguage detectInitialLanguage();
UiThemeMode detectInitialThemeMode();
bool resolveDarkMode(UiThemeMode mode);

QString text(UiLanguage language, QStringView chinese, QStringView english);
QString buildApplicationStyleSheet(bool darkMode);
QPalette buildApplicationPalette(bool darkMode);

}  // namespace cdmanager::presentation::ui
