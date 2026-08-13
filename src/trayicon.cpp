#include "trayicon.h"

#include <algorithm>

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {
const QString kSvgPath = QStringLiteral(":/icons/app/whatly-symbolic.svg");
const QString kColourZero =
    QStringLiteral(":/icons/app/notification/whatly-notify.png");
QString colourPath(int count) {
  return count == 0
             ? kColourZero
             : QStringLiteral(":/icons/app/notification/whatly-notify-%1.png")
                   .arg(count);
}

// The badge, drawn rather than baked. Measured off whatly-notify-3.png so a drawn
// one sits where the artwork's does: flush with the bottom-right corner, 32x27 of
// a 64x64 icon, corners rounded by about 8. It widens for a second and third
// character instead of shrinking the text into the same box, because the panel
// scales the whole icon down by three and there is nothing to spare.
void paintCountBadge(QPainter &p, int size, const QString &text,
                     const QColor &fill, const QColor &ink, bool cutOut = false) {
  if (text.isEmpty() || size <= 0)
    return;
  const qreal k = size / 64.0;
  const int height = qRound(27 * k);
  const int radius = qRound(8 * k);
  const int padX = qRound(7 * k);

  QFont f = qApp->font();
  f.setBold(true);
  // One size down for three characters ("99+"), which is the only text that needs
  // it; two digits still get the full height.
  f.setPixelSize(qMax(1, qRound((text.size() >= 3 ? 15 : 19) * k)));
  const QFontMetrics fm(f);
  const int width = qMax(qRound(32 * k), fm.horizontalAdvance(text) + 2 * padX);
  const QRect badge(size - width, size - height, width, height);

  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  // Monochrome asks for a gap: the badge is the same light tone as the glyph it
  // sits on, so the two ran together into one bright blob and only the digits were
  // left to read — at panel size, three or four pixels of them. Clearing a slightly
  // larger rounded rect first lets the panel's own colour through as a ring, which
  // separates the badge from the glyph whatever colour that panel is. The colour
  // icon needs none of this: red on teal separates itself.
  if (cutOut) {
    const int grow = qMax(1, qRound(2 * k));
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.drawRoundedRect(badge.adjusted(-grow, -grow, grow, grow), radius + grow,
                      radius + grow);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  }
  p.setBrush(fill);
  p.drawRoundedRect(badge, radius, radius);
  p.setFont(f);
  p.setPen(ink);
  p.drawText(badge, Qt::AlignCenter, text);
}
} // namespace

namespace TrayIcon {

bool isFullyTransparent(const QImage &img) {
  if (img.isNull())
    return true;
  const QImage a =
      img.format() == QImage::Format_ARGB32_Premultiplied
          ? img
          : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < a.height(); ++y) {
    const QRgb *line = reinterpret_cast<const QRgb *>(a.constScanLine(y));
    for (int x = 0; x < a.width(); ++x)
      if (qAlpha(line[x]) != 0)
        return false;
  }
  return true;
}

QImage monochromeGlyphMask(const QString &svgPath, const QString &fallbackPngPath,
                           int size) {
  QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  {
    QSvgRenderer renderer(svgPath);
    if (renderer.isValid()) {
      QPainter rp(&img);
      renderer.render(&rp);
    }
  }
  if (!isFullyTransparent(img))
    return img;

  // The SVG produced nothing on this setup. Fall back to the colour icon's
  // shape: only its alpha channel matters, since the caller tints the mask.
  const QImage png(fallbackPngPath);
  if (!png.isNull())
    return png.convertToFormat(QImage::Format_ARGB32_Premultiplied)
        .scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  return img; // still empty — caller degrades to the colour icon
}

QString badgeText(int count) {
  if (count <= 0)
    return QString();
  return count > 99 ? QStringLiteral("99+") : QString::number(count);
}

QImage composeTrayImage(int notificationCount, bool monochrome, bool connected,
                        int size) {
  // No longer clamped to ten. The clamp was invisible while the count came from
  // the window title and under-reported wildly; with a real count of unread chats
  // the ceiling is reached and stayed at, so the badge stopped saying anything.
  const int count = qMax(0, notificationCount);

  QPixmap base(size, size);
  base.fill(Qt::transparent);

  // When monochrome is asked for, build the glyph mask up front. The helper
  // prefers the SVG but falls back to the colour icon's shape if the SVG renders
  // empty on some setup (issue #14); if even that yields nothing, drop out of
  // monochrome so the tray shows the colour icon instead of a blank slot.
  QImage monoMask;
  if (monochrome) {
    monoMask = monochromeGlyphMask(kSvgPath, kColourZero, size);
    if (isFullyTransparent(monoMask))
      monochrome = false;
  }

  if (monochrome) {
    // A monochrome tray icon is asked for so it stops being the one bright thing
    // in an otherwise grey tray — and those trays are almost always dark, with
    // the other icons light. So the glyph is tinted light rather than to the app
    // palette (which is the *window's* colour, not the panel's, and would be
    // dark and invisible under a light app theme on a dark panel). A faint dark
    // outline underneath keeps it legible on the rarer light panel too, kept
    // subtle so it is not noticeable on the common dark panel (issue #14).
    const QPixmap mask = QPixmap::fromImage(monoMask);

    auto tinted = [&](const QColor &c) {
      QPixmap px = mask;
      QPainter tp(&px);
      tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tp.fillRect(px.rect(), c);
      tp.end();
      return px;
    };

    const QPixmap dark = tinted(QColor(0, 0, 0, 70));       // faint outline halo
    const QPixmap light = tinted(QColor(0xea, 0xea, 0xea)); // main fill

    QPainter p(&base);
    for (int dx = -1; dx <= 1; ++dx)
      for (int dy = -1; dy <= 1; ++dy)
        if (dx || dy)
          p.drawPixmap(dx, dy, dark);
    p.drawPixmap(0, 0, light);
    p.end();
  } else {
    // The colourful icons carry a baked-in badge for one to nine. Past nine there
    // is no artwork for it — there used to be one file, whatly-notify-10.png, with
    // a bare "+" and no digit in it — so take the plain icon and draw the badge.
    QPixmap glyph(colourPath(count <= 9 ? count : 0));
    QPainter p(&base);
    p.drawPixmap(base.rect(), glyph);
  }

  // Draw the count: always in monochrome, where nothing is baked in, and past nine
  // in colour, where the artwork stops. One badge, one rule, two palettes — the two
  // modes used to disagree, and the colour one said less than the monochrome one.
  if (const QString text = badgeText(count);
      !text.isEmpty() && (monochrome || count > 9)) {
    QPainter p(&base);
    if (monochrome)
      // Grey rather than the glyph's own light tone: a badge in the same tone
      // merged with the glyph into one bright blob, leaving the digits — three or
      // four pixels of them at panel size — as the only thing to read. Mid-grey
      // with white digits gives the badge an edge of its own and keeps the whole
      // icon colourless, which is what the setting is for.
      paintCountBadge(p, size, text, QColor(0x6a, 0x6a, 0x6a), Qt::white,
                      /*cutOut=*/true);
    else
      paintCountBadge(p, size, text, QColor(0xe1, 0x1d, 0x1d), // the artwork's red
                      Qt::white);
    p.end();
  }

  // Not connected: dim the whole thing so it plainly reads as inactive.
  if (!connected) {
    QPixmap dimmed(size, size);
    dimmed.fill(Qt::transparent);
    QPainter p(&dimmed);
    p.setOpacity(0.40);
    p.drawPixmap(0, 0, base);
    p.end();
    return dimmed.toImage();
  }

  return base.toImage();
}

} // namespace TrayIcon
