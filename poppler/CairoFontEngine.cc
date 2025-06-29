//========================================================================
//
// CairoFontEngine.cc
//
// Copyright 2003 Glyph & Cog, LLC
// Copyright 2004 Red Hat, Inc
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005-2007 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2005, 2006 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005 Martin Kretzschmar <martink@gnome.org>
// Copyright (C) 2005, 2009, 2012, 2013, 2015, 2017 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2006, 2007, 2010, 2011 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2007 Koji Otani <sho@bbr.jp>
// Copyright (C) 2008, 2009 Chris Wilson <chris@chris-wilson.co.uk>
// Copyright (C) 2008, 2012, 2014, 2016, 2017, 2018 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2009 Darren Kenny <darren.kenny@sun.com>
// Copyright (C) 2010 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2010 Jan Kümmel <jan+freedesktop@snorc.org>
// Copyright (C) 2012 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2015, 2016 Jason Crain <jason@aquaticape.us>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#include "config.h"
#include <string.h>
#include "CairoFontEngine.h"
#include "CairoOutputDev.h"
#include "GlobalParams.h"
#include <fofi/FoFiTrueType.h>
#include <fofi/FoFiType1C.h>
#include "goo/gfile.h"
#include "Error.h"
#include "XRef.h"
#include "Gfx.h"
#include "Page.h"

#if defined(HAVE_FCNTL_H) && defined(HAVE_SYS_MMAN_H) && defined(HAVE_SYS_STAT_H)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#define CAN_CHECK_OPEN_FACES 1
#endif

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#ifdef MULTITHREADED
#  define fontEngineLocker()   MutexLocker locker(&mutex)
#else
#  define fontEngineLocker()
#endif

//------------------------------------------------------------------------
// CairoFont
//------------------------------------------------------------------------

CairoFont::CairoFont(Ref refA,
             cairo_font_face_t *cairo_font_faceA,
             int *codeToGIDA,
             Guint codeToGIDLenA,
             GBool substituteA,
             GBool printingA) : ref(refA),
                       cairo_font_face(cairo_font_faceA),
                       codeToGID(codeToGIDA),
                       codeToGIDLen(codeToGIDLenA),
                       substitute(substituteA),
                       printing(printingA)      { }


CairoFont::~CairoFont() {
  cairo_font_face_destroy (cairo_font_face);
  gfree(codeToGID);
}

GBool
CairoFont::matches(Ref &other, GBool printingA) {
  return (other.num == ref.num && other.gen == ref.gen);
}

cairo_font_face_t *
CairoFont::getFontFace(void) {
  return cairo_font_face;
}

unsigned long
CairoFont::getGlyph(CharCode code,
		    Unicode *u, int uLen) {
  FT_UInt gid;

  if (codeToGID && code < codeToGIDLen) {
    gid = (FT_UInt)codeToGID[code];
  } else {
    gid = (FT_UInt)code;
  }
  return gid;
}

double
CairoFont::getSubstitutionCorrection(GfxFont *gfxFont)
{
  double w1, w2, w3;
  CharCode code;
  char *name;

  // for substituted fonts: adjust the font matrix -- compare the
  // width of 'm' in the original font and the substituted font
  if (isSubstitute() && !gfxFont->isCIDFont()) {
    for (code = 0; code < 256; ++code) {
      if ((name = ((Gfx8BitFont *)gfxFont)->getCharName(code)) &&
	  name[0] == 'm' && name[1] == '\0') {
	break;
      }
    }
    if (code < 256) {
      w1 = ((Gfx8BitFont *)gfxFont)->getWidth(code);
      {
	cairo_matrix_t m;
	cairo_matrix_init_identity(&m);
	cairo_font_options_t *options = cairo_font_options_create();
	cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
	cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
	cairo_scaled_font_t *scaled_font = cairo_scaled_font_create(cairo_font_face, &m, &m, options);

	cairo_text_extents_t extents;
	cairo_scaled_font_text_extents(scaled_font, "m", &extents);

	cairo_scaled_font_destroy(scaled_font);
	cairo_font_options_destroy(options);
	w2 = extents.x_advance;
      }
      w3 = ((Gfx8BitFont *)gfxFont)->getWidth(0);
      if (!gfxFont->isSymbolic() && w2 > 0 && w1 > w3) {
	// if real font is substantially narrower than substituted
	// font, reduce the font size accordingly
	if (w1 > 0.01 && w1 < 0.9 * w2) {
	  w1 /= w2;
	  return w1;
	}
      }
    }
  }
  return 1.0;
}

