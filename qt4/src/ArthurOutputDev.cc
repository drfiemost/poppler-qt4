//========================================================================
//
// ArthurOutputDev.cc
//
// Copyright 2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005 Brad Hards <bradh@frogmouth.net>
// Copyright (C) 2005-2009, 2011, 2012, 2014, 2015, 2018 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2008, 2010 Pino Toscano <pino@kde.org>
// Copyright (C) 2009, 2011 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2009 Petr Gajdos <pgajdos@novell.com>
// Copyright (C) 2010 Matthias Fauconneau <matthias.fauconneau@gmail.com>
// Copyright (C) 2011 Andreas Hartmetz <ahartmetz@gmail.com>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2013 Dominik Haumann <dhaumann@kde.org>
// Copyright (C) 2017 Adrian Johnson <ajohnson@redneon.com>
// //
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <string.h>
#include <math.h>

#include "goo/gfile.h"
#include "GlobalParams.h"
#include "Error.h"
#include "Object.h"
#include "GfxState.h"
#include "GfxFont.h"
#include "Link.h"
#include "FontEncodingTables.h"
#include <fofi/FoFiTrueType.h>
#include "ArthurOutputDev.h"

#include <QtCore/QtDebug>
#include <QtGui/QPainterPath>
#include <QPicture>
//------------------------------------------------------------------------

#ifdef HAVE_SPLASH
#include "splash/SplashFontFileID.h"
#include "splash/SplashFontFile.h"
#include "splash/SplashFontEngine.h"
#include "splash/SplashFont.h"
#include "splash/SplashMath.h"
#include "splash/SplashPath.h"
#include "splash/SplashGlyphBitmap.h"
//------------------------------------------------------------------------
// SplashOutFontFileID
//------------------------------------------------------------------------

class SplashOutFontFileID: public SplashFontFileID {
public:

  SplashOutFontFileID(Ref *rA) { r = *rA; }

  ~SplashOutFontFileID() {}

  GBool matches(SplashFontFileID *id) {
    return ((SplashOutFontFileID *)id)->r.num == r.num &&
           ((SplashOutFontFileID *)id)->r.gen == r.gen;
  }

private:

  Ref r;
};

#endif

//------------------------------------------------------------------------
// ArthurOutputDev
//------------------------------------------------------------------------

ArthurOutputDev::ArthurOutputDev(QPainter *painter):
  m_lastTransparencyGroupPicture(nullptr),
  m_fontHinting(NoHinting)
{
  m_painter.push(painter);
  m_currentBrush = QBrush(Qt::SolidPattern);
  m_fontEngine = nullptr;
  m_font = nullptr;
}

ArthurOutputDev::~ArthurOutputDev()
{
#ifdef HAVE_SPLASH
  delete m_fontEngine;
#endif
}

void ArthurOutputDev::startDoc(XRef *xrefA) {
  xref = xrefA;
#ifdef HAVE_SPLASH
  delete m_fontEngine;

  const bool isHintingEnabled = m_fontHinting != NoHinting;
  const bool isSlightHinting = m_fontHinting == SlightHinting;

  m_fontEngine = new SplashFontEngine(
  globalParams->getEnableFreeType(),
  isHintingEnabled,
  isSlightHinting,
  m_painter.top()->testRenderHint(QPainter::TextAntialiasing));
#endif
}

void ArthurOutputDev::startPage(int pageNum, GfxState *state, XRef *)
{
  // fill page with white background.
  int w = static_cast<int>(state->getPageWidth());
  int h = static_cast<int>(state->getPageHeight());
  QColor fillColour(Qt::white);
  QBrush fill(fillColour);
  m_painter.top()->save();
  m_painter.top()->setPen(fillColour);
  m_painter.top()->setBrush(fill);
  m_painter.top()->drawRect(0, 0, w, h);
  m_painter.top()->restore();
}

void ArthurOutputDev::endPage() {
}

void ArthurOutputDev::saveState(GfxState *state)
{
  m_painter.top()->save();
}

void ArthurOutputDev::restoreState(GfxState *state)
{
  m_painter.top()->restore();
}

void ArthurOutputDev::updateAll(GfxState *state)
{
  OutputDev::updateAll(state);
  m_needFontUpdate = gTrue;
}

