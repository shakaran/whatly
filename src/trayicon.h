#ifndef TRAYICON_H
#define TRAYICON_H

#include <QImage>
#include <QString>

// Helpers for building the tray icon. Kept as a pure, GUI-free unit (plain
// values in, QImage out) so the composition logic can be unit-tested headless,
// away from MainWindow and the live QSystemTrayIcon.
namespace TrayIcon {

// True when every pixel of the image is fully transparent (or the image is
// null) — i.e. it carries no visible glyph.
bool isFullyTransparent(const QImage &img);

// Render the monochrome tray glyph as an alpha mask (opaque where the glyph is)
// at the given square size. Prefers the vector source; if the SVG renderer
// yields nothing on some setups — a missing Qt Svg runtime, or an unrenderable
// file — it falls back to the colour PNG's shape so the monochrome icon is never
// blank (issue #14). Returns a transparent image only when both sources fail,
// letting the caller degrade to the colour icon.
QImage monochromeGlyphMask(const QString &svgPath, const QString &fallbackPngPath,
                           int size);

// What the badge says for a given count: nothing at zero, the number itself up to
// ninety-nine, and "99+" beyond it. Not a cap on the count but on the digits: the
// badge is about a third of an icon that a panel draws at some 22 px, so a third
// digit is three pixels wide and says less than the "+" does. The real number is
// on the tray icon's tooltip instead.
QString badgeText(int count);

// Compose the full tray image for a given state: unread count (any count), whether
// the monochrome icon is requested, and whether the app is connected. The count
// applies in every state — including 0 — so an idle tray honours the monochrome
// choice just like a busy one (the #14 regression: the idle path used to bypass
// this and always show the colour icon). Falls back to the colour icon if the
// monochrome glyph cannot be rendered.
QImage composeTrayImage(int notificationCount, bool monochrome, bool connected,
                        int size);

} // namespace TrayIcon

#endif // TRAYICON_H
