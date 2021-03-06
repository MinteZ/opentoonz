

#include "tools/stylepicker.h"
#include "tcolorstyles.h"
#include "tstroke.h"
#include "tofflinegl.h"
#include "tvectorrenderdata.h"
#include "tvectorimage.h"
#include "ttoonzimage.h"
#include "trasterimage.h"
#include "toonz/dpiscale.h"
#include "tpixelutils.h"
#include "tregion.h"

//---------------------------------------------------------

StylePicker::StylePicker(const TImageP &image)
    : m_image(image), m_palette(image->getPalette()) {}

//---------------------------------------------------------

StylePicker::StylePicker(const TImageP &image, const TPaletteP &palette)
    : m_image(image), m_palette(palette) {}

//---------------------------------------------------------

TPoint StylePicker::getRasterPoint(const TPointD &p) const {
  if (TToonzImageP ti = m_image) {
    // DpiScale dpiScale(ti);
    TDimension size = ti->getSize();
    return TPoint(tround(0.5 * size.lx + p.x),   /// dpiScale.getSx()),
                  tround(0.5 * size.ly + p.y));  /// dpiScale.getSy()));
  } else if (TRasterImageP ri = m_image) {
    // DpiScale dpiScale(ri);
    TDimension size = ri->getRaster()->getSize();
    return TPoint(tround(0.5 * size.lx + p.x),   // /dpiScale.getSx()),
                  tround(0.5 * size.ly + p.y));  // /dpiScale.getSy()));
  } else
    return TPoint(tround(p.x), tround(p.y));
}

//---------------------------------------------------------
/*-- (StylePickerTool内で)LineとAreaを切り替えてPickできる。mode: 0=Area,
 * 1=Line, 2=Line&Areas(default)  --*/
int StylePicker::pickStyleId(const TPointD &pos, double radius2,
                             int mode) const {
  int styleId = 0;
  if (TToonzImageP ti = m_image) {
    TRasterCM32P ras = ti->getRaster();
    TPoint point     = getRasterPoint(pos);
    if (!ras->getBounds().contains(point)) return -1;
    TPixelCM32 col = ras->pixels(point.y)[point.x];

    switch (mode) {
    case 0:  // AREAS
      styleId = col.getPaint();
      break;
    case 1:  // LINES
      styleId = col.getInk();
      break;
    case 2:  // ALL (Line & Area)
    default:
      styleId = col.isPurePaint() ? col.getPaint() : col.getInk();
      break;
    }
  } else if (TRasterImageP ri = m_image) {
    const TPalette *palette = m_palette.getPointer();
    if (!palette) return -1;
    TRaster32P ras = ri->getRaster();
    if (!ras) return -1;
    TPoint point = getRasterPoint(pos);
    if (!ras->getBounds().contains(point)) return -1;
    TPixel32 col = ras->pixels(point.y)[point.x];
    styleId      = palette->getClosestStyle(col);
  } else if (TVectorImageP vi = m_image) {
    // prima cerca lo stile della regione piu' vicina
    TRegion *r     = vi->getRegion(pos);
    if (r) styleId = r->getStyle();
    // poi cerca quello della stroke, ma se prima aveva trovato una regione,
    // richiede che
    // il click sia proprio sopra la stroke, altrimenti cerca la stroke piu'
    // vicina (max circa 10 pixel)
    const double maxDist2 = (styleId == 0) ? 100.0 * radius2 : 0;
    bool strokeFound;
    double dist2, w, thick;
    UINT index;
    //! funzionerebbe ancora meglio con un getNearestStroke che considera
    // la thickness, cioe' la min distance dalla outline e non dalla centerLine
    strokeFound = vi->getNearestStroke(pos, w, index, dist2);
    if (strokeFound) {
      TStroke *stroke = vi->getStroke(index);
      thick           = stroke->getThickPoint(w).thick;
      if (dist2 - thick * thick < maxDist2) {
        assert(stroke);
        styleId = stroke->getStyle();
      }
    }
  }
  return styleId;
}

//---------------------------------------------------------
/*--- Toonz Raster LevelのToneを拾う。 ---*/
int StylePicker::pickTone(const TPointD &pos) {
  if (TToonzImageP ti = m_image) {
    TRasterCM32P ras = ti->getRaster();
    if (!ras) return -1;
    TPoint point = getRasterPoint(pos);
    if (!ras->getBounds().contains(point)) return -1;
    TPixelCM32 col = ras->pixels(point.y)[point.x];

    return col.getTone();
  } else
    return -1;
}

//---------------------------------------------------------

