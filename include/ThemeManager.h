#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QString>
#include <QColor>

class ThemeManager {
public:
    // Color System
    static QString bgPrimary() { return "rgba(255, 255, 255, 0.6)"; }
    static QString bgSecondary() { return "rgba(255, 255, 255, 0.4)"; }
    static QString border() { return "rgba(255, 255, 255, 0.8)"; }
    static QString textPrimary() { return "rgba(0, 0, 0, 0.85)"; }
    static QString textSecondary() { return "rgba(0, 0, 0, 0.6)"; }

    // Spacing
    static int spaceXs() { return 4; }
    static int spaceSm() { return 8; }
    static int spaceMd() { return 12; }
    static int spaceLg() { return 16; }
    static int spaceXl() { return 24; }

    // Border Radius
    static int radiusSmall() { return 10; }
    static int radiusMedium() { return 14; }
    static int radiusLarge() { return 20; }
    static int radiusWindow() { return 24; }

    // Typography
    static QString fontFamily() { return "Segoe UI Variable"; }
    static int fontSizeNormal() { return 13; }
    static int fontSizeTitle() { return 18; }
};

#endif // THEMEMANAGER_H