//------------------------------------------------------------------------
// CairoFreeTypeFont
//------------------------------------------------------------------------

static cairo_user_data_key_t _ft_cairo_key;

static void
_ft_done_face_uncached (void *closure)
{
    FT_Face face = (FT_Face) closure;
    FT_Done_Face (face);
}

static GBool
_ft_new_face_uncached (FT_Library lib,
		       const char *filename,
		       char *font_data,
		       int font_data_len,
		       FT_Face *face_out,
		       cairo_font_face_t **font_face_out)
{
  FT_Face face;
  cairo_font_face_t *font_face;

  if (font_data == nullptr) {
    if (FT_New_Face (lib, filename, 0, &face))
      return gFalse;
  } else {
    if (FT_New_Memory_Face (lib, (unsigned char *)font_data, font_data_len, 0, &face))
      return gFalse;
  }

  font_face = cairo_ft_font_face_create_for_ft_face (face,
							  FT_LOAD_NO_HINTING |
							  FT_LOAD_NO_BITMAP);
  if (cairo_font_face_set_user_data (font_face,
				     &_ft_cairo_key,
				     face,
				     _ft_done_face_uncached))
  {
    _ft_done_face_uncached (face);
    cairo_font_face_destroy (font_face);
    return gFalse;
  }

  *face_out = face;
  *font_face_out = font_face;
  return gTrue;
}

#ifdef CAN_CHECK_OPEN_FACES
static struct _ft_face_data {
  struct _ft_face_data *prev, *next, **head;

  int fd;
  unsigned long hash;
  size_t size;
  unsigned char *bytes;

  FT_Library lib;
  FT_Face face;
  cairo_font_face_t *font_face;
} *_ft_open_faces;

static unsigned long
_djb_hash (const unsigned char *bytes, size_t len)
{
  unsigned long hash = 5381;
  while (len--) {
    unsigned char c = *bytes++;
    hash *= 33;
    hash ^= c;
  }
  return hash;
}

static GBool
_ft_face_data_equal (struct _ft_face_data *a, struct _ft_face_data *b)
{
  if (a->lib != b->lib)
    return gFalse;
  if (a->size != b->size)
    return gFalse;
  if (a->hash != b->hash)
    return gFalse;

  return memcmp (a->bytes, b->bytes, a->size) == 0;
}

static void
_ft_done_face (void *closure)
{
  struct _ft_face_data *data = (struct _ft_face_data *) closure;

  if (data->next)
    data->next->prev = data->prev;
  if (data->prev)
    data->prev->next = data->next;
  else
    _ft_open_faces = data->next;

  if (data->fd != -1) {
#if defined(__SUNPRO_CC) && defined(__sun) && defined(__SVR4)
    munmap ((char*)data->bytes, data->size);
#else
    munmap (data->bytes, data->size);
#endif
    close (data->fd);
  } else {
    gfree (data->bytes);
  }

  FT_Done_Face (data->face);
  gfree (data);
}