TPixel32 StylePicker::pickColor(const TPointD &pos, double radius2) const {
  TToonzImageP ti  = m_image;
  TRasterImageP ri = m_image;
  if (!!ri)  // !!ti || !!ri)
  {
    TRasterP raster;
    // if(ti)
    //  raster = ti->getRGBM(true);
    // else
    raster = ri->getRaster();

    TPoint point = getRasterPoint(pos);
    if (!raster->getBounds().contains(point)) return TPixel32::Transparent;

    TRaster32P raster32 = raster;
    if (raster32) return raster32->pixels(point.y)[point.x];

    TRasterGR8P rasterGR8 = raster;
    if (rasterGR8) return toPixel32(rasterGR8->pixels(point.y)[point.x]);
  } else if (TVectorImageP vi = m_image) {
    const TPalette *palette = m_palette.getPointer();
    if (!palette) return TPixel32::Transparent;
    int styleId = pickStyleId(pos, radius2);
    if (0 <= styleId && styleId < palette->getStyleCount())
      return palette->getStyle(styleId)->getAverageColor();
  }
  return TPixel32::Transparent;
}

//---------------------------------------------------------

namespace {

TPixel32 getAverageColor(const TRect &rect) {
  GLenum fmt =
#if defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
      GL_RGBA;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_MBGR)
      GL_ABGR_EXT;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_RGBM)
      GL_RGBA;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_MRGB)
      GL_BGRA;
#else
//   Error  PLATFORM NOT SUPPORTED
#error "unknown channel order!"
#endif
  UINT r = 0, g = 0, b = 0, m = 0;
  std::vector<TPixel32> buffer(rect.getLx() * rect.getLy());
  glReadPixels(rect.x0, rect.y0, rect.getLx(), rect.getLy(), fmt,
               GL_UNSIGNED_BYTE, &buffer[0]);
  int size = rect.getLx() * rect.getLy();
  for (int i = 0; i < size; i++) {
    r += buffer[i].r;
    g += buffer[i].g;
    b += buffer[i].b;
  }

  return TPixel32(b / size, g / size, r / size, 255);
}

//---------------------------------------------------------

TPixel32 getAverageColor(TStroke *stroke) {
  GLenum fmt =
#if defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
      GL_RGBA;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_MBGR)
      GL_ABGR_EXT;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_RGBM)
      GL_RGBA;
#elif defined(TNZ_MACHINE_CHANNEL_ORDER_MRGB)
      GL_BGRA;
#else
//   Error  PLATFORM NOT SUPPORTED
#error "unknown channel order"
#endif

  // leggo il buffer e mi prendo i pixels
  UINT r = 0, g = 0, b = 0, m = 0;
  TRect rect = convert(stroke->getBBox());
  std::vector<TPixel32> buffer(rect.getLx() * rect.getLy());
  glReadPixels(rect.x0, rect.y0, rect.getLx(), rect.getLy(), fmt,
               GL_UNSIGNED_BYTE, &buffer[0]);

  // calcolo le regioni dello stroke
  TVectorImage aux;
  aux.addStroke(stroke);
  aux.transform(TTranslation(convert(-rect.getP00())));
  aux.findRegions();
  int regionCount = aux.getRegionCount();
  int size = 0, lx = rect.getLx();

  for (int j = 0; j < regionCount; j++) {
    TRegion *reg  = aux.getRegion(j);
    TRect regRect = convert(reg->getBBox());
    for (int y = regRect.y0; y < regRect.y1; y++) {
      std::vector<double> intersections;
      reg->computeScanlineIntersections(y, intersections);
      assert(!(intersections.size() & 0x1));
      for (UINT i = 0; i < intersections.size(); i += 2) {
        if (intersections[i] == intersections[i + 1]) continue;
        int firstInters  = (int)intersections[i];
        int secondInters = (int)intersections[i + 1];
        for (int x = firstInters + 1; x < secondInters - 1; x++) {
          r += buffer[y * lx + x].r;
          g += buffer[y * lx + x].g;
          b += buffer[y * lx + x].b;
          size++;
        }
      }
    }
  }

  if (size != 0)
    return TPixel32(b / size, g / size, r / size, 255);
  else
    return TPixel32(buffer[0].b, buffer[0].g, buffer[0].r, 255);
}

}  // namspace

//---------------------------------------------------------

TPixel32 StylePicker::pickColor(const TRectD &area) const {
  // TRectD rect=area.enlarge(-1,-1);
  return getAverageColor(convert(area));
}

//---------------------------------------------------------

TPixel32 StylePicker::pickColor(TStroke *stroke) const {
  return getAverageColor(stroke);
}

//---------------------------------------------------------
