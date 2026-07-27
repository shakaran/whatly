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

QImage composeTrayImage(int notificationCount, bool monochrome, bool connected,
                        int size) {
  const int count = std::clamp(notificationCount, 0, 10);

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
    // The colourful icons already carry the count badge baked in.
    QPixmap glyph(colourPath(count));
    QPainter p(&base);
    p.drawPixmap(base.rect(), glyph);
  }

  // In monochrome mode the count is not baked into the glyph, so draw it.
  if (monochrome && count > 0) {
    QPainter p(&base);
    p.setRenderHint(QPainter::Antialiasing);
    const int d = 34;
    const QRect badge(size - d, 0, d, d);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xea, 0xea, 0xea)); // same light as the glyph
    p.drawEllipse(badge);
    QFont f = qApp->font();
    f.setPixelSize(count >= 10 ? 20 : 26);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x11, 0x11, 0x11)); // dark number on the light badge
    p.drawText(badge, Qt::AlignCenter,
               count >= 10 ? QStringLiteral("9+") : QString::number(count));
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