static GBool
_ft_new_face (FT_Library lib,
	      const char *filename,
	      char *font_data,
	      int font_data_len,
	      FT_Face *face_out,
	      cairo_font_face_t **font_face_out)
{
  struct _ft_face_data *l;
  struct stat st;
  struct _ft_face_data tmpl;

  tmpl.fd = -1;

  if (font_data == nullptr) {
    /* if we fail to mmap the file, just pass it to FreeType instead */
    tmpl.fd = open (filename, O_RDONLY);
    if (tmpl.fd == -1)
      return _ft_new_face_uncached (lib, filename, font_data, font_data_len, face_out, font_face_out);

    if (fstat (tmpl.fd, &st) == -1) {
      close (tmpl.fd);
      return _ft_new_face_uncached (lib, filename, font_data, font_data_len, face_out, font_face_out);
    }

    tmpl.bytes = (unsigned char *) mmap (nullptr, st.st_size,
					 PROT_READ, MAP_PRIVATE,
					 tmpl.fd, 0);
    if (tmpl.bytes == MAP_FAILED) {
      close (tmpl.fd);
      return _ft_new_face_uncached (lib, filename, font_data, font_data_len, face_out, font_face_out);
    }
    tmpl.size = st.st_size;
  } else {
    tmpl.bytes = (unsigned char*) font_data;
    tmpl.size = font_data_len;
  }

  /* check to see if this is a duplicate of any of the currently open fonts */
  tmpl.lib = lib;
  tmpl.hash = _djb_hash (tmpl.bytes, tmpl.size);

  for (l = _ft_open_faces; l; l = l->next) {
    if (_ft_face_data_equal (l, &tmpl)) {
      if (tmpl.fd != -1) {
#if defined(__SUNPRO_CC) && defined(__sun) && defined(__SVR4)
        munmap ((char*)tmpl.bytes, tmpl.size);
#else
        munmap (tmpl.bytes, tmpl.size);
#endif
        close (tmpl.fd);
      } else {
	gfree (tmpl.bytes);
      }
      *face_out = l->face;
      *font_face_out = cairo_font_face_reference (l->font_face);
      return gTrue;
    }
  }

  /* not a dup, open and insert into list */
  if (FT_New_Memory_Face (lib,
			  (FT_Byte *) tmpl.bytes, tmpl.size,
			  0, &tmpl.face))
  {
    if (tmpl.fd != -1) {
#if defined(__SUNPRO_CC) && defined(__sun) && defined(__SVR4)
      munmap ((char*)tmpl.bytes, tmpl.size);
#else
      munmap (tmpl.bytes, tmpl.size);
#endif

      close (tmpl.fd);
    }
    return gFalse;
  }

  l = (struct _ft_face_data *) gmallocn (1, sizeof (struct _ft_face_data));
  *l = tmpl;
  l->prev = nullptr;
  l->next = _ft_open_faces;
  if (_ft_open_faces)
    _ft_open_faces->prev = l;
  _ft_open_faces = l;

  l->font_face = cairo_ft_font_face_create_for_ft_face (tmpl.face,
							  FT_LOAD_NO_HINTING |
							  FT_LOAD_NO_BITMAP);
  if (cairo_font_face_set_user_data (l->font_face,
				     &_ft_cairo_key,
				     l,
				     _ft_done_face))
  {
    cairo_font_face_destroy (l->font_face);
    _ft_done_face (l);
    return gFalse;
  }

  *face_out = l->face;
  *font_face_out = l->font_face;
  return gTrue;
}
#else
#define _ft_new_face _ft_new_face_uncached
#endif

CairoFreeTypeFont::CairoFreeTypeFont(Ref refA,
                     cairo_font_face_t *cairo_font_faceA,
                     int *codeToGIDA,
                     Guint codeToGIDLenA,
                     GBool substituteA) : CairoFont(refA,
                                   cairo_font_faceA,
                                   codeToGIDA,
                                   codeToGIDLenA,
                                   substituteA,
                                   gTrue) { }

CairoFreeTypeFont::~CairoFreeTypeFont() { }

