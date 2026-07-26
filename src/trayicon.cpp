#include "trayicon.h"

#include <QPainter>
#include <QSvgRenderer>

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

} // namespace TrayIcon