// This looks wrong - why aren't adjusting the matrix?
void ArthurOutputDev::updateCTM(GfxState *state, double m11, double m12,
				double m21, double m22,
				double m31, double m32)
{
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
}

void ArthurOutputDev::updateLineDash(GfxState *state)
{
  double *dashPattern;
  int dashLength;
  double dashStart;
  state->getLineDash(&dashPattern, &dashLength, &dashStart);
  QVector<qreal> pattern(dashLength);
  for (int i = 0; i < dashLength; ++i) {
    pattern[i] = dashPattern[i];
  }
  m_currentPen.setDashPattern(pattern);
  m_currentPen.setDashOffset(dashStart);
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateFlatness(GfxState *state)
{
  // qDebug() << "updateFlatness";
}

void ArthurOutputDev::updateLineJoin(GfxState *state)
{
  switch (state->getLineJoin()) {
  case 0:
    m_currentPen.setJoinStyle(Qt::MiterJoin);
    break;
  case 1:
    m_currentPen.setJoinStyle(Qt::RoundJoin);
    break;
  case 2:
    m_currentPen.setJoinStyle(Qt::BevelJoin);
    break;
  }
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateLineCap(GfxState *state)
{
  switch (state->getLineCap()) {
  case 0:
    m_currentPen.setCapStyle(Qt::FlatCap);
    break;
  case 1:
    m_currentPen.setCapStyle(Qt::RoundCap);
    break;
  case 2:
    m_currentPen.setCapStyle(Qt::SquareCap);
    break;
  }
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateMiterLimit(GfxState *state)
{
  m_currentPen.setMiterLimit(state->getMiterLimit());
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateLineWidth(GfxState *state)
{
  m_currentPen.setWidthF(state->getLineWidth());
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateFillColor(GfxState *state)
{
  GfxRGB rgb;
  QColor brushColour = m_currentBrush.color();
  state->getFillRGB(&rgb);
  brushColour.setRgbF(colToDbl(rgb.r), colToDbl(rgb.g), colToDbl(rgb.b), brushColour.alphaF());
  m_currentBrush.setColor(brushColour);
}

void ArthurOutputDev::updateStrokeColor(GfxState *state)
{
  GfxRGB rgb;
  QColor penColour = m_currentPen.color();
  state->getStrokeRGB(&rgb);
  penColour.setRgbF(colToDbl(rgb.r), colToDbl(rgb.g), colToDbl(rgb.b), penColour.alphaF());
  m_currentPen.setColor(penColour);
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateBlendMode(GfxState * state)
{
  GfxBlendMode blendMode = state->getBlendMode();

  // missing composition modes in QPainter:
  // - CompositionMode_Hue
  // - CompositionMode_Color
  // - CompositionMode_Luminosity
  // - CompositionMode_Saturation

  switch(blendMode){
  case gfxBlendMultiply:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Multiply);
    break;
  case gfxBlendScreen:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Screen);
    break;
  case gfxBlendDarken:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Darken);
    break;
  case gfxBlendLighten:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Lighten);
    break;
  case gfxBlendColorDodge:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_ColorDodge);
    break;
  case gfxBlendColorBurn:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_ColorBurn);
    break;
  case gfxBlendHardLight:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_HardLight);
    break;
  case gfxBlendSoftLight:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_SoftLight);
    break;
  case gfxBlendDifference:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Difference);
    break;
  case gfxBlendExclusion:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Exclusion);
    break;
  case gfxBlendColor:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_Plus);
    break;
  default:
    qDebug() << "Unsupported blend mode, falling back to CompositionMode_SourceOver";
  case gfxBlendNormal:
    m_painter.top()->setCompositionMode(QPainter::CompositionMode_SourceOver);
    break;
  }
}

void ArthurOutputDev::updateFillOpacity(GfxState *state)
{
  QColor brushColour= m_currentBrush.color();
  brushColour.setAlphaF(state->getFillOpacity());
  m_currentBrush.setColor(brushColour);
}

void ArthurOutputDev::updateStrokeOpacity(GfxState *state)
{
  QColor penColour= m_currentPen.color();
  penColour.setAlphaF(state->getStrokeOpacity());
  m_currentPen.setColor(penColour);
  m_painter.top()->setPen(m_currentPen);
}