CairoFreeTypeFont *CairoFreeTypeFont::create(GfxFont *gfxFont, XRef *xref,
					     FT_Library lib, GBool useCIDs) {
  GooString *fileName;
  char *fileNameC;
  char *font_data;
  int font_data_len;
  int i, n;
  GfxFontType fontType;
  GfxFontLoc *fontLoc;
  char **enc;
  const char *name;
  FoFiTrueType *ff;
  FoFiType1C *ff1c;
  Ref ref;
  FT_Face face;
  cairo_font_face_t *font_face;

  int *codeToGID;
  Guint codeToGIDLen;
  
  codeToGID = nullptr;
  codeToGIDLen = 0;
  font_data = nullptr;
  font_data_len = 0;
  fileName = nullptr;
  fileNameC = nullptr;

  GBool substitute = gFalse;
  
  ref = *gfxFont->getID();
  fontType = gfxFont->getType();

  if (!(fontLoc = gfxFont->locateFont(xref, nullptr))) {
    error(errSyntaxError, -1, "Couldn't find a font for '{0:s}'",
    	gfxFont->getName() ? gfxFont->getName()->getCString()
	                       : "(unnamed)");
    goto err2;
  }

  // embedded font
  if (fontLoc->locType == gfxFontLocEmbedded) {
    font_data = gfxFont->readEmbFontFile(xref, &font_data_len);
    if (nullptr == font_data)
      goto err2;

  // external font
  } else { // gfxFontLocExternal
    fileName = fontLoc->path;
    fontType = fontLoc->fontType;
    substitute = gTrue;
  }

  if (fileName != nullptr) {
    fileNameC = fileName->getCString();
  }

  switch (fontType) {
  case fontType1:
  case fontType1C:
  case fontType1COT:
    if (! _ft_new_face (lib, fileNameC, font_data, font_data_len, &face, &font_face)) {
      error(errSyntaxError, -1, "could not create type1 face");
      goto err2;
    }
    
    enc = ((Gfx8BitFont *)gfxFont)->getEncoding();
    
    codeToGID = (int *)gmallocn(256, sizeof(int));
    codeToGIDLen = 256;
    for (i = 0; i < 256; ++i) {
      codeToGID[i] = 0;
      if ((name = enc[i])) {
	codeToGID[i] = FT_Get_Name_Index(face, (char*)name);
	if (codeToGID[i] == 0) {
	  Unicode u;
	  u = globalParams->mapNameToUnicodeText (name);
	  codeToGID[i] = FT_Get_Char_Index (face, u);
	}
	if (codeToGID[i] == 0) {
	  name = GfxFont::getAlternateName(name);
	  if (name) {
	    codeToGID[i] = FT_Get_Name_Index(face, (char*)name);
	  }
	}
      }
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
		n * sizeof(int));
      }
    } else {
      if (font_data != nullptr) {
        ff = FoFiTrueType::make(font_data, font_data_len);
      } else {
        ff = FoFiTrueType::load(fileNameC);
      }
      if (! ff)
	goto err2;
      codeToGID = ((GfxCIDFont *)gfxFont)->getCodeToGIDMap(ff, &n);
      delete ff;
    }
    codeToGIDLen = n;
    /* Fall through */
  case fontTrueType:
  case fontTrueTypeOT:
    if (font_data != nullptr) {
      ff = FoFiTrueType::make(font_data, font_data_len);
    } else {
      ff = FoFiTrueType::load(fileNameC);
    }
    if (! ff) {
      error(errSyntaxError, -1, "failed to load truetype font\n");
      goto err2;
    }
    /* This might be set already for the CIDType2 case */
    if (fontType == fontTrueType || fontType == fontTrueTypeOT) {
      codeToGID = ((Gfx8BitFont *)gfxFont)->getCodeToGIDMap(ff);
      codeToGIDLen = 256;
    }
    delete ff;
    if (! _ft_new_face (lib, fileNameC, font_data, font_data_len, &face, &font_face)) {
      error(errSyntaxError, -1, "could not create truetype face\n");
      goto err2;
    }
    break;
    
  case fontCIDType0:
  case fontCIDType0C:

    codeToGID = nullptr;
    codeToGIDLen = 0;

    if (!useCIDs)
    {
      if (font_data != nullptr) {
        ff1c = FoFiType1C::make(font_data, font_data_len);
      } else {
        ff1c = FoFiType1C::load(fileNameC);
      }
      if (ff1c) {
        codeToGID = ff1c->getCIDToGIDMap((int *)&codeToGIDLen);
        delete ff1c;
      }
    }

    if (! _ft_new_face (lib, fileNameC, font_data, font_data_len, &face, &font_face)) {
      error(errSyntaxError, -1, "could not create cid face\n");
      goto err2;
    }
    break;

  case fontCIDType0COT:
    codeToGID = nullptr;
    n = 0;
    if (((GfxCIDFont *)gfxFont)->getCIDToGID()) {
      n = ((GfxCIDFont *)gfxFont)->getCIDToGIDLen();
      if (n) {
	codeToGID = (int *)gmallocn(n, sizeof(int));
	memcpy(codeToGID, ((GfxCIDFont *)gfxFont)->getCIDToGID(),
	       n * sizeof(int));
      }
    }
    codeToGIDLen = n;

    if (!codeToGID) {
      if (!useCIDs) {
	if (font_data != nullptr) {
	  ff = FoFiTrueType::make(font_data, font_data_len);
	} else {
	  ff = FoFiTrueType::load(fileNameC);
	}
	if (ff) {
	  if (ff->isOpenTypeCFF()) {
	    codeToGID = ff->getCIDToGIDMap((int *)&codeToGIDLen);
	  }
	  delete ff;
	}
      }
    }
    if (! _ft_new_face (lib, fileNameC, font_data, font_data_len, &face, &font_face)) {
      error(errSyntaxError, -1, "could not create cid (OT) face\n");
      goto err2;
    }
    break;

  default:
    fprintf (stderr, "font type %d not handled\n", (int)fontType);
    goto err2;
    break;
  }

  delete fontLoc;
  return new CairoFreeTypeFont(ref,
		       font_face,
		       codeToGID, codeToGIDLen,
		       substitute);

 err2:
  /* hmm? */
  delete fontLoc;
  gfree (codeToGID);
  gfree (font_data);
  fprintf (stderr, "some font thing failed\n");
  return nullptr;
}

