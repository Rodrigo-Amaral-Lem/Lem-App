// Application-wide palette paired with the app-owned QML controls.
// Keep native window roles readable when the operating system uses dark mode.

#pragma once
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

// The forms have light card backgrounds. Pin all palette roles so an OS dark
// palette cannot supply white labels on those backgrounds.
inline void applyLightTheme() {
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QPalette p;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        p.setColor(group, QPalette::Window, QColor("#f4f6f7"));
        p.setColor(group, QPalette::Base, QColor("#ffffff"));
        p.setColor(group, QPalette::AlternateBase, QColor("#edf3f4"));
        p.setColor(group, QPalette::Button, QColor("#e6eef0"));
        p.setColor(group, QPalette::WindowText, QColor(disabled ? "#52616a" : "#263238"));
        p.setColor(group, QPalette::Text, QColor(disabled ? "#52616a" : "#263238"));
        p.setColor(group, QPalette::ButtonText, QColor(disabled ? "#52616a" : "#263238"));
        p.setColor(group, QPalette::PlaceholderText, QColor("#52616a"));
        p.setColor(group, QPalette::Highlight, QColor(disabled ? "#52616a" : "#007c91"));
        p.setColor(group, QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(group, QPalette::ToolTipBase, QColor("#ffffff"));
        p.setColor(group, QPalette::ToolTipText, QColor("#263238"));
        p.setColor(group, QPalette::Light, QColor("#ffffff"));
        p.setColor(group, QPalette::Midlight, QColor("#edf3f4"));
        p.setColor(group, QPalette::Mid, QColor("#71828b"));
        p.setColor(group, QPalette::Dark, QColor("#61757f"));
        p.setColor(group, QPalette::Shadow, QColor("#445761"));
        p.setColor(group, QPalette::Link, QColor("#007c91"));
        p.setColor(group, QPalette::LinkVisited, QColor("#514d82"));
        p.setColor(group, QPalette::Accent, QColor("#007c91"));
        p.setColor(group, QPalette::BrightText, QColor("#ffffff"));
    }
    QGuiApplication::setPalette(p);
}