void ArthurOutputDev::updateFont(GfxState *state)
{
#ifdef HAVE_SPLASH
  GfxFont *gfxFont;
  GfxFontLoc *fontLoc;
  GfxFontType fontType;
  SplashOutFontFileID *id;
  SplashFontFile *fontFile;
  SplashFontSrc *fontsrc = nullptr;
  FoFiTrueType *ff;
  GooString *fileName;
  char *tmpBuf;
  int tmpBufLen = 0;
  int *codeToGID;
  double *textMat;
  double m11, m12, m21, m22, fontSize;
  SplashCoord mat[4];
  int n;
  int faceIndex = 0;
  SplashCoord matrix[6];

  m_needFontUpdate = false;
  m_font = nullptr;
  fileName = nullptr;
  tmpBuf = nullptr;
  fontLoc = nullptr;

  if (!(gfxFont = state->getFont())) {
    goto err1;
  }
  fontType = gfxFont->getType();
  if (fontType == fontType3) {
    goto err1;
  }

  // check the font file cache
  id = new SplashOutFontFileID(gfxFont->getID());
  if ((fontFile = m_fontEngine->getFontFile(id))) {
    delete id;

  } else {

    if (!(fontLoc = gfxFont->locateFont(xref, nullptr))) {
      error(errSyntaxError, -1, "Couldn't find a font for '{0:s}'",
	    gfxFont->getName() ? gfxFont->getName()->getCString()
	                       : "(unnamed)");
      goto err2;
    }

    // embedded font
    if (fontLoc->locType == gfxFontLocEmbedded) {
      // if there is an embedded font, read it to memory
      tmpBuf = gfxFont->readEmbFontFile(xref, &tmpBufLen);
      if (! tmpBuf)
	goto err2;

    // external font
    } else { // gfxFontLocExternal
      fileName = fontLoc->path;
      fontType = fontLoc->fontType;
    }

    fontsrc = new SplashFontSrc;
    if (fileName)
      fontsrc->setFile(fileName, gFalse);
    else
      fontsrc->setBuf(tmpBuf, tmpBufLen, gTrue);
    
    // load the font file
    switch (fontType) {
    case fontType1:
      if (!(fontFile = m_fontEngine->loadType1Font(
			   id,
			   fontsrc,
			   (const char **)((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontType1C:
      if (!(fontFile = m_fontEngine->loadType1CFont(
			   id,
			   fontsrc,
			   (const char **)((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontType1COT:
      if (!(fontFile = m_fontEngine->loadOpenTypeT1CFont(
			   id,
			   fontsrc,
			   (const char **)((Gfx8BitFont *)gfxFont)->getEncoding()))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontTrueType:
    case fontTrueTypeOT:
	if (fileName)
	 ff = FoFiTrueType::load(fileName->getCString());
	else
	ff = FoFiTrueType::make(tmpBuf, tmpBufLen);
      if (ff) {
	codeToGID = ((Gfx8BitFont *)gfxFont)->getCodeToGIDMap(ff);
	n = 256;
	delete ff;
      } else {
	codeToGID = nullptr;
	n = 0;
      }
      if (!(fontFile = m_fontEngine->loadTrueTypeFont(
			   id,
			   fontsrc,
			   codeToGID, n))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType0:
    case fontCIDType0C:
      if (!(fontFile = m_fontEngine->loadCIDFont(
			   id,
			   fontsrc))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType0COT:
      if (((GfxCIDFont *)gfxFont)->getCIDToGID()) {
	n = ((GfxCIDFont *)gfxFont)->getCIDToGIDLen();
	codeToGID = (int *)gmallocn(n, sizeof(int));
	memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
	       n * sizeof(int));
      } else {
	codeToGID = nullptr;
	n = 0;
      }      
      if (!(fontFile = m_fontEngine->loadOpenTypeCFFFont(
			   id,
			   fontsrc,
			   codeToGID, n))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    case fontCIDType2:
    case fontCIDType2OT:
      codeToGID = nullptr;
      n = 0;
      if (((GfxCIDFont *)gfxFont)->getCIDToGID()) {
	n = ((GfxCIDFont *)gfxFont)->getCIDToGIDLen();
	if (n) {
	  codeToGID = (int *)gmallocn(n, sizeof(int));
	  memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
		  n * sizeof(Gushort));
	}
      } else {
	if (fileName)
	  ff = FoFiTrueType::load(fileName->getCString());
	else
	  ff = FoFiTrueType::make(tmpBuf, tmpBufLen);
	if (! ff)
	  goto err2;
	codeToGID = ((GfxCIDFont *)gfxFont)->getCodeToGIDMap(ff, &n);
	delete ff;
      }
      if (!(fontFile = m_fontEngine->loadTrueTypeFont(
			   id,
			   fontsrc,
			   codeToGID, n, faceIndex))) {
	error(errSyntaxError, -1, "Couldn't create a font for '{0:s}'",
	      gfxFont->getName() ? gfxFont->getName()->getCString()
	                         : "(unnamed)");
	goto err2;
      }
      break;
    default:
      // this shouldn't happen
      goto err2;
    }
  }

  // get the font matrix
  textMat = state->getTextMat();
  fontSize = state->getFontSize();
  m11 = textMat[0] * fontSize * state->getHorizScaling();
  m12 = textMat[1] * fontSize * state->getHorizScaling();
  m21 = textMat[2] * fontSize;
  m22 = textMat[3] * fontSize;

  {
  QMatrix painterMatrix = m_painter.top()->worldMatrix();
  matrix[0] = painterMatrix.m11();
  matrix[1] = painterMatrix.m12();
  matrix[2] = painterMatrix.m21();
  matrix[3] = painterMatrix.m22();
  matrix[4] = painterMatrix.dx();
  matrix[5] = painterMatrix.dy();
  }

  // create the scaled font
  mat[0] = m11;  mat[1] = -m12;
  mat[2] = m21;  mat[3] = -m22;
  m_font = m_fontEngine->getFont(fontFile, mat, matrix);

  delete fontLoc;
  if (fontsrc && !fontsrc->isFile)
      fontsrc->unref();
  return;

 err2:
  delete id;
  delete fontLoc;
 err1:
  if (fontsrc && !fontsrc->isFile)
      fontsrc->unref();
  return;
#endif
}

static QPainterPath convertPath(GfxState *state, GfxPath *path, Qt::FillRule fillRule)
{
  GfxSubpath *subpath;
  double x1, y1, x2, y2, x3, y3;
  int i, j;

  QPainterPath qPath;
  qPath.setFillRule(fillRule);
  for (i = 0; i < path->getNumSubpaths(); ++i) {
    subpath = path->getSubpath(i);
    if (subpath->getNumPoints() > 0) {
      state->transform(subpath->getX(0), subpath->getY(0), &x1, &y1);
      qPath.moveTo(x1, y1);
      j = 1;
      while (j < subpath->getNumPoints()) {
	if (subpath->getCurve(j)) {
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  state->transform(subpath->getX(j+1), subpath->getY(j+1), &x2, &y2);
	  state->transform(subpath->getX(j+2), subpath->getY(j+2), &x3, &y3);
	  qPath.cubicTo( x1, y1, x2, y2, x3, y3);
	  j += 3;
	} else {
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  qPath.lineTo(x1, y1);
	  ++j;
	}
      }
      if (subpath->isClosed()) {
	qPath.closeSubpath();
      }
    }
  }
  return qPath;
}

void ArthurOutputDev::stroke(GfxState *state)
{
  m_painter.top()->strokePath( convertPath( state, state->getPath(), Qt::OddEvenFill ), m_currentPen );
}

void ArthurOutputDev::fill(GfxState *state)
{
  m_painter.top()->fillPath( convertPath( state, state->getPath(), Qt::WindingFill ), m_currentBrush );
}

void ArthurOutputDev::eoFill(GfxState *state)
{
  m_painter.top()->fillPath( convertPath( state, state->getPath(), Qt::OddEvenFill ), m_currentBrush );
}

void ArthurOutputDev::clip(GfxState *state)
{
  m_painter.top()->setClipPath(convertPath( state, state->getPath(), Qt::WindingFill ) );
}

void ArthurOutputDev::eoClip(GfxState *state)
{
  m_painter.top()->setClipPath(convertPath( state, state->getPath(), Qt::OddEvenFill ) );
}

void ArthurOutputDev::drawChar(GfxState *state, double x, double y,
			       double dx, double dy,
			       double originX, double originY,
			       CharCode code, int nBytes, Unicode *u, int uLen) {
#ifdef HAVE_SPLASH
  double x1, y1;
  double x2, y2;
//   SplashPath *path;
  int render;

  if (m_needFontUpdate) {
    updateFont(state);
  }
  if (!m_font) {
    return;
  }

  // check for invisible text -- this is used by Acrobat Capture
  render = state->getRender();
  if (render == 3) {
    return;
  }

  x -= originX;
  y -= originY;

  // fill
  if (!(render & 1)) {
    SplashPath * fontPath;
    fontPath = m_font->getGlyphPath(code);
    if (fontPath) {
      QPainterPath qPath;
      qPath.setFillRule(Qt::WindingFill);
      for (int i = 0; i < fontPath->length; ++i) {
        // SplashPath.flags: bitwise or allowed
        if (fontPath->flags[i] & splashPathLast || fontPath->flags[i] & splashPathClosed) {
            qPath.closeSubpath();
        }
        if (fontPath->flags[i] & splashPathFirst) {
            state->transform(fontPath->pts[i].x+x, -fontPath->pts[i].y+y, &x1, &y1);
            qPath.moveTo(x1,y1);
        }
        if (fontPath->flags[i] & splashPathCurve) {
            state->transform(fontPath->pts[i].x+x, -fontPath->pts[i].y+y, &x1, &y1);
            state->transform(fontPath->pts[i+1].x+x, -fontPath->pts[i+1].y+y, &x2, &y2);
            qPath.quadTo(x1,y1,x2,y2);
            ++i;
        }
        // FIXME fix this
        // 	else if (fontPath->flags[i] & splashPathArcCW) {
        // 	  qDebug() << "Need to implement arc";
        // 	}
        else {
            state->transform(fontPath->pts[i].x+x, -fontPath->pts[i].y+y, &x1, &y1);
            qPath.lineTo(x1,y1);
        }
      }
      GfxRGB rgb;
      QColor brushColour = m_currentBrush.color();
      state->getFillRGB(&rgb);
      brushColour.setRgbF(colToDbl(rgb.r), colToDbl(rgb.g), colToDbl(rgb.b), state->getFillOpacity());
      m_painter.top()->setBrush(brushColour);
      m_painter.top()->setPen(Qt::NoPen);
      m_painter.top()->drawPath(qPath);
      delete fontPath;
    }
  }

  // stroke
  if ((render & 3) == 1 || (render & 3) == 2) {
    qDebug() << "no stroke";
    /*
    if ((path = m_font->getGlyphPath(code))) {
      path->offset((SplashCoord)x1, (SplashCoord)y1);
      splash->stroke(path);
      delete path;
    }
    */
  }

  // clip
  if (render & 4) {
    qDebug() << "no clip";
    /*
    path = m_font->getGlyphPath(code);
    path->offset((SplashCoord)x1, (SplashCoord)y1);
    if (textClipPath) {
      textClipPath->append(path);
      delete path;
    } else {
      textClipPath = path;
    }
    */
  }
#endif
}

GBool ArthurOutputDev::beginType3Char(GfxState *state, double x, double y,
				      double dx, double dy,
				      CharCode code, Unicode *u, int uLen)
{
  return gFalse;
}

void ArthurOutputDev::endType3Char(GfxState *state)
{
}

void ArthurOutputDev::type3D0(GfxState *state, double wx, double wy)
{
}

void ArthurOutputDev::type3D1(GfxState *state, double wx, double wy,
			      double llx, double lly, double urx, double ury)
{
}

void ArthurOutputDev::endTextObject(GfxState *state)
{
}


void ArthurOutputDev::drawImageMask(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool interpolate, GBool inlineImg)
{
  qDebug() << "drawImageMask";
#if 0
  unsigned char *buffer;
  unsigned char *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  double *ctm;
  cairo_matrix_t matrix;
  int invert_bit;
  int row_stride;

  row_stride = (width + 3) & ~3;
  buffer = (unsigned char *) malloc (height * row_stride);
  if (buffer == nullptr) {
    error(-1, "Unable to allocate memory for image.");
    return;
  }

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width, 1, 1);
  imgStr->reset();

  invert_bit = invert ? 1 : 0;

  for (y = 0; y < height; y++) {
    pix = imgStr->getLine();
    dest = buffer + y * row_stride;
    for (x = 0; x < width; x++) {

      if (pix[x] ^ invert_bit)
	*dest++ = 0;
      else
	*dest++ = 255;
    }
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_A8,
					  width, height, row_stride);
  if (image == nullptr)
    return;
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == nullptr)
    return;

  ctm = state->getCTM();
  LOG (printf ("drawImageMask %dx%d, matrix: %f, %f, %f, %f, %f, %f\n",
	       width, height, ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]));
  matrix.xx = ctm[0] / width;
  matrix.xy = -ctm[2] / height;
  matrix.yx = ctm[1] / width;
  matrix.yy = -ctm[3] / height;
  matrix.x0 = ctm[2] + ctm[4];
  matrix.y0 = ctm[3] + ctm[5];
  cairo_matrix_invert (&matrix);
  cairo_pattern_set_matrix (pattern, &matrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);
  /* FIXME: Doesn't the image mask support any colorspace? */
  cairo_set_source_rgb (cairo, fill_color.r, fill_color.g, fill_color.b);
  cairo_mask (cairo, pattern);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  imgStr->close ();
  delete imgStr;
#endif
}

//TODO: lots more work here.
void ArthurOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
				int width, int height,
				GfxImageColorMap *colorMap,
				GBool interpolate, int *maskColors, GBool inlineImg)
{
  unsigned int *data;
  unsigned int *line;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  int i;
  double *ctm;
  QMatrix matrix;
  QImage image;
  int stride;
  
  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width,
			   colorMap->getNumPixelComps(),
			   colorMap->getBits());
  imgStr->reset();
  
  image = QImage(width, height, QImage::Format_ARGB32);
  data = (unsigned int *)image.bits();
  stride = image.bytesPerLine()/4;
  for (y = 0; y < height; y++) {
    pix = imgStr->getLine();
    line = data+y*stride;
    colorMap->getRGBLine(pix, line, width);

    if (maskColors) {
      for (x = 0; x < width; x++) {
        for (i = 0; i < colorMap->getNumPixelComps(); ++i) {
            if (pix[i] < maskColors[2*i] * 255||
                pix[i] > maskColors[2*i+1] * 255) {
                *line = *line | 0xff000000;
                break;
            }
        }
        pix += colorMap->getNumPixelComps();
        line++;
      }
    } else {
      for (x = 0; x < width; x++) { *line = *line | 0xff000000; line++; }
    }
  }

  ctm = state->getCTM();
  matrix.setMatrix(ctm[0] / width, ctm[1] / width, -ctm[2] / height, -ctm[3] / height, ctm[2] + ctm[4], ctm[3] + ctm[5]);

  m_painter.top()->setMatrix(matrix, true);
  m_painter.top()->drawImage( QPoint(0,0), image );
  delete imgStr;

}

void ArthurOutputDev::beginTransparencyGroup(GfxState * /*state*/, double * /*bbox*/,
                                             GfxColorSpace * /*blendingColorSpace*/,
                                             GBool /*isolated*/, GBool /*knockout*/,
                                             GBool /*forSoftMask*/)
{
  // The entire transparency group will be painted into a
  // freshly created QPicture object.  Since an existing painter
  // cannot change its paint device, we need to construct a
  // new QPainter object as well.
  m_qpictures.push(new QPicture);
  m_painter.push(new QPainter(m_qpictures.top()));
}

void ArthurOutputDev::endTransparencyGroup(GfxState * /*state*/)
{
  // Stop painting into the group
  m_painter.top()->end();

  // Kill the painter that has been used for the transparency group
  delete(m_painter.top());
  m_painter.pop();

  // Store the QPicture object that holds the result of the transparency group
  // painting.  It will be painted and deleted in the method paintTransparencyGroup.
  if (m_lastTransparencyGroupPicture)
  {
    qDebug() << "Found a transparency group that has not been painted";
    delete(m_lastTransparencyGroupPicture);
  }
  m_lastTransparencyGroupPicture = m_qpictures.top();
  m_qpictures.pop();
}

void ArthurOutputDev::paintTransparencyGroup(GfxState * /*state*/, double * /*bbox*/)
{
  // Actually draw the transparency group
  m_painter.top()->drawPicture(0,0,*m_lastTransparencyGroupPicture);

  // And delete it
  delete(m_lastTransparencyGroupPicture);
  m_lastTransparencyGroupPicture = nullptr;
}