//------------------------------------------------------------------------
// CairoType3Font
//------------------------------------------------------------------------

static const cairo_user_data_key_t type3_font_key = {0};

typedef struct _type3_font_info {
  GfxFont *font;
  PDFDoc *doc;
  CairoFontEngine *fontEngine;
  GBool printing;
  XRef *xref;
} type3_font_info_t;

static void
_free_type3_font_info(void *closure)
{
  type3_font_info_t *info = (type3_font_info_t *) closure;

  info->font->decRefCnt();
  free (info);
}

static cairo_status_t
_init_type3_glyph (cairo_scaled_font_t  *scaled_font,
                   cairo_t              *cr,
                   cairo_font_extents_t *extents)
{
  type3_font_info_t *info;
  GfxFont *font;
  double *mat;

  info = (type3_font_info_t *)
    cairo_font_face_get_user_data (cairo_scaled_font_get_font_face (scaled_font),
				   &type3_font_key);
  font = info->font;
  mat = font->getFontBBox();
  extents->ascent = mat[3]; /* y2 */
  extents->descent = -mat[3]; /* -y1 */
  extents->height = extents->ascent + extents->descent;
  extents->max_x_advance = mat[2] - mat[1]; /* x2 - x1 */
  extents->max_y_advance = 0;

  return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_render_type3_glyph (cairo_scaled_font_t  *scaled_font,
		     unsigned long         glyph,
		     cairo_t              *cr,
		     cairo_text_extents_t *metrics)
{
  Dict *charProcs;
  Object charProc;
  CairoOutputDev *output_dev;
  cairo_matrix_t matrix, invert_y_axis;
  double *mat;
  double wx, wy;
  PDFRectangle box;
  type3_font_info_t *info;
  GfxFont *font;
  Dict *resDict;
  Gfx *gfx;

  info = (type3_font_info_t *)
    cairo_font_face_get_user_data (cairo_scaled_font_get_font_face (scaled_font),
				   &type3_font_key);

  font = info->font;
  resDict = ((Gfx8BitFont *)font)->getResources();
  charProcs = ((Gfx8BitFont *)(info->font))->getCharProcs();
  if (!charProcs)
    return CAIRO_STATUS_USER_FONT_ERROR;

  if ((int)glyph >= charProcs->getLength())
    return CAIRO_STATUS_USER_FONT_ERROR;

  mat = font->getFontMatrix();
  matrix.xx = mat[0];
  matrix.yx = mat[1];
  matrix.xy = mat[2];
  matrix.yy = mat[3];
  matrix.x0 = mat[4];
  matrix.y0 = mat[5];
  cairo_matrix_init_scale (&invert_y_axis, 1, -1);
  cairo_matrix_multiply (&matrix, &matrix, &invert_y_axis);
  cairo_transform (cr, &matrix);

  output_dev = new CairoOutputDev();
  output_dev->setCairo(cr);
  output_dev->setPrinting(info->printing);

  mat = font->getFontBBox();
  box.x1 = mat[0];
  box.y1 = mat[1];
  box.x2 = mat[2];
  box.y2 = mat[3];
  gfx = new Gfx(info->doc, output_dev, resDict, &box, nullptr);
  output_dev->startDoc(info->doc, info->fontEngine);
  output_dev->startPage (1, gfx->getState(), gfx->getXRef());
  output_dev->setInType3Char(gTrue);
  charProc = charProcs->getVal(glyph);
  gfx->display(&charProc);

  output_dev->getType3GlyphWidth (&wx, &wy);
  cairo_matrix_transform_distance (&matrix, &wx, &wy);
  metrics->x_advance = wx;
  metrics->y_advance = wy;
  if (output_dev->hasType3GlyphBBox()) {
    double *bbox = output_dev->getType3GlyphBBox();

    cairo_matrix_transform_point (&matrix, &bbox[0], &bbox[1]);
    cairo_matrix_transform_point (&matrix, &bbox[2], &bbox[3]);
    metrics->x_bearing = bbox[0];
    metrics->y_bearing = bbox[1];
    metrics->width = bbox[2] - bbox[0];
    metrics->height = bbox[3] - bbox[1];
  }

  delete gfx;
  delete output_dev;

  return CAIRO_STATUS_SUCCESS;
}


CairoType3Font *CairoType3Font::create(GfxFont *gfxFont, PDFDoc *doc,
				       CairoFontEngine *fontEngine,
				       GBool printing, XRef *xref) {
  type3_font_info_t *info;
  cairo_font_face_t *font_face;
  Ref ref;
  int *codeToGID;
  Guint codeToGIDLen;
  int i, j;
  char **enc;
  Dict *charProcs;
  char *name;

  charProcs = ((Gfx8BitFont *)gfxFont)->getCharProcs();
  info = (type3_font_info_t *) malloc(sizeof(*info));
  ref = *gfxFont->getID();
  font_face = cairo_user_font_face_create();
  cairo_user_font_face_set_init_func (font_face, _init_type3_glyph);
  cairo_user_font_face_set_render_glyph_func (font_face, _render_type3_glyph);
  gfxFont->incRefCnt();
  info->font = gfxFont;
  info->doc = doc;
  info->fontEngine = fontEngine;
  info->printing = printing;
  info->xref = xref;

  cairo_font_face_set_user_data (font_face, &type3_font_key, (void *) info, _free_type3_font_info);

  enc = ((Gfx8BitFont *)gfxFont)->getEncoding();
  codeToGID = (int *)gmallocn(256, sizeof(int));
  codeToGIDLen = 256;
  for (i = 0; i < 256; ++i) {
    codeToGID[i] = 0;
    if (charProcs && (name = enc[i])) {
      for (j = 0; j < charProcs->getLength(); j++) {
	if (strcmp(name, charProcs->getKey(j)) == 0) {
	  codeToGID[i] = j;
	}
      }
    }
  }

  return new CairoType3Font(ref, doc, font_face, codeToGID, codeToGIDLen, printing, xref);
}

CairoType3Font::CairoType3Font(Ref refA,
                   PDFDoc *docA,
                   cairo_font_face_t *cairo_font_faceA,
                   int *codeToGIDA,
                   Guint codeToGIDLenA,
                   GBool printingA, XRef *xref) : CairoFont(refA,
                               cairo_font_faceA,
                               codeToGIDA,
                               codeToGIDLenA,
							   gFalse,
							   printingA),
                         doc(docA) { }

CairoType3Font::~CairoType3Font() { }

GBool
CairoType3Font::matches(Ref &other, GBool printingA) {
  return (other.num == ref.num && other.gen == ref.gen && printing == printingA);
}


//------------------------------------------------------------------------
// CairoFontEngine
//------------------------------------------------------------------------

CairoFontEngine::CairoFontEngine(FT_Library libA) {
  int i;

  lib = libA;
  for (i = 0; i < cairoFontCacheSize; ++i) {
    fontCache[i] = nullptr;
  }
  
  FT_Int major, minor, patch;
  // as of FT 2.1.8, CID fonts are indexed by CID instead of GID
  FT_Library_Version(lib, &major, &minor, &patch);
  useCIDs = major > 2 ||
            (major == 2 && (minor > 1 || (minor == 1 && patch > 7)));
#ifdef MULTITHREADED
  gInitMutex(&mutex);
#endif
}

CairoFontEngine::~CairoFontEngine() {
  int i;
  
  for (i = 0; i < cairoFontCacheSize; ++i) {
    if (fontCache[i])
      delete fontCache[i];
  }
#ifdef MULTITHREADED
  gDestroyMutex(&mutex);
#endif
}

CairoFont *
CairoFontEngine::getFont(GfxFont *gfxFont, PDFDoc *doc, GBool printing, XRef *xref) {
  int i, j;
  Ref ref;
  CairoFont *font;
  GfxFontType fontType;
  
  fontEngineLocker();
  ref = *gfxFont->getID();

  for (i = 0; i < cairoFontCacheSize; ++i) {
    font = fontCache[i];
    if (font && font->matches(ref, printing)) {
      for (j = i; j > 0; --j) {
	fontCache[j] = fontCache[j-1];
      }
      fontCache[0] = font;
      return font;
    }
  }
  
  fontType = gfxFont->getType();
  if (fontType == fontType3)
    font = CairoType3Font::create (gfxFont, doc, this, printing, xref);
  else
    font = CairoFreeTypeFont::create (gfxFont, xref, lib, useCIDs);

  //XXX: if font is null should we still insert it into the cache?
  if (fontCache[cairoFontCacheSize - 1]) {
    delete fontCache[cairoFontCacheSize - 1];
  }
  for (j = cairoFontCacheSize - 1; j > 0; --j) {
    fontCache[j] = fontCache[j-1];
  }
  fontCache[0] = font;
  return font;
}
