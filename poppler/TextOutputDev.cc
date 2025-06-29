//========================================================================
//
// TextOutputDev.cc
//
// Copyright 1997-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005-2007 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005 Nickolay V. Shmyrev <nshmyrev@yandex.ru>
// Copyright (C) 2006-2008, 2011-2013 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2006, 2007, 2013 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2006 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2007, 2008, 2012, 2017 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2008 Koji Otani <sho@bbr.jp>
// Copyright (C) 2008, 2010-2012, 2014-2018 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2008 Pino Toscano <pino@kde.org>
// Copyright (C) 2008, 2010 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2009 Ross Moore <ross@maths.mq.edu.au>
// Copyright (C) 2009 Kovid Goyal <kovid@kovidgoyal.net>
// Copyright (C) 2010 Brian Ewins <brian.ewins@gmail.com>
// Copyright (C) 2010 Marek Kasik <mkasik@redhat.com>
// Copyright (C) 2010 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2011 Sam Liao <phyomh@gmail.com>
// Copyright (C) 2012 Horst Prote <prote@fmi.uni-stuttgart.de>
// Copyright (C) 2012, 2013-2017 Jason Crain <jason@aquaticape.us>
// Copyright (C) 2012 Peter Breitenlohner <peb@mppmu.mpg.de>
// Copyright (C) 2013 José Aliste <jaliste@src.gnome.org>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2013 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2016 Khaled Hosny <khaledhosny@eglug.org>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <cmath>
#include <float.h>
#include <ctype.h>
#include <algorithm>
#ifdef _WIN32
#include <fcntl.h> // for O_BINARY
#include <io.h>    // for setmode
#endif
#include "goo/gmem.h"
#include "goo/GooString.h"
#include "goo/GooList.h"
#include "poppler-config.h"
#include "Error.h"
#include "GlobalParams.h"
#include "UnicodeMap.h"
#include "UnicodeTypeTable.h"
#include "Link.h"
#include "TextOutputDev.h"
#include "Page.h"
#include "Annot.h"
#include "UTF.h"

#ifdef MACOS
// needed for setting type/creator of MacOS files
#include "ICSupport.h"
#endif

//------------------------------------------------------------------------
// parameters
//------------------------------------------------------------------------

// Each bucket in a text pool includes baselines within a range of
// this many points.
#define textPoolStep 4

// Inter-character space width which will cause addChar to start a new
// word.
#define minWordBreakSpace 0.1

// Negative inter-character space width, i.e., overlap, which will
// cause addChar to start a new word.
#define minDupBreakOverlap 0.2

// Max distance between baselines of two lines within a block, as a
// fraction of the font size.
#define maxLineSpacingDelta 1.5

// Max difference in primary font sizes on two lines in the same
// block.  Delta1 is used when examining new lines above and below the
// current block; delta2 is used when examining text that overlaps the
// current block; delta3 is used when examining text to the left and
// right of the current block.
#define maxBlockFontSizeDelta1 0.05
#define maxBlockFontSizeDelta2 0.6
#define maxBlockFontSizeDelta3 0.2

// Max difference in font sizes inside a word.
#define maxWordFontSizeDelta 0.05

// Maximum distance between baselines of two words on the same line,
// e.g., distance between subscript or superscript and the primary
// baseline, as a fraction of the font size.
#define maxIntraLineDelta 0.5

// Minimum inter-word spacing, as a fraction of the font size.  (Only
// used for raw ordering.)
#define minWordSpacing 0.15

// Maximum inter-word spacing, as a fraction of the font size.
#define maxWordSpacing 1.5

// Maximum horizontal spacing which will allow a word to be pulled
// into a block.
#define minColSpacing1 0.3

// Minimum spacing between columns, as a fraction of the font size.
#define minColSpacing2 1.0

// Maximum vertical spacing between blocks within a flow, as a
// multiple of the font size.
#define maxBlockSpacing 2.5

// Minimum spacing between characters within a word, as a fraction of
// the font size.
#define minCharSpacing -0.5

// Maximum spacing between characters within a word, as a fraction of
// the font size, when there is no obvious extra-wide character
// spacing.
#define maxCharSpacing 0.03

// When extra-wide character spacing is detected, the inter-character
// space threshold is set to the minimum inter-character space
// multiplied by this constant.
#define maxWideCharSpacingMul 1.3

// Upper limit on spacing between characters in a word.
#define maxWideCharSpacing 0.4

// Max difference in primary,secondary coordinates (as a fraction of
// the font size) allowed for duplicated text (fake boldface, drop
// shadows) which is to be discarded.
#define dupMaxPriDelta 0.1
#define dupMaxSecDelta 0.2

// Max width of underlines (in points).
#define maxUnderlineWidth 3

// Min distance between baseline and underline (in points).
//~ this should be font-size-dependent
#define minUnderlineGap -2

// Max distance between baseline and underline (in points).
//~ this should be font-size-dependent
#define maxUnderlineGap 4

// Max horizontal distance between edge of word and start of underline
// (in points).
//~ this should be font-size-dependent
#define underlineSlack 1

// Max distance between edge of text and edge of link border
#define hyperlinkSlack 2

// Max distance between characters when combining a base character and
// combining character
#define combMaxMidDelta 0.3
#define combMaxBaseDelta 0.4

static int reorderText(Unicode *text, int len, UnicodeMap *uMap, GBool primaryLR, GooString *s, Unicode* u) {
  char lre[8], rle[8], popdf[8], buf[8];
  int lreLen = 0, rleLen = 0, popdfLen = 0, n;
  int nCols, i, j, k;

  nCols = 0;

  if (s) {
    lreLen = uMap->mapUnicode(0x202a, lre, sizeof(lre));
    rleLen = uMap->mapUnicode(0x202b, rle, sizeof(rle));
    popdfLen = uMap->mapUnicode(0x202c, popdf, sizeof(popdf));
  }

  if (primaryLR) {
    i = 0;
    while (i < len) {
      // output a left-to-right section
      for (j = i; j < len && !unicodeTypeR(text[j]); ++j) ;
      for (k = i; k < j; ++k) {
        if (s) {
          n = uMap->mapUnicode(text[k], buf, sizeof(buf));
          s->append(buf, n);
        }
        if (u) u[nCols] = text[k];
        ++nCols;
      }
      i = j;
      // output a right-to-left section
      for (j = i;
         j < len && !(unicodeTypeL(text[j]) || unicodeTypeNum(text[j]));
         ++j) ;
      if (j > i) {
        if (s) s->append(rle, rleLen);
        for (k = j - 1; k >= i; --k) {
          if (s) {
            n = uMap->mapUnicode(text[k], buf, sizeof(buf));
            s->append(buf, n);
          }
          if (u) u[nCols] = text[k];
          ++nCols;
        }
        if (s) s->append(popdf, popdfLen);
        i = j;
      }
    }
  } else {
    // Note: This code treats numeric characters (European and
    // Arabic/Indic) as left-to-right, which isn't strictly correct
    // (incurs extra LRE/POPDF pairs), but does produce correct
    // visual formatting.
    if (s) s->append(rle, rleLen);
    i = len - 1;
    while (i >= 0) {
      // output a right-to-left section
      for (j = i;
         j >= 0 && !(unicodeTypeL(text[j]) || unicodeTypeNum(text[j]));
         --j) ;
      for (k = i; k > j; --k) {
        if (s) {
          n = uMap->mapUnicode(text[k], buf, sizeof(buf));
          s->append(buf, n);
        }
        if (u) u[nCols] = text[k];
        ++nCols;
      }
      i = j;
      // output a left-to-right section
      for (j = i; j >= 0 && !unicodeTypeR(text[j]); --j) ;
      if (j < i) {
        if (s) s->append(lre, lreLen);
        for (k = j + 1; k <= i; ++k) {
          if (s) {
            n = uMap->mapUnicode(text[k], buf, sizeof(buf));
            s->append(buf, n);
          }
          if (u) u[nCols] = text[k];
          ++nCols;
        }
        if (s) s->append(popdf, popdfLen);
        i = j;
      }
    }
    if (s) s->append(popdf, popdfLen);
  }

  return nCols;
}

//------------------------------------------------------------------------
// TextUnderline
//------------------------------------------------------------------------

class TextUnderline {
public:

  TextUnderline(double x0A, double y0A, double x1A, double y1A)
    { x0 = x0A; y0 = y0A; x1 = x1A; y1 = y1A; horiz = y0 == y1; }
  ~TextUnderline() {}

  double x0, y0, x1, y1;
  GBool horiz;
};

//------------------------------------------------------------------------
// TextLink
//------------------------------------------------------------------------

class TextLink {
public:

  TextLink(int xMinA, int yMinA, int xMaxA, int yMaxA, AnnotLink *linkA)
    { xMin = xMinA; yMin = yMinA; xMax = xMaxA; yMax = yMaxA; link = linkA; }
  ~TextLink() {}

  int xMin, yMin, xMax, yMax;
  AnnotLink *link;
};

//------------------------------------------------------------------------
// TextFontInfo
//------------------------------------------------------------------------

TextFontInfo::TextFontInfo(GfxState *state) {
  gfxFont = state->getFont();
  if (gfxFont)
    gfxFont->incRefCnt();
#ifdef TEXTOUT_WORD_LIST
  fontName = (gfxFont && gfxFont->getName()) ? gfxFont->getName()->copy()
                                             : (GooString *)nullptr;
  flags = gfxFont ? gfxFont->getFlags() : 0;
#endif
}

TextFontInfo::~TextFontInfo() {
  if (gfxFont)
    gfxFont->decRefCnt();
#ifdef TEXTOUT_WORD_LIST
  if (fontName) {
    delete fontName;
  }
#endif
}

GBool TextFontInfo::matches(GfxState *state) {
  return state->getFont() == gfxFont;
}

GBool TextFontInfo::matches(TextFontInfo *fontInfo) {
  return gfxFont == fontInfo->gfxFont;
}

double TextFontInfo::getAscent() {
  return gfxFont ? gfxFont->getAscent() : 0.95;
}

double TextFontInfo::getDescent() {
  return gfxFont ? gfxFont->getDescent() : -0.35;
}

int TextFontInfo::getWMode() {
  return gfxFont ? gfxFont->getWMode() : 0;
}

//------------------------------------------------------------------------
// TextWord
//------------------------------------------------------------------------

TextWord::TextWord(GfxState *state, int rotA, double fontSizeA) {
  rot = rotA;
  fontSize = fontSizeA;
  text = nullptr;
  charcode = nullptr;
  edge = nullptr;
  charPos = nullptr;
  font = nullptr;
  textMat = nullptr;
  len = size = 0;
  spaceAfter = gFalse;
  next = nullptr;

#ifdef TEXTOUT_WORD_LIST
  GfxRGB rgb;

  if ((state->getRender() & 3) == 1) {
    state->getStrokeRGB(&rgb);
  } else {
    state->getFillRGB(&rgb);
  }
  colorR = colToDbl(rgb.r);
  colorG = colToDbl(rgb.g);
  colorB = colToDbl(rgb.b);
#endif

  underlined = gFalse;
  link = nullptr;
}

TextWord::~TextWord() {
  gfree(text);
  gfree(charcode);
  gfree(edge);
  gfree(charPos);
  gfree(font);
  gfree(textMat);
}

void TextWord::addChar(GfxState *state, TextFontInfo *fontA, double x, double y,
		       double dx, double dy, int charPosA, int charLen,
		       CharCode c, Unicode u, const Matrix &textMatA) {
  ensureCapacity(len+1);
  text[len] = u;
  charcode[len] = c;
  charPos[len] = charPosA;
  charPos[len + 1] = charPosA + charLen;
  font[len] = fontA;
  textMat[len] = textMatA;

  if (len == 0)
    setInitialBounds(fontA, x, y);

  if (wMode) { // vertical writing mode
    // NB: the rotation value has been incremented by 1 (in
    // TextPage::beginWord()) for vertical writing mode
    switch (rot) {
    case 0:
      edge[len] = x - fontSize;
      xMax = edge[len+1] = x;
      break;
    case 1:
      edge[len] = y - fontSize;
      yMax = edge[len+1] = y;
      break;
    case 2:
      edge[len] = x + fontSize;
      xMin = edge[len+1] = x;
      break;
    case 3:
      edge[len] = y + fontSize;
      yMin = edge[len+1] = y;
      break;
    }
  } else { // horizontal writing mode
    switch (rot) {
    case 0:
      edge[len] = x;
      xMax = edge[len+1] = x + dx;
      break;
    case 1:
      edge[len] = y;
      yMax = edge[len+1] = y + dy;
      break;
    case 2:
      edge[len] = x;
      xMin = edge[len+1] = x + dx;
      break;
    case 3:
      edge[len] = y;
      yMin = edge[len+1] = y + dy;
      break;
   }
  }
  ++len;
}

void TextWord::setInitialBounds(TextFontInfo *fontA, double x, double y) {
  double ascent = fontA->getAscent() * fontSize;
  double descent = fontA->getDescent() * fontSize;
  wMode = fontA->getWMode();

  if (wMode) { // vertical writing mode
    // NB: the rotation value has been incremented by 1 (in
    // TextPage::beginWord()) for vertical writing mode
    switch (rot) {
    case 0:
      xMin = x - fontSize;
      yMin = y - fontSize;
      yMax = y;
      base = y;
      break;
    case 1:
      xMin = x;
      yMin = y - fontSize;
      xMax = x + fontSize;
      base = x;
      break;
    case 2:
      yMin = y;
      xMax = x + fontSize;
      yMax = y + fontSize;
      base = y;
      break;
    case 3:
      xMin = x - fontSize;
      xMax = x;
      yMax = y + fontSize;
      base = x;
      break;
    }
  } else { // horizontal writing mode
    switch (rot) {
    case 0:
      xMin = x;
      yMin = y - ascent;
      yMax = y - descent;
      if (yMin == yMax) {
	// this is a sanity check for a case that shouldn't happen -- but
	// if it does happen, we want to avoid dividing by zero later
	yMin = y;
	yMax = y + 1;
      }
      base = y;
      break;
    case 1:
      xMin = x + descent;
      yMin = y;
      xMax = x + ascent;
      if (xMin == xMax) {
	// this is a sanity check for a case that shouldn't happen -- but
	// if it does happen, we want to avoid dividing by zero later
	xMin = x;
	xMax = x + 1;
      }
      base = x;
      break;
    case 2:
      yMin = y + descent;
      xMax = x;
      yMax = y + ascent;
      if (yMin == yMax) {
	// this is a sanity check for a case that shouldn't happen -- but
	// if it does happen, we want to avoid dividing by zero later
	yMin = y;
	yMax = y + 1;
      }
      base = y;
      break;
    case 3:
      xMin = x - ascent;
      xMax = x - descent;
      yMax = y;
      if (xMin == xMax) {
	// this is a sanity check for a case that shouldn't happen -- but
	// if it does happen, we want to avoid dividing by zero later
	xMin = x;
	xMax = x + 1;
      }
      base = x;
      break;
    }
  }
}

void TextWord::ensureCapacity(int capacity) {
  if (capacity > size) {
    size = std::max(size + 16, capacity);
    text = (Unicode *)greallocn(text, size, sizeof(Unicode));
    charcode = (CharCode *)greallocn(charcode, (size + 1), sizeof(CharCode));
    edge = (double *)greallocn(edge, (size + 1), sizeof(double));
    charPos = (int *)greallocn(charPos, size + 1, sizeof(int));
    font = (TextFontInfo **)greallocn(font, size, sizeof(TextFontInfo *));
    textMat = (Matrix *)greallocn(textMat, size, sizeof(Matrix));
  }
}

struct CombiningTable {
  Unicode base;
  Unicode comb;
};

static struct CombiningTable combiningTable[] = {
  {0x0060, 0x0300}, // grave
  {0x00a8, 0x0308}, // dieresis
  {0x00af, 0x0304}, // macron
  {0x00b4, 0x0301}, // acute
  {0x00b8, 0x0327}, // cedilla
  {0x02c6, 0x0302}, // circumflex
  {0x02c7, 0x030c}, // caron
  {0x02d8, 0x0306}, // breve
  {0x02d9, 0x0307}, // dotaccent
  {0x02da, 0x030a}, // ring
  {0x02dc, 0x0303}, // tilde
  {0x02dd, 0x030b}  // hungarumlaut (double acute accent)
};

// returning combining versions of characters
static Unicode getCombiningChar(Unicode u) {
  int len = sizeof(combiningTable) / sizeof(combiningTable[0]);
  for (int i = 0; i < len; ++i) {
    if (u == combiningTable[i].base)
      return combiningTable[i].comb;
  }
  return 0;
}

GBool TextWord::addCombining(GfxState *state, TextFontInfo *fontA, double fontSizeA, double x, double y,
			     double dx, double dy, int charPosA, int charLen,
			     CharCode c, Unicode u, const Matrix &textMatA) {
  if (len == 0 || wMode != 0 || fontA->getWMode() != 0)
    return gFalse;

  Unicode cCurrent = getCombiningChar(u);
  Unicode cPrev = getCombiningChar(text[len-1]);
  double edgeMid = (edge[len-1] + edge[len]) / 2;
  double charMid, maxScaledMidDelta, charBase, maxScaledBaseDelta;

  if (cCurrent != 0 && unicodeTypeAlphaNum(text[len-1])) {
    // Current is a combining character, previous is base character
    maxScaledMidDelta = fabs(edge[len] - edge[len-1]) * combMaxMidDelta;
    charMid = charBase = maxScaledBaseDelta = 0;

    // Test if characters overlap
    if (rot == 0 || rot == 2) {
      charMid = x + (dx / 2);
      charBase = y;
      maxScaledBaseDelta = (yMax - yMin) * combMaxBaseDelta;
    } else {
      charMid = y + (dy / 2);
      charBase = x;
      maxScaledBaseDelta = (xMax - xMin) * combMaxBaseDelta;
    }

    if (fabs(charMid - edgeMid) >= maxScaledMidDelta ||
	fabs(charBase - base) >= maxScaledBaseDelta)
      return gFalse;

    // Add character, but don't adjust edge / bounding box because
    // combining character's positioning could be odd.
    ensureCapacity(len+1);
    text[len] = cCurrent;
    charcode[len] = c;
    charPos[len] = charPosA;
    charPos[len+1] = charPosA + charLen;
    font[len] = fontA;
    textMat[len] = textMatA;
    edge[len+1] = edge[len];
    edge[len] = (edge[len+1] + edge[len-1]) / 2;
    ++len;
    return gTrue;
  }

  if (cPrev != 0 && unicodeTypeAlphaNum(u)) {
    // Previous is a combining character, current is base character
    maxScaledBaseDelta = (fontA->getAscent() - fontA->getDescent()) * fontSizeA * combMaxBaseDelta;
    charMid = charBase = maxScaledMidDelta = 0;

    // Test if characters overlap
    if (rot == 0 || rot == 2) {
      charMid = x + (dx / 2);
      charBase = y;
      maxScaledMidDelta = fabs(dx * combMaxMidDelta);
    } else {
      charMid = y + (dy / 2);
      charBase = x;
      maxScaledMidDelta = fabs(dy * combMaxMidDelta);
    }

    if (fabs(charMid - edgeMid) >= maxScaledMidDelta ||
	fabs(charBase - base) >= maxScaledBaseDelta)
      return gFalse;

    // move combining character to after base character
    ensureCapacity(len+1);
    fontSize = fontSizeA;
    text[len] = cPrev;
    charcode[len] = charcode[len-1];
    charPos[len] = charPosA;
    charPos[len+1] = charPosA + charLen;
    font[len] = font[len-1];
    textMat[len] = textMat[len-1];

    text[len-1] = u;
    charcode[len-1] = c;
    font[len-1] = fontA;
    textMat[len-1] = textMatA;

    if (len == 1)
      setInitialBounds(fontA, x, y);

    // Updated edges / bounding box because we changed the base
    // character.
    if (wMode) {
      switch (rot) {
      case 0:
	edge[len-1] = x - fontSize;
	xMax = edge[len+1] = x;
	break;
      case 1:
	edge[len-1] = y - fontSize;
	yMax = edge[len+1] = y;
	break;
      case 2:
	edge[len-1] = x + fontSize;
	xMin = edge[len+1] = x;
	break;
      case 3:
	edge[len-1] = y + fontSize;
	yMin = edge[len+1] = y;
	break;
      }
    } else {
      switch (rot) {
      case 0:
	edge[len-1] = x;
	xMax = edge[len+1] = x + dx;
	break;
      case 1:
	edge[len-1] = y;
	yMax = edge[len+1] = y + dy;
	break;
      case 2:
	edge[len-1] = x;
	xMin = edge[len+1] = x + dx;
	break;
      case 3:
	edge[len-1] = y;
	yMin = edge[len+1] = y + dy;
	break;
      }
    }

    edge[len] = (edge[len+1] + edge[len-1]) / 2;
    ++len;
    return gTrue;
  }
  return gFalse;
}

void TextWord::merge(TextWord *word) {
  int i;

  if (word->xMin < xMin) {
    xMin = word->xMin;
  }
  if (word->yMin < yMin) {
    yMin = word->yMin;
  }
  if (word->xMax > xMax) {
    xMax = word->xMax;
  }
  if (word->yMax > yMax) {
    yMax = word->yMax;
  }
  ensureCapacity(len + word->len);
  for (i = 0; i < word->len; ++i) {
    text[len + i] = word->text[i];
    charcode[len + i] = word->charcode[i];
    edge[len + i] = word->edge[i];
    charPos[len + i] = word->charPos[i];
    font[len + i] = word->font[i];
    textMat[len + i] = word->textMat[i];
  }
  edge[len + word->len] = word->edge[word->len];
  charPos[len + word->len] = word->charPos[word->len];
  len += word->len;
}

inline int TextWord::primaryCmp(TextWord *word) {
  double cmp;

  cmp = 0; // make gcc happy
  switch (rot) {
  case 0:
    cmp = xMin - word->xMin;
    break;
  case 1:
    cmp = yMin - word->yMin;
    break;
  case 2:
    cmp = word->xMax - xMax;
    break;
  case 3:
    cmp = word->yMax - yMax;
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

double TextWord::primaryDelta(TextWord *word) {
  double delta;

  delta = 0; // make gcc happy
  switch (rot) {
  case 0:
    delta = word->xMin - xMax;
    break;
  case 1:
    delta = word->yMin - yMax;
    break;
  case 2:
    delta = xMin - word->xMax;
    break;
  case 3:
    delta = yMin - word->yMax;
    break;
  }
  return delta;
}

int TextWord::cmpYX(const void *p1, const void *p2) {
  TextWord *word1 = *(TextWord **)p1;
  TextWord *word2 = *(TextWord **)p2;
  double cmp;

  cmp = word1->yMin - word2->yMin;
  if (cmp == 0) {
    cmp = word1->xMin - word2->xMin;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

#ifdef TEXTOUT_WORD_LIST

GooString *TextWord::getText() {
  GooString *s;
  UnicodeMap *uMap;
  char buf[8];
  int n, i;

  s = new GooString();
  if (!(uMap = globalParams->getTextEncoding())) {
    return s;
  }
  for (i = 0; i < len; ++i) {
    n = uMap->mapUnicode(text[i], buf, sizeof(buf));
    s->append(buf, n);
  }
  uMap->decRefCnt();
  return s;
}

void TextWord::getCharBBox(int charIdx, double *xMinA, double *yMinA,
			   double *xMaxA, double *yMaxA) {
  if (charIdx < 0 || charIdx >= len) {
    return;
  }
  switch (rot) {
  case 0:
    *xMinA = edge[charIdx];
    *xMaxA = edge[charIdx + 1];
    *yMinA = yMin;
    *yMaxA = yMax;
    break;
  case 1:
    *xMinA = xMin;
    *xMaxA = xMax;
    *yMinA = edge[charIdx];
    *yMaxA = edge[charIdx + 1];
    break;
  case 2:
    *xMinA = edge[charIdx + 1];
    *xMaxA = edge[charIdx];
    *yMinA = yMin;
    *yMaxA = yMax;
    break;
  case 3:
    *xMinA = xMin;
    *xMaxA = xMax;
    *yMinA = edge[charIdx + 1];
    *yMaxA = edge[charIdx];
    break;
  }
}

#endif // TEXTOUT_WORD_LIST

//------------------------------------------------------------------------
// TextPool
//------------------------------------------------------------------------

TextPool::TextPool() {
  minBaseIdx = 0;
  maxBaseIdx = -1;
  pool = nullptr;
  cursor = nullptr;
  cursorBaseIdx = -1;
}

TextPool::~TextPool() {
  int baseIdx;
  TextWord *word, *word2;

  for (baseIdx = minBaseIdx; baseIdx <= maxBaseIdx; ++baseIdx) {
    for (word = pool[baseIdx - minBaseIdx]; word; word = word2) {
      word2 = word->next;
      delete word;
    }
  }
  gfree(pool);
}

int TextPool::getBaseIdx(double base) {
  int baseIdx;

  baseIdx = (int)(base / textPoolStep);
  if (baseIdx < minBaseIdx) {
    return minBaseIdx;
  }
  if (baseIdx > maxBaseIdx) {
    return maxBaseIdx;
  }
  return baseIdx;
}

void TextPool::addWord(TextWord *word) {
  TextWord **newPool;
  int wordBaseIdx, newMinBaseIdx, newMaxBaseIdx, baseIdx;
  TextWord *w0, *w1;

  // expand the array if needed
  wordBaseIdx = (int)(word->base / textPoolStep);
  if (unlikely(wordBaseIdx <= INT_MIN + 128 || wordBaseIdx >= INT_MAX - 128)) {
      error(errSyntaxWarning, -1, "wordBaseIdx out of range");
      delete word;
      return;
  }
  if (minBaseIdx > maxBaseIdx) {
    minBaseIdx = wordBaseIdx - 128;
    maxBaseIdx = wordBaseIdx + 128;
    pool = (TextWord **)gmallocn(maxBaseIdx - minBaseIdx + 1,
				 sizeof(TextWord *));
    for (baseIdx = minBaseIdx; baseIdx <= maxBaseIdx; ++baseIdx) {
      pool[baseIdx - minBaseIdx] = nullptr;
    }
  } else if (wordBaseIdx < minBaseIdx) {
    newMinBaseIdx = wordBaseIdx - 128;
    newPool = (TextWord **)gmallocn(maxBaseIdx - newMinBaseIdx + 1,
				    sizeof(TextWord *));
    for (baseIdx = newMinBaseIdx; baseIdx < minBaseIdx; ++baseIdx) {
      newPool[baseIdx - newMinBaseIdx] = nullptr;
    }
    memcpy(&newPool[minBaseIdx - newMinBaseIdx], pool,
	   (maxBaseIdx - minBaseIdx + 1) * sizeof(TextWord *));
    gfree(pool);
    pool = newPool;
    minBaseIdx = newMinBaseIdx;
  } else if (wordBaseIdx > maxBaseIdx) {
    newMaxBaseIdx = wordBaseIdx + 128;
    pool = (TextWord **)greallocn(pool, newMaxBaseIdx - minBaseIdx + 1,
				  sizeof(TextWord *));
    for (baseIdx = maxBaseIdx + 1; baseIdx <= newMaxBaseIdx; ++baseIdx) {
      pool[baseIdx - minBaseIdx] = nullptr;
    }
    maxBaseIdx = newMaxBaseIdx;
  }

  // insert the new word
  if (cursor && wordBaseIdx == cursorBaseIdx &&
      word->primaryCmp(cursor) >= 0) {
    w0 = cursor;
    w1 = cursor->next;
  } else {
    w0 = nullptr;
    w1 = pool[wordBaseIdx - minBaseIdx];
  }
  for (; w1 && word->primaryCmp(w1) > 0; w0 = w1, w1 = w1->next) ;
  word->next = w1;
  if (w0) {
    w0->next = word;
  } else {
    pool[wordBaseIdx - minBaseIdx] = word;
  }
  cursor = word;
  cursorBaseIdx = wordBaseIdx;
}

//------------------------------------------------------------------------
// TextLine
//------------------------------------------------------------------------

TextLine::TextLine(TextBlock *blkA, int rotA, double baseA) {
  blk = blkA;
  rot = rotA;
  base = baseA;
  words = lastWord = nullptr;
  text = nullptr;
  edge = nullptr;
  col = nullptr;
  len = 0;
  convertedLen = 0;
  hyphenated = gFalse;
  next = nullptr;
  xMin = yMin = 0;
  xMax = yMax = -1;
  normalized = nullptr;
  normalized_len = 0;
  normalized_idx = nullptr;
}

TextLine::~TextLine() {
  TextWord *word;

  while (words) {
    word = words;
    words = words->next;
    delete word;
  }
  gfree(text);
  gfree(edge);
  gfree(col);
  if (normalized) {
    gfree(normalized);
    gfree(normalized_idx);
  }
}

void TextLine::addWord(TextWord *word) {
  if (lastWord) {
    lastWord->next = word;
  } else {
    words = word;
  }
  lastWord = word;

  if (xMin > xMax) {
    xMin = word->xMin;
    xMax = word->xMax;
    yMin = word->yMin;
    yMax = word->yMax;
  } else {
    if (word->xMin < xMin) {
      xMin = word->xMin;
    }
    if (word->xMax > xMax) {
      xMax = word->xMax;
    }
    if (word->yMin < yMin) {
      yMin = word->yMin;
    }
    if (word->yMax > yMax) {
      yMax = word->yMax;
    }
  }
}

double TextLine::primaryDelta(TextLine *line) {
  double delta;

  delta = 0; // make gcc happy
  switch (rot) {
  case 0:
    delta = line->xMin - xMax;
    break;
  case 1:
    delta = line->yMin - yMax;
    break;
  case 2:
    delta = xMin - line->xMax;
    break;
  case 3:
    delta = yMin - line->yMax;
    break;
  }
  return delta;
}

int TextLine::primaryCmp(TextLine *line) {
  double cmp;

  cmp = 0; // make gcc happy
  switch (rot) {
  case 0:
    cmp = xMin - line->xMin;
    break;
  case 1:
    cmp = yMin - line->yMin;
    break;
  case 2:
    cmp = line->xMax - xMax;
    break;
  case 3:
    cmp = line->yMax - yMax;
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLine::secondaryCmp(TextLine *line) {
  double cmp;

  cmp = (rot == 0 || rot == 3) ? base - line->base : line->base - base;
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLine::cmpYX(TextLine *line) {
  int cmp;

  if ((cmp = secondaryCmp(line))) {
    return cmp;
  }
  return primaryCmp(line);
}

int TextLine::cmpXY(const void *p1, const void *p2) {
  TextLine *line1 = *(TextLine **)p1;
  TextLine *line2 = *(TextLine **)p2;
  int cmp;

  if ((cmp = line1->primaryCmp(line2))) {
    return cmp;
  }
  return line1->secondaryCmp(line2);
}

void TextLine::coalesce(UnicodeMap *uMap) {
  TextWord *word0, *word1;
  double space, delta, minSpace;
  GBool isUnicode;
  char buf[8];
  int i, j;

  if (words->next) {

    // compute the inter-word space threshold
    if (words->len > 1 || words->next->len > 1) {
      minSpace = 0;
    } else {
      minSpace = words->primaryDelta(words->next);
      for (word0 = words->next, word1 = word0->next;
	   word1 && minSpace > 0;
	   word0 = word1, word1 = word0->next) {
	if (word1->len > 1) {
	  minSpace = 0;
	}
	delta = word0->primaryDelta(word1);
	if (delta < minSpace) {
	  minSpace = delta;
	}
      }
    }
    if (minSpace <= 0) {
      space = maxCharSpacing * words->fontSize;
    } else {
      space = maxWideCharSpacingMul * minSpace;
      if (space > maxWideCharSpacing * words->fontSize) {
	space = maxWideCharSpacing * words->fontSize;
      }
    }

    // merge words
    word0 = words;
    word1 = words->next;
    while (word1) {
      if (word0->primaryDelta(word1) >= space) {
	word0->spaceAfter = gTrue;
	word0 = word1;
	word1 = word1->next;
      } else if (word0->font[word0->len - 1] == word1->font[0] &&
		 word0->underlined == word1->underlined &&
		 fabs(word0->fontSize - word1->fontSize) <
		   maxWordFontSizeDelta * words->fontSize &&
		 word1->charPos[0] == word0->charPos[word0->len]) {
	word0->merge(word1);
	word0->next = word1->next;
	delete word1;
	word1 = word0->next;
      } else {
	word0 = word1;
	word1 = word1->next;
      }
    }
  }

  // build the line text
  isUnicode = uMap ? uMap->isUnicode() : gFalse;
  len = 0;
  for (word1 = words; word1; word1 = word1->next) {
    len += word1->len;
    if (word1->spaceAfter) {
      ++len;
    }
  }
  text = (Unicode *)gmallocn(len, sizeof(Unicode));
  edge = (double *)gmallocn(len + 1, sizeof(double));
  i = 0;
  for (word1 = words; word1; word1 = word1->next) {
    for (j = 0; j < word1->len; ++j) {
      text[i] = word1->text[j];
      edge[i] = word1->edge[j];
      ++i;
    }
    edge[i] = word1->edge[word1->len];
    if (word1->spaceAfter) {
      text[i] = (Unicode)0x0020;
      ++i;
    }
  }

  // compute convertedLen and set up the col array
  col = (int *)gmallocn(len + 1, sizeof(int));
  convertedLen = 0;
  for (i = 0; i < len; ++i) {
    col[i] = convertedLen;
    if (isUnicode) {
      ++convertedLen;
    } else if (uMap) {
      convertedLen += uMap->mapUnicode(text[i], buf, sizeof(buf));
    }
  }
  col[len] = convertedLen;

  // check for hyphen at end of line
  //~ need to check for other chars used as hyphens
  hyphenated = text[len - 1] == (Unicode)'-';
}

//------------------------------------------------------------------------
// TextLineFrag
//------------------------------------------------------------------------

class TextLineFrag {
public:

  TextLine *line;		// the line object
  int start, len;		// offset and length of this fragment
				//   (in Unicode chars)
  double xMin, xMax;		// bounding box coordinates
  double yMin, yMax;
  double base;			// baseline virtual coordinate
  int col;			// first column

  void init(TextLine *lineA, int startA, int lenA);
  void computeCoords(GBool oneRot);

  static int cmpYXPrimaryRot(const void *p1, const void *p2);
  static int cmpYXLineRot(const void *p1, const void *p2);
  static int cmpXYLineRot(const void *p1, const void *p2);
  static int cmpXYColumnPrimaryRot(const void *p1, const void *p2);
  static int cmpXYColumnLineRot(const void *p1, const void *p2);
};

void TextLineFrag::init(TextLine *lineA, int startA, int lenA) {
  line = lineA;
  start = startA;
  len = lenA;
  col = line->col[start];
}

void TextLineFrag::computeCoords(GBool oneRot) {
  TextBlock *blk;
  double d0, d1, d2, d3, d4;

  if (oneRot) {

    switch (line->rot) {
    case 0:
      xMin = line->edge[start];
      xMax = line->edge[start + len];
      yMin = line->yMin;
      yMax = line->yMax;
      break;
    case 1:
      xMin = line->xMin;
      xMax = line->xMax;
      yMin = line->edge[start];
      yMax = line->edge[start + len];
      break;
    case 2:
      xMin = line->edge[start + len];
      xMax = line->edge[start];
      yMin = line->yMin;
      yMax = line->yMax;
      break;
    case 3:
      xMin = line->xMin;
      xMax = line->xMax;
      yMin = line->edge[start + len];
      yMax = line->edge[start];
      break;
    }
    base = line->base;

  } else {

    if (line->rot == 0 && line->blk->page->primaryRot == 0) {

      xMin = line->edge[start];
      xMax = line->edge[start + len];
      yMin = line->yMin;
      yMax = line->yMax;
      base = line->base;

    } else {

      blk = line->blk;
      d0 = line->edge[start];
      d1 = line->edge[start + len];
      d2 = d3 = d4 = 0; // make gcc happy

      switch (line->rot) {
      case 0:
	d2 = line->yMin;
	d3 = line->yMax;
	d4 = line->base;
	d0 = (d0 - blk->xMin) / (blk->xMax - blk->xMin);
	d1 = (d1 - blk->xMin) / (blk->xMax - blk->xMin);
	d2 = (d2 - blk->yMin) / (blk->yMax - blk->yMin);
	d3 = (d3 - blk->yMin) / (blk->yMax - blk->yMin);
	d4 = (d4 - blk->yMin) / (blk->yMax - blk->yMin);
	break;
      case 1:
	d2 = line->xMax;
	d3 = line->xMin;
	d4 = line->base;
	d0 = (d0 - blk->yMin) / (blk->yMax - blk->yMin);
	d1 = (d1 - blk->yMin) / (blk->yMax - blk->yMin);
	d2 = (blk->xMax - d2) / (blk->xMax - blk->xMin);
	d3 = (blk->xMax - d3) / (blk->xMax - blk->xMin);
	d4 = (blk->xMax - d4) / (blk->xMax - blk->xMin);
	break;
      case 2:
	d2 = line->yMax;
	d3 = line->yMin;
	d4 = line->base;
	d0 = (blk->xMax - d0) / (blk->xMax - blk->xMin);
	d1 = (blk->xMax - d1) / (blk->xMax - blk->xMin);
	d2 = (blk->yMax - d2) / (blk->yMax - blk->yMin);
	d3 = (blk->yMax - d3) / (blk->yMax - blk->yMin);
	d4 = (blk->yMax - d4) / (blk->yMax - blk->yMin);
	break;
      case 3:
	d2 = line->xMin;
	d3 = line->xMax;
	d4 = line->base;
	d0 = (blk->yMax - d0) / (blk->yMax - blk->yMin);
	d1 = (blk->yMax - d1) / (blk->yMax - blk->yMin);
	d2 = (d2 - blk->xMin) / (blk->xMax - blk->xMin);
	d3 = (d3 - blk->xMin) / (blk->xMax - blk->xMin);
	d4 = (d4 - blk->xMin) / (blk->xMax - blk->xMin);
	break;
      }

      switch (line->blk->page->primaryRot) {
      case 0:
	xMin = blk->xMin + d0 * (blk->xMax - blk->xMin);
	xMax = blk->xMin + d1 * (blk->xMax - blk->xMin);
	yMin = blk->yMin + d2 * (blk->yMax - blk->yMin);
	yMax = blk->yMin + d3 * (blk->yMax - blk->yMin);
	base = blk->yMin + d4 * (blk->yMax - blk->yMin);
	break;
      case 1:
	xMin = blk->xMax - d3 * (blk->xMax - blk->xMin);
	xMax = blk->xMax - d2 * (blk->xMax - blk->xMin);
	yMin = blk->yMin + d0 * (blk->yMax - blk->yMin);
	yMax = blk->yMin + d1 * (blk->yMax - blk->yMin);
	base = blk->xMax - d4 * (blk->xMax - blk->xMin);
	break;
      case 2:
	xMin = blk->xMax - d1 * (blk->xMax - blk->xMin);
	xMax = blk->xMax - d0 * (blk->xMax - blk->xMin);
	yMin = blk->yMax - d3 * (blk->yMax - blk->yMin);
	yMax = blk->yMax - d2 * (blk->yMax - blk->yMin);
	base = blk->yMax - d4 * (blk->yMax - blk->yMin);
	break;
      case 3:
	xMin = blk->xMin + d2 * (blk->xMax - blk->xMin);
	xMax = blk->xMin + d3 * (blk->xMax - blk->xMin);
	yMin = blk->yMax - d1 * (blk->yMax - blk->yMin);
	yMax = blk->yMax - d0 * (blk->yMax - blk->yMin);
	base = blk->xMin + d4 * (blk->xMax - blk->xMin);
	break;
      }

    }
  }
}

int TextLineFrag::cmpYXPrimaryRot(const void *p1, const void *p2) {
  TextLineFrag *frag1 = (TextLineFrag *)p1;
  TextLineFrag *frag2 = (TextLineFrag *)p2;
  double cmp;

  cmp = 0; // make gcc happy
  switch (frag1->line->blk->page->primaryRot) {
  case 0:
    if (fabs(cmp = frag1->yMin - frag2->yMin) < 0.01) {
      cmp = frag1->xMin - frag2->xMin;
    }
    break;
  case 1:
    if (fabs(cmp = frag2->xMax - frag1->xMax) < 0.01) {
      cmp = frag1->yMin - frag2->yMin;
    }
    break;
  case 2:
    if (fabs(cmp = frag2->yMin - frag1->yMin) < 0.01) {
      cmp = frag2->xMax - frag1->xMax;
    }
    break;
  case 3:
    if (fabs(cmp = frag1->xMax - frag2->xMax) < 0.01) {
      cmp = frag2->yMax - frag1->yMax;
    }
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLineFrag::cmpYXLineRot(const void *p1, const void *p2) {
  TextLineFrag *frag1 = (TextLineFrag *)p1;
  TextLineFrag *frag2 = (TextLineFrag *)p2;
  double cmp;

  cmp = 0; // make gcc happy
  switch (frag1->line->rot) {
  case 0:
    if ((cmp = frag1->yMin - frag2->yMin) == 0) {
      cmp = frag1->xMin - frag2->xMin;
    }
    break;
  case 1:
    if ((cmp = frag2->xMax - frag1->xMax) == 0) {
      cmp = frag1->yMin - frag2->yMin;
    }
    break;
  case 2:
    if ((cmp = frag2->yMin - frag1->yMin) == 0) {
      cmp = frag2->xMax - frag1->xMax;
    }
    break;
  case 3:
    if ((cmp = frag1->xMax - frag2->xMax) == 0) {
      cmp = frag2->yMax - frag1->yMax;
    }
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLineFrag::cmpXYLineRot(const void *p1, const void *p2) {
  TextLineFrag *frag1 = (TextLineFrag *)p1;
  TextLineFrag *frag2 = (TextLineFrag *)p2;
  double cmp;

  cmp = 0; // make gcc happy
  switch (frag1->line->rot) {
  case 0:
    if ((cmp = frag1->xMin - frag2->xMin) == 0) {
      cmp = frag1->yMin - frag2->yMin;
    }
    break;
  case 1:
    if ((cmp = frag1->yMin - frag2->yMin) == 0) {
      cmp = frag2->xMax - frag1->xMax;
    }
    break;
  case 2:
    if ((cmp = frag2->xMax - frag1->xMax) == 0) {
      cmp = frag2->yMin - frag1->yMin;
    }
    break;
  case 3:
    if ((cmp = frag2->yMax - frag1->yMax) == 0) {
      cmp = frag1->xMax - frag2->xMax;
    }
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLineFrag::cmpXYColumnPrimaryRot(const void *p1, const void *p2) {
  TextLineFrag *frag1 = (TextLineFrag *)p1;
  TextLineFrag *frag2 = (TextLineFrag *)p2;
  double cmp;

  // if columns overlap, compare y values
  if (frag1->col < frag2->col + (frag2->line->col[frag2->start + frag2->len] -
				 frag2->line->col[frag2->start]) &&
      frag2->col < frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start])) {
    cmp = 0; // make gcc happy
    switch (frag1->line->blk->page->primaryRot) {
    case 0: cmp = frag1->yMin - frag2->yMin; break;
    case 1: cmp = frag2->xMax - frag1->xMax; break;
    case 2: cmp = frag2->yMin - frag1->yMin; break;
    case 3: cmp = frag1->xMax - frag2->xMax; break;
    }
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
  }

  // otherwise, compare starting column
  return frag1->col - frag2->col;
}

int TextLineFrag::cmpXYColumnLineRot(const void *p1, const void *p2) {
  TextLineFrag *frag1 = (TextLineFrag *)p1;
  TextLineFrag *frag2 = (TextLineFrag *)p2;
  double cmp;

  // if columns overlap, compare y values
  if (frag1->col < frag2->col + (frag2->line->col[frag2->start + frag2->len] -
				 frag2->line->col[frag2->start]) &&
      frag2->col < frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start])) {
    cmp = 0; // make gcc happy
    switch (frag1->line->rot) {
    case 0: cmp = frag1->yMin - frag2->yMin; break;
    case 1: cmp = frag2->xMax - frag1->xMax; break;
    case 2: cmp = frag2->yMin - frag1->yMin; break;
    case 3: cmp = frag1->xMax - frag2->xMax; break;
    }
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
  }

  // otherwise, compare starting column
  return frag1->col - frag2->col;
}

//------------------------------------------------------------------------
// TextBlock
//------------------------------------------------------------------------

TextBlock::TextBlock(TextPage *pageA, int rotA) {
  page = pageA;
  rot = rotA;
  xMin = yMin = 0;
  xMax = yMax = -1;
  priMin = 0;
  priMax = page->pageWidth;
  pool = new TextPool();
  lines = nullptr;
  curLine = nullptr;
  next = nullptr;
  stackNext = nullptr;
  tableId = -1;
  tableEnd = gFalse;
}

TextBlock::~TextBlock() {
  TextLine *line;

  delete pool;
  while (lines) {
    line = lines;
    lines = lines->next;
    delete line;
  }
}

void TextBlock::addWord(TextWord *word) {
  pool->addWord(word);
  if (xMin > xMax) {
    xMin = word->xMin;
    xMax = word->xMax;
    yMin = word->yMin;
    yMax = word->yMax;
  } else {
    if (word->xMin < xMin) {
      xMin = word->xMin;
    }
    if (word->xMax > xMax) {
      xMax = word->xMax;
    }
    if (word->yMin < yMin) {
      yMin = word->yMin;
    }
    if (word->yMax > yMax) {
      yMax = word->yMax;
    }
  }
}

void TextBlock::coalesce(UnicodeMap *uMap, double fixedPitch) {
  TextWord *word0, *word1, *word2, *bestWord0, *bestWord1, *lastWord;
  TextLine *line, *line0, *line1;
  int poolMinBaseIdx, startBaseIdx, minBaseIdx, maxBaseIdx;
  int baseIdx, bestWordBaseIdx, idx0, idx1;
  double minBase, maxBase;
  double fontSize, wordSpacing, delta, priDelta, secDelta;
  TextLine **lineArray;
  GBool found, overlap;
  int col1, col2;
  int i, j, k;

  // discard duplicated text (fake boldface, drop shadows)
  for (idx0 = pool->minBaseIdx; idx0 <= pool->maxBaseIdx; ++idx0) {
    word0 = pool->getPool(idx0);
    while (word0) {
      priDelta = dupMaxPriDelta * word0->fontSize;
      secDelta = dupMaxSecDelta * word0->fontSize;
      maxBaseIdx = pool->getBaseIdx(word0->base + secDelta);
      found = gFalse;
      word1 = word2 = nullptr; // make gcc happy
      for (idx1 = idx0; idx1 <= maxBaseIdx; ++idx1) {
	if (idx1 == idx0) {
	  word1 = word0;
	  word2 = word0->next;
	} else {
	  word1 = nullptr;
	  word2 = pool->getPool(idx1);
	}
	for (; word2; word1 = word2, word2 = word2->next) {
	  if (word2->len == word0->len &&
	      !memcmp(word2->text, word0->text,
		      word0->len * sizeof(Unicode))) {
	    switch (rot) {
	    case 0:
	    case 2:
	      found = fabs(word0->xMin - word2->xMin) < priDelta &&
		      fabs(word0->xMax - word2->xMax) < priDelta &&
		      fabs(word0->yMin - word2->yMin) < secDelta &&
		      fabs(word0->yMax - word2->yMax) < secDelta;
	      break;
	    case 1:
	    case 3:
	      found = fabs(word0->xMin - word2->xMin) < secDelta &&
		      fabs(word0->xMax - word2->xMax) < secDelta &&
		      fabs(word0->yMin - word2->yMin) < priDelta &&
		      fabs(word0->yMax - word2->yMax) < priDelta;
	      break;
	    }
	  }
	  if (found) {
	    break;
	  }
	}
	if (found) {
	  break;
	}
      }
      if (found) {
	if (word1) {
	  word1->next = word2->next;
	} else {
	  pool->setPool(idx1, word2->next);
	}
	delete word2;
      } else {
	word0 = word0->next;
      }
    }
  }

  // build the lines
  curLine = nullptr;
  poolMinBaseIdx = pool->minBaseIdx;
  charCount = 0;
  nLines = 0;
  while (1) {

    // find the first non-empty line in the pool
    for (;
	 poolMinBaseIdx <= pool->maxBaseIdx && !pool->getPool(poolMinBaseIdx);
	 ++poolMinBaseIdx) ;
    if (poolMinBaseIdx > pool->maxBaseIdx) {
      break;
    }

    // look for the left-most word in the first four lines of the
    // pool -- this avoids starting with a superscript word
    startBaseIdx = poolMinBaseIdx;
    for (baseIdx = poolMinBaseIdx + 1;
	 baseIdx < poolMinBaseIdx + 4 && baseIdx <= pool->maxBaseIdx;
	 ++baseIdx) {
      if (!pool->getPool(baseIdx)) {
	continue;
      }
      if (pool->getPool(baseIdx)->primaryCmp(pool->getPool(startBaseIdx))
	  < 0) {
	startBaseIdx = baseIdx;
      }
    }

    // create a new line
    word0 = pool->getPool(startBaseIdx);
    pool->setPool(startBaseIdx, word0->next);
    word0->next = nullptr;
    line = new TextLine(this, word0->rot, word0->base);
    line->addWord(word0);
    lastWord = word0;

    // compute the search range
    fontSize = word0->fontSize;
    minBase = word0->base - maxIntraLineDelta * fontSize;
    maxBase = word0->base + maxIntraLineDelta * fontSize;
    minBaseIdx = pool->getBaseIdx(minBase);
    maxBaseIdx = pool->getBaseIdx(maxBase);
    wordSpacing = fixedPitch ? fixedPitch : maxWordSpacing * fontSize;

    // find the rest of the words in this line
    while (1) {

      // find the left-most word whose baseline is in the range for
      // this line
      bestWordBaseIdx = 0;
      bestWord0 = bestWord1 = nullptr;
      overlap = gFalse;
      for (baseIdx = minBaseIdx;
	   !overlap && baseIdx <= maxBaseIdx;
	   ++baseIdx) {
	for (word0 = nullptr, word1 = pool->getPool(baseIdx);
	     word1;
	     word0 = word1, word1 = word1->next) {
	  if (word1->base >= minBase &&
	      word1->base <= maxBase) {
	    delta = lastWord->primaryDelta(word1);
	    if (delta < minCharSpacing * fontSize) {
	      overlap = gTrue;
	      break;
	    } else {
	      if (delta < wordSpacing &&
		  (!bestWord1 || word1->primaryCmp(bestWord1) < 0)) {
		bestWordBaseIdx = baseIdx;
		bestWord0 = word0;
		bestWord1 = word1;
	      }
	      break;
	    }
	  }
	}
      }
      if (overlap || !bestWord1) {
	break;
      }

      // remove it from the pool, and add it to the line
      if (bestWord0) {
	bestWord0->next = bestWord1->next;
      } else {
	pool->setPool(bestWordBaseIdx, bestWord1->next);
      }
      bestWord1->next = nullptr;
      line->addWord(bestWord1);
      lastWord = bestWord1;
    }

    // add the line
    if (curLine && line->cmpYX(curLine) > 0) {
      line0 = curLine;
      line1 = curLine->next;
    } else {
      line0 = nullptr;
      line1 = lines;
    }
    for (;
	 line1 && line->cmpYX(line1) > 0;
	 line0 = line1, line1 = line1->next) ;
    if (line0) {
      line0->next = line;
    } else {
      lines = line;
    }
    line->next = line1;
    curLine = line;
    line->coalesce(uMap);
    charCount += line->len;
    ++nLines;
  }

  // sort lines into xy order for column assignment
  lineArray = (TextLine **)gmallocn(nLines, sizeof(TextLine *));
  for (line = lines, i = 0; line; line = line->next, ++i) {
    lineArray[i] = line;
  }
  qsort(lineArray, nLines, sizeof(TextLine *), &TextLine::cmpXY);

  // column assignment
  nColumns = 0;
  if (fixedPitch) {
    for (i = 0; i < nLines; ++i) {
      line0 = lineArray[i];
      col1 = 0; // make gcc happy
      switch (rot) {
      case 0:
	col1 = (int)((line0->xMin - xMin) / fixedPitch + 0.5);
	break;
      case 1:
	col1 = (int)((line0->yMin - yMin) / fixedPitch + 0.5);
	break;
      case 2:
	col1 = (int)((xMax - line0->xMax) / fixedPitch + 0.5);
	break;
      case 3:
	col1 = (int)((yMax - line0->yMax) / fixedPitch + 0.5);
	break;
      }
      for (k = 0; k <= line0->len; ++k) {
	line0->col[k] += col1;
      }
      if (line0->col[line0->len] > nColumns) {
	nColumns = line0->col[line0->len];
      }
    }
  } else {
    for (i = 0; i < nLines; ++i) {
      line0 = lineArray[i];
      col1 = 0;
      for (j = 0; j < i; ++j) {
	line1 = lineArray[j];
	if (line1->primaryDelta(line0) >= 0) {
	  col2 = line1->col[line1->len] + 1;
	} else {
	  k = 0; // make gcc happy
	  switch (rot) {
	  case 0:
	    for (k = 0;
		 k < line1->len &&
		   line0->xMin >= 0.5 * (line1->edge[k] + line1->edge[k+1]);
		 ++k) ;
	    break;
	  case 1:
	    for (k = 0;
		 k < line1->len &&
		   line0->yMin >= 0.5 * (line1->edge[k] + line1->edge[k+1]);
		 ++k) ;
	    break;
	  case 2:
	    for (k = 0;
		 k < line1->len &&
		   line0->xMax <= 0.5 * (line1->edge[k] + line1->edge[k+1]);
		 ++k) ;
	    break;
	  case 3:
	    for (k = 0;
		 k < line1->len &&
		   line0->yMax <= 0.5 * (line1->edge[k] + line1->edge[k+1]);
		 ++k) ;
	    break;
	  }
	  col2 = line1->col[k];
	}
	if (col2 > col1) {
	  col1 = col2;
	}
      }
      for (k = 0; k <= line0->len; ++k) {
	line0->col[k] += col1;
      }
      if (line0->col[line0->len] > nColumns) {
	nColumns = line0->col[line0->len];
      }
    }
  }
  gfree(lineArray);
}

void TextBlock::updatePriMinMax(TextBlock *blk) {
  double newPriMin, newPriMax;
  GBool gotPriMin, gotPriMax;

  gotPriMin = gotPriMax = gFalse;
  newPriMin = newPriMax = 0; // make gcc happy
  switch (page->primaryRot) {
  case 0:
  case 2:
    if (blk->yMin < yMax && blk->yMax > yMin) {
      if (blk->xMin < xMin) {
	newPriMin = blk->xMax;
	gotPriMin = gTrue;
      }
      if (blk->xMax > xMax) {
	newPriMax = blk->xMin;
	gotPriMax = gTrue;
      }
    }
    break;
  case 1:
  case 3:
    if (blk->xMin < xMax && blk->xMax > xMin) {
      if (blk->yMin < yMin) {
	newPriMin = blk->yMax;
	gotPriMin = gTrue;
      }
      if (blk->yMax > yMax) {
	newPriMax = blk->yMin;
	gotPriMax = gTrue;
      }
    }
    break;
  }
  if (gotPriMin) {
    if (newPriMin > xMin) {
      newPriMin = xMin;
    }
    if (newPriMin > priMin) {
      priMin = newPriMin;
    }
  }
  if (gotPriMax) {
    if (newPriMax < xMax) {
      newPriMax = xMax;
    }
    if (newPriMax < priMax) {
      priMax = newPriMax;
    }
  }
}

int TextBlock::cmpXYPrimaryRot(const void *p1, const void *p2) {
  TextBlock *blk1 = *(TextBlock **)p1;
  TextBlock *blk2 = *(TextBlock **)p2;
  double cmp;

  cmp = 0; // make gcc happy
  switch (blk1->page->primaryRot) {
  case 0:
    if ((cmp = blk1->xMin - blk2->xMin) == 0) {
      cmp = blk1->yMin - blk2->yMin;
    }
    break;
  case 1:
    if ((cmp = blk1->yMin - blk2->yMin) == 0) {
      cmp = blk2->xMax - blk1->xMax;
    }
    break;
  case 2:
    if ((cmp = blk2->xMax - blk1->xMax) == 0) {
      cmp = blk2->yMin - blk1->yMin;
    }
    break;
  case 3:
    if ((cmp = blk2->yMax - blk1->yMax) == 0) {
      cmp = blk1->xMax - blk2->xMax;
    }
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextBlock::cmpYXPrimaryRot(const void *p1, const void *p2) {
  TextBlock *blk1 = *(TextBlock **)p1;
  TextBlock *blk2 = *(TextBlock **)p2;
  double cmp;

  cmp = 0; // make gcc happy
  switch (blk1->page->primaryRot) {
  case 0:
    if ((cmp = blk1->yMin - blk2->yMin) == 0) {
      cmp = blk1->xMin - blk2->xMin;
    }
    break;
  case 1:
    if ((cmp = blk2->xMax - blk1->xMax) == 0) {
      cmp = blk1->yMin - blk2->yMin;
    }
    break;
  case 2:
    if ((cmp = blk2->yMin - blk1->yMin) == 0) {
      cmp = blk2->xMax - blk1->xMax;
    }
    break;
  case 3:
    if ((cmp = blk1->xMax - blk2->xMax) == 0) {
      cmp = blk2->yMax - blk1->yMax;
    }
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextBlock::primaryCmp(TextBlock *blk) {
  double cmp;

  cmp = 0; // make gcc happy
  switch (rot) {
  case 0:
    cmp = xMin - blk->xMin;
    break;
  case 1:
    cmp = yMin - blk->yMin;
    break;
  case 2:
    cmp = blk->xMax - xMax;
    break;
  case 3:
    cmp = blk->yMax - yMax;
    break;
  }
  return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

double TextBlock::secondaryDelta(TextBlock *blk) {
  double delta;

  delta = 0; // make gcc happy
  switch (rot) {
  case 0:
    delta = blk->yMin - yMax;
    break;
  case 1:
    delta = xMin - blk->xMax;
    break;
  case 2:
    delta = yMin - blk->yMax;
    break;
  case 3:
    delta = blk->xMin - xMax;
    break;
  }
  return delta;
}

GBool TextBlock::isBelow(TextBlock *blk) {
  GBool below;

  below = gFalse; // make gcc happy
  switch (page->primaryRot) {
  case 0:
    below = xMin >= blk->priMin && xMax <= blk->priMax &&
            yMin > blk->yMin;
    break;
  case 1:
    below = yMin >= blk->priMin && yMax <= blk->priMax &&
            xMax < blk->xMax;
    break;
  case 2:
    below = xMin >= blk->priMin && xMax <= blk->priMax &&
            yMax < blk->yMax;
    break;
  case 3:
    below = yMin >= blk->priMin && yMax <= blk->priMax &&
            xMin > blk->xMin;
    break;
  }

  return below;
}

GBool TextBlock::isBeforeByRule1(TextBlock *blk1) {
  GBool before = gFalse;
  GBool overlap = gFalse;

  switch (this->page->primaryRot) {
  case 0:
  case 2:
    overlap = ((this->ExMin <= blk1->ExMin) &&
	       (blk1->ExMin <= this->ExMax)) ||
      ((blk1->ExMin <= this->ExMin) &&
       (this->ExMin <= blk1->ExMax));
    break;
  case 1:
  case 3:
    overlap = ((this->EyMin <= blk1->EyMin) &&
	       (blk1->EyMin <= this->EyMax)) ||
      ((blk1->EyMin <= this->EyMin) &&
       (this->EyMin <= blk1->EyMax));
    break;
  }
  switch (this->page->primaryRot) {
  case 0:
    before = overlap && this->EyMin < blk1->EyMin;
    break;
  case 1:
    before = overlap && this->ExMax > blk1->ExMax;
    break;
  case 2:
    before = overlap && this->EyMax > blk1->EyMax;
    break;
  case 3:
    before = overlap && this->ExMin < blk1->ExMin;
    break;
  }
  return before;
}

GBool TextBlock::isBeforeByRule2(TextBlock *blk1) {
  double cmp = 0;
  int rotLR = rot;

  if (!page->primaryLR) {
    rotLR = (rotLR + 2) % 4;
  }

  switch (rotLR) {
  case 0:
    cmp = ExMax - blk1->ExMin;
    break;
  case 1:
    cmp = EyMin - blk1->EyMax;
    break;
  case 2:
    cmp = blk1->ExMax - ExMin;
    break;
  case 3:
    cmp = blk1->EyMin - EyMax;
    break;
  }
  return cmp <= 0;
}

// Sort into reading order by performing a topological sort using the rules
// given in "High Performance Document Layout Analysis", T.M. Breuel, 2003.
// See http://pubs.iupr.org/#2003-breuel-sdiut
// Topological sort is done by depth first search, see
// http://en.wikipedia.org/wiki/Topological_sorting
int TextBlock::visitDepthFirst(TextBlock *blkList, int pos1,
			       TextBlock **sorted, int sortPos,
			       GBool* visited,
			       TextBlock **cache, int cacheSize) {
  int pos2;
  TextBlock *blk1, *blk2, *blk3;
  GBool before;

  if (visited[pos1]) {
    return sortPos;
  }

  blk1 = this;

#if 0 // for debugging
  printf("visited: %d %.2f..%.2f %.2f..%.2f\n",
	 sortPos, blk1->ExMin, blk1->ExMax, blk1->EyMin, blk1->EyMax);
#endif
  visited[pos1] = gTrue;
  pos2 = -1;
  for (blk2 = blkList; blk2; blk2 = blk2->next) {
    pos2++;
    if (visited[pos2]) {
      // skip visited nodes
      continue;
    }
    before = gFalse;

    // is blk2 before blk1? (for table entries)
    if (blk1->tableId >= 0 && blk1->tableId == blk2->tableId) {
      if (page->primaryLR) {
        if (blk2->xMax <= blk1->xMin &&
            blk2->yMin <= blk1->yMax &&
            blk2->yMax >= blk1->yMin)
          before = gTrue;
      } else {
        if (blk2->xMin >= blk1->xMax &&
            blk2->yMin <= blk1->yMax &&
            blk2->yMax >= blk1->yMin)
          before = gTrue;
      }

      if (blk2->yMax <= blk1->yMin)
        before = gTrue;
    } else {
      if (blk2->isBeforeByRule1(blk1)) {
        // Rule (1) blk1 and blk2 overlap, and blk2 is above blk1.
        before = gTrue;
#if 0   // for debugging
        printf("rule1: %.2f..%.2f %.2f..%.2f %.2f..%.2f %.2f..%.2f\n",
	       blk2->ExMin, blk2->ExMax, blk2->EyMin, blk2->EyMax,
	       blk1->ExMin, blk1->ExMax, blk1->EyMin, blk1->EyMax);
#endif
      } else if (blk2->isBeforeByRule2(blk1)) {
        // Rule (2) blk2 left of blk1, and no intervening blk3
        //          such that blk1 is before blk3 by rule 1,
        //          and blk3 is before blk2 by rule 1.
        before = gTrue;
	for (int i = 0; i < cacheSize && cache[i]; ++i) {
	  if (blk1->isBeforeByRule1(cache[i]) &&
	      cache[i]->isBeforeByRule1(blk2)) {
	    before = gFalse;
	    std::rotate(cache, cache + i, cache + i + 1);
	    break;
	  }
	}

	if (before) {
	  for (blk3 = blkList; blk3; blk3 = blk3->next) {
	    if (blk3 == blk2 || blk3 == blk1) {
	      continue;
	    }
	    if (blk1->isBeforeByRule1(blk3) &&
		blk3->isBeforeByRule1(blk2)) {
	      before = gFalse;
	      std::copy_backward(cache, cache + cacheSize - 1,
				 cache + cacheSize);
	      cache[0] = blk3;
	      break;
	    }
	  }
        }
#if 0 // for debugging
        if (before) {
	  printf("rule2: %.2f..%.2f %.2f..%.2f %.2f..%.2f %.2f..%.2f\n",
	         blk1->ExMin, blk1->ExMax, blk1->EyMin, blk1->EyMax,
	         blk2->ExMin, blk2->ExMax, blk2->EyMin, blk2->EyMax);
        }
#endif
      }
    }
    if (before) {
      // blk2 is before blk1, so it needs to be visited
      // before we can add blk1 to the sorted list.
      sortPos = blk2->visitDepthFirst(blkList, pos2, sorted, sortPos, visited, cache, cacheSize);
    }
  }
#if 0 // for debugging
  printf("sorted: %d %.2f..%.2f %.2f..%.2f\n",
	 sortPos, blk1->ExMin, blk1->ExMax, blk1->EyMin, blk1->EyMax);
#endif
  sorted[sortPos++] = blk1;
  return sortPos;
}

int TextBlock::visitDepthFirst(TextBlock *blkList, int pos1,
			       TextBlock **sorted, int sortPos,
			       GBool* visited) {
  const int blockCacheSize = 4;
  TextBlock *blockCache[blockCacheSize];
  std::fill(blockCache, blockCache + blockCacheSize, (TextBlock*)nullptr);
  return visitDepthFirst(blkList, pos1, sorted, sortPos, visited, blockCache,
			 blockCacheSize);
}

//------------------------------------------------------------------------
// TextFlow
//------------------------------------------------------------------------

TextFlow::TextFlow(TextPage *pageA, TextBlock *blk) {
  page = pageA;
  xMin = blk->xMin;
  xMax = blk->xMax;
  yMin = blk->yMin;
  yMax = blk->yMax;
  priMin = blk->priMin;
  priMax = blk->priMax;
  blocks = lastBlk = blk;
  next = nullptr;
}

TextFlow::~TextFlow() {
  TextBlock *blk;

  while (blocks) {
    blk = blocks;
    blocks = blocks->next;
    delete blk;
  }
}

void TextFlow::addBlock(TextBlock *blk) {
  if (lastBlk) {
    lastBlk->next = blk;
  } else {
    blocks = blk;
  }
  lastBlk = blk;
  if (blk->xMin < xMin) {
    xMin = blk->xMin;
  }
  if (blk->xMax > xMax) {
    xMax = blk->xMax;
  }
  if (blk->yMin < yMin) {
    yMin = blk->yMin;
  }
  if (blk->yMax > yMax) {
    yMax = blk->yMax;
  }
}

GBool TextFlow::blockFits(TextBlock *blk, TextBlock *prevBlk) {
  GBool fits;

  // lower blocks must use smaller fonts
  if (blk->lines->words->fontSize > lastBlk->lines->words->fontSize) {
    return gFalse;
  }

  fits = gFalse; // make gcc happy
  switch (page->primaryRot) {
  case 0:
    fits = blk->xMin >= priMin && blk->xMax <= priMax;
    break;
  case 1:
    fits = blk->yMin >= priMin && blk->yMax <= priMax;
    break;
  case 2:
    fits = blk->xMin >= priMin && blk->xMax <= priMax;
    break;
  case 3:
    fits = blk->yMin >= priMin && blk->yMax <= priMax;
    break;
  }
  return fits;
}

#ifdef TEXTOUT_WORD_LIST

//------------------------------------------------------------------------
// TextWordList
//------------------------------------------------------------------------

TextWordList::TextWordList(TextPage *text, GBool physLayout) {
  TextFlow *flow;
  TextBlock *blk;
  TextLine *line;
  TextWord *word;
  TextWord **wordArray;
  int nWords, i;

  words = new GooList();

  if (text->rawOrder) {
    for (word = text->rawWords; word; word = word->next) {
      words->append(word);
    }

  } else if (physLayout) {
    // this is inefficient, but it's also the least useful of these
    // three cases
    nWords = 0;
    for (flow = text->flows; flow; flow = flow->next) {
      for (blk = flow->blocks; blk; blk = blk->next) {
	for (line = blk->lines; line; line = line->next) {
	  for (word = line->words; word; word = word->next) {
	    ++nWords;
	  }
	}
      }
    }
    wordArray = (TextWord **)gmallocn(nWords, sizeof(TextWord *));
    i = 0;
    for (flow = text->flows; flow; flow = flow->next) {
      for (blk = flow->blocks; blk; blk = blk->next) {
	for (line = blk->lines; line; line = line->next) {
	  for (word = line->words; word; word = word->next) {
	    wordArray[i++] = word;
	  }
	}
      }
    }
    qsort(wordArray, nWords, sizeof(TextWord *), &TextWord::cmpYX);
    for (i = 0; i < nWords; ++i) {
      words->append(wordArray[i]);
    }
    gfree(wordArray);

  } else {
    for (flow = text->flows; flow; flow = flow->next) {
      for (blk = flow->blocks; blk; blk = blk->next) {
	for (line = blk->lines; line; line = line->next) {
	  for (word = line->words; word; word = word->next) {
	    words->append(word);
	  }
	}
      }
    }
  }
}

TextWordList::~TextWordList() {
  delete words;
}

int TextWordList::getLength() {
  return words->getLength();
}

TextWord *TextWordList::get(int idx) {
  if (idx < 0 || idx >= words->getLength()) {
    return nullptr;
  }
  return (TextWord *)words->get(idx);
}

#endif // TEXTOUT_WORD_LIST

//------------------------------------------------------------------------
// TextPage
//------------------------------------------------------------------------

TextPage::TextPage(GBool rawOrderA) {
  int rot;

  refCnt = 1;
  rawOrder = rawOrderA;
  curWord = nullptr;
  charPos = 0;
  curFont = nullptr;
  curFontSize = 0;
  nest = 0;
  nTinyChars = 0;
  lastCharOverlap = gFalse;
  if (!rawOrder) {
    for (rot = 0; rot < 4; ++rot) {
      pools[rot] = new TextPool();
    }
  }
  flows = nullptr;
  blocks = nullptr;
  rawWords = nullptr;
  rawLastWord = nullptr;
  fonts = new GooList();
  lastFindXMin = lastFindYMin = 0;
  haveLastFind = gFalse;
  underlines = new GooList();
  links = new GooList();
  mergeCombining = gTrue;
}

TextPage::~TextPage() {
  int rot;

  clear();
  if (!rawOrder) {
    for (rot = 0; rot < 4; ++rot) {
      delete pools[rot];
    }
  }
  delete fonts;
  deleteGooList(underlines, TextUnderline);
  deleteGooList(links, TextLink);
}

void TextPage::incRefCnt() {
  refCnt++;
}

void TextPage::decRefCnt() {
  if (--refCnt == 0)
    delete this;
}

void TextPage::startPage(GfxState *state) {
  clear();
  if (state) {
    pageWidth = state->getPageWidth();
    pageHeight = state->getPageHeight();
  } else {
    pageWidth = pageHeight = 0;
  }
}

void TextPage::endPage() {
  if (curWord) {
    endWord();
  }
}

void TextPage::clear() {
  int rot;
  TextFlow *flow;
  TextWord *word;

  if (curWord) {
    delete curWord;
    curWord = nullptr;
  }
  if (rawOrder) {
    while (rawWords) {
      word = rawWords;
      rawWords = rawWords->next;
      delete word;
    }
  } else {
    for (rot = 0; rot < 4; ++rot) {
      delete pools[rot];
    }
    while (flows) {
      flow = flows;
      flows = flows->next;
      delete flow;
    }
    gfree(blocks);
  }
  deleteGooList(fonts, TextFontInfo);
  deleteGooList(underlines, TextUnderline);
  deleteGooList(links, TextLink);

  curWord = nullptr;
  charPos = 0;
  curFont = nullptr;
  curFontSize = 0;
  nest = 0;
  nTinyChars = 0;
  if (!rawOrder) {
    for (rot = 0; rot < 4; ++rot) {
      pools[rot] = new TextPool();
    }
  }
  flows = nullptr;
  blocks = nullptr;
  rawWords = nullptr;
  rawLastWord = nullptr;
  fonts = new GooList();
  underlines = new GooList();
  links = new GooList();
}

void TextPage::updateFont(GfxState *state) {
  GfxFont *gfxFont;
  double *fm;
  char *name;
  int code, mCode, letterCode, anyCode;
  double w;
  int i;

  // get the font info object
  curFont = nullptr;
  for (i = 0; i < fonts->getLength(); ++i) {
    curFont = (TextFontInfo *)fonts->get(i);
    if (curFont->matches(state)) {
      break;
    }
    curFont = nullptr;
  }
  if (!curFont) {
    curFont = new TextFontInfo(state);
    fonts->append(curFont);
  }

  // adjust the font size
  gfxFont = state->getFont();
  curFontSize = state->getTransformedFontSize();
  if (gfxFont && gfxFont->getType() == fontType3) {
    // This is a hack which makes it possible to deal with some Type 3
    // fonts.  The problem is that it's impossible to know what the
    // base coordinate system used in the font is without actually
    // rendering the font.  This code tries to guess by looking at the
    // width of the character 'm' (which breaks if the font is a
    // subset that doesn't contain 'm').
    mCode = letterCode = anyCode = -1;
    for (code = 0; code < 256; ++code) {
      name = ((Gfx8BitFont *)gfxFont)->getCharName(code);
      int nameLen = name ? strlen(name) : 0;
      GBool nameOneChar = nameLen == 1 || (nameLen > 1 && name[1] == '\0');
      if (nameOneChar && name[0] == 'm') {
	mCode = code;
      }
      if (letterCode < 0 && nameOneChar &&
	  ((name[0] >= 'A' && name[0] <= 'Z') ||
	   (name[0] >= 'a' && name[0] <= 'z'))) {
	letterCode = code;
      }
      if (anyCode < 0 && name &&
	  ((Gfx8BitFont *)gfxFont)->getWidth(code) > 0) {
	anyCode = code;
      }
    }
    if (mCode >= 0 &&
	(w = ((Gfx8BitFont *)gfxFont)->getWidth(mCode)) > 0) {
      // 0.6 is a generic average 'm' width -- yes, this is a hack
      curFontSize *= w / 0.6;
    } else if (letterCode >= 0 &&
	       (w = ((Gfx8BitFont *)gfxFont)->getWidth(letterCode)) > 0) {
      // even more of a hack: 0.5 is a generic letter width
      curFontSize *= w / 0.5;
    } else if (anyCode >= 0 &&
	       (w = ((Gfx8BitFont *)gfxFont)->getWidth(anyCode)) > 0) {
      // better than nothing: 0.5 is a generic character width
      curFontSize *= w / 0.5;
    }
    fm = gfxFont->getFontMatrix();
    if (fm[0] != 0) {
      curFontSize *= fabs(fm[3] / fm[0]);
    }
  }
}

void TextPage::beginWord(GfxState *state) {
  GfxFont *gfxFont;
  double *fontm;
  double m[4], m2[4];
  int rot;

  // This check is needed because Type 3 characters can contain
  // text-drawing operations (when TextPage is being used via
  // {X,Win}SplashOutputDev rather than TextOutputDev).
  if (curWord) {
    ++nest;
    return;
  }

  // compute the rotation
  state->getFontTransMat(&m[0], &m[1], &m[2], &m[3]);
  gfxFont = state->getFont();
  if (gfxFont && gfxFont->getType() == fontType3) {
    fontm = state->getFont()->getFontMatrix();
    m2[0] = fontm[0] * m[0] + fontm[1] * m[2];
    m2[1] = fontm[0] * m[1] + fontm[1] * m[3];
    m2[2] = fontm[2] * m[0] + fontm[3] * m[2];
    m2[3] = fontm[2] * m[1] + fontm[3] * m[3];
    m[0] = m2[0];
    m[1] = m2[1];
    m[2] = m2[2];
    m[3] = m2[3];
  }
  if (fabs(m[0] * m[3]) > fabs(m[1] * m[2])) {
    rot = (m[0] > 0 || m[3] < 0) ? 0 : 2;
  } else {
    rot = (m[2] > 0) ? 1 : 3;
  }

  // for vertical writing mode, the lines are effectively rotated 90
  // degrees
  if (gfxFont && gfxFont->getWMode()) {
    rot = (rot + 1) & 3;
  }

  curWord = new TextWord(state, rot, curFontSize);
}

void TextPage::addChar(GfxState *state, double x, double y,
		       double dx, double dy,
		       CharCode c, int nBytes, Unicode *u, int uLen) {
  double x1, y1, w1, h1, dx2, dy2, base, sp, delta;
  GBool overlap;
  int i;
  int wMode;
  Matrix mat;

  // subtract char and word spacing from the dx,dy values
  sp = state->getCharSpace();
  if (c == (CharCode)0x20) {
    sp += state->getWordSpace();
  }
  state->textTransformDelta(sp * state->getHorizScaling(), 0, &dx2, &dy2);
  dx -= dx2;
  dy -= dy2;
  state->transformDelta(dx, dy, &w1, &h1);

  // throw away chars that aren't inside the page bounds
  // (and also do a sanity check on the character size)
  state->transform(x, y, &x1, &y1);
  if (x1 + w1 < 0 || x1 > pageWidth ||
      y1 + h1 < 0 || y1 > pageHeight ||
      x1 != x1 || y1 != y1 || // IEEE way of checking for isnan
      w1 != w1 || h1 != h1) {
    charPos += nBytes;
    return;
  }

  // check the tiny chars limit
  if (fabs(w1) < 3 && fabs(h1) < 3) {
    if (++nTinyChars > 50000) {
      charPos += nBytes;
      return;
    }
  }

  // break words at space character
  if (uLen == 1 && UnicodeIsWhitespace(u[0])) {
    charPos += nBytes;
    endWord();
    return;
  } else if (uLen == 1 && u[0] == (Unicode)0x0) {
    // ignore null characters
    charPos += nBytes;
    return;
  }

  state->getFontTransMat(&mat.m[0], &mat.m[1], &mat.m[2], &mat.m[3]);
  mat.m[0] *= state->getHorizScaling();
  mat.m[1] *= state->getHorizScaling();
  mat.m[4] = x1;
  mat.m[5] = y1;

  if (mergeCombining && curWord && uLen == 1 &&
      curWord->addCombining(state, curFont, curFontSize, x1, y1, w1, h1, charPos, nBytes, c,
			    u[0], mat)) {
    charPos += nBytes;
    return;
  }

  // start a new word if:
  // (1) this character doesn't fall in the right place relative to
  //     the end of the previous word (this places upper and lower
  //     constraints on the position deltas along both the primary
  //     and secondary axes), or
  // (2) this character overlaps the previous one (duplicated text), or
  // (3) the previous character was an overlap (we want each duplicated
  //     character to be in a word by itself at this stage),
  // (4) the font size has changed
  // (5) the WMode changed
  if (curWord && curWord->len > 0) {
    base = sp = delta = 0; // make gcc happy
    switch (curWord->rot) {
    case 0:
      base = y1;
      sp = x1 - curWord->xMax;
      delta = x1 - curWord->edge[curWord->len - 1];
      break;
    case 1:
      base = x1;
      sp = y1 - curWord->yMax;
      delta = y1 - curWord->edge[curWord->len - 1];
      break;
    case 2:
      base = y1;
      sp = curWord->xMin - x1;
      delta = curWord->edge[curWord->len - 1] - x1;
      break;
    case 3:
      base = x1;
      sp = curWord->yMin - y1;
      delta = curWord->edge[curWord->len - 1] - y1;
      break;
    }
    overlap = fabs(delta) < dupMaxPriDelta * curWord->fontSize &&
              fabs(base - curWord->base) < dupMaxSecDelta * curWord->fontSize;
    wMode = curFont->getWMode();
    if (overlap || lastCharOverlap ||
	sp < -minDupBreakOverlap * curWord->fontSize ||
	sp > minWordBreakSpace * curWord->fontSize ||
	fabs(base - curWord->base) > 0.5 ||
	curFontSize != curWord->fontSize ||
	wMode != curWord->wMode) {
      endWord();
    }
    lastCharOverlap = overlap;
  } else {
    lastCharOverlap = gFalse;
  }

  if (uLen != 0) {
    // start a new word if needed
    if (!curWord) {
      beginWord(state);
    }

    // page rotation and/or transform matrices can cause text to be
    // drawn in reverse order -- in this case, swap the begin/end
    // coordinates and break text into individual chars
    if ((curWord->rot == 0 && w1 < 0) ||
        (curWord->rot == 1 && h1 < 0) ||
        (curWord->rot == 2 && w1 > 0) ||
        (curWord->rot == 3 && h1 > 0)) {
      endWord();
      beginWord(state);
      x1 += w1;
      y1 += h1;
      w1 = -w1;
      h1 = -h1;
    }

    // add the characters to the current word
    w1 /= uLen;
    h1 /= uLen;
    for (i = 0; i < uLen; ++i) {
      curWord->addChar(state, curFont, x1 + i*w1, y1 + i*h1, w1, h1, charPos, nBytes, c, u[i], mat);
    }
  }
  charPos += nBytes;
}

void TextPage::incCharCount(int nChars) {
  charPos += nChars;
}

void TextPage::endWord() {
  // This check is needed because Type 3 characters can contain
  // text-drawing operations (when TextPage is being used via
  // {X,Win}SplashOutputDev rather than TextOutputDev).
  if (nest > 0) {
    --nest;
    return;
  }

  if (curWord) {
    addWord(curWord);
    curWord = nullptr;
  }
}

void TextPage::addWord(TextWord *word) {
  // throw away zero-length words -- they don't have valid xMin/xMax
  // values, and they're useless anyway
  if (word->len == 0) {
    delete word;
    return;
  }

  if (rawOrder) {
    if (rawLastWord) {
      rawLastWord->next = word;
    } else {
      rawWords = word;
    }
    rawLastWord = word;
  } else {
    pools[word->rot]->addWord(word);
  }
}

void TextPage::addUnderline(double x0, double y0, double x1, double y1) {
  underlines->append(new TextUnderline(x0, y0, x1, y1));
}

void TextPage::addLink(int xMin, int yMin, int xMax, int yMax, AnnotLink *link) {
  links->append(new TextLink(xMin, yMin, xMax, yMax, link));
}

void TextPage::coalesce(GBool physLayout, double fixedPitch, GBool doHTML) {
  UnicodeMap *uMap;
  TextPool *pool;
  TextWord *word0, *word1, *word2;
  TextLine *line;
  TextBlock *blkList, *blk, *lastBlk, *blk0, *blk1, *blk2;
  TextFlow *flow, *lastFlow;
  TextUnderline *underline;
  TextLink *link;
  int rot, poolMinBaseIdx, baseIdx, startBaseIdx, endBaseIdx;
  double minBase, maxBase, newMinBase, newMaxBase;
  double fontSize, colSpace1, colSpace2, lineSpace, intraLineSpace, blkSpace;
  GBool found;
  int count[4];
  int lrCount;
  int col1, col2;
  int i, j, n;

  if (rawOrder) {
    primaryRot = 0;
    primaryLR = gTrue;
    return;
  }

  uMap = globalParams->getTextEncoding();
  blkList = nullptr;
  lastBlk = nullptr;
  nBlocks = 0;
  primaryRot = 0;

#if 0 // for debugging
  printf("*** initial words ***\n");
  for (rot = 0; rot < 4; ++rot) {
    pool = pools[rot];
    for (baseIdx = pool->minBaseIdx; baseIdx <= pool->maxBaseIdx; ++baseIdx) {
      for (word0 = pool->getPool(baseIdx); word0; word0 = word0->next) {
	printf("    word: x=%.2f..%.2f y=%.2f..%.2f base=%.2f fontSize=%.2f rot=%d link=%p '",
	       word0->xMin, word0->xMax, word0->yMin, word0->yMax,
	       word0->base, word0->fontSize, rot*90, word0->link);
	for (i = 0; i < word0->len; ++i) {
	  fputc(word0->text[i] & 0xff, stdout);
	}
	printf("'\n");
      }
    }
  }
  printf("\n");
#endif

#if 0 //~ for debugging
  for (i = 0; i < underlines->getLength(); ++i) {
    underline = (TextUnderline *)underlines->get(i);
    printf("underline: x=%g..%g y=%g..%g horiz=%d\n",
	   underline->x0, underline->x1, underline->y0, underline->y1,
	   underline->horiz);
  }
#endif

  if (doHTML) {

    //----- handle underlining
    for (i = 0; i < underlines->getLength(); ++i) {
      underline = (TextUnderline *)underlines->get(i);
      if (underline->horiz) {
	// rot = 0
	if (pools[0]->minBaseIdx <= pools[0]->maxBaseIdx) {
	  startBaseIdx = pools[0]->getBaseIdx(underline->y0 + minUnderlineGap);
	  endBaseIdx = pools[0]->getBaseIdx(underline->y0 + maxUnderlineGap);
	  for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	    for (word0 = pools[0]->getPool(j); word0; word0 = word0->next) {
	      //~ need to check the y value against the word baseline
	      if (underline->x0 < word0->xMin + underlineSlack &&
		  word0->xMax - underlineSlack < underline->x1) {
		word0->underlined = gTrue;
	      }
	    }
	  }
	}

	// rot = 2
	if (pools[2]->minBaseIdx <= pools[2]->maxBaseIdx) {
	  startBaseIdx = pools[2]->getBaseIdx(underline->y0 - maxUnderlineGap);
	  endBaseIdx = pools[2]->getBaseIdx(underline->y0 - minUnderlineGap);
	  for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	    for (word0 = pools[2]->getPool(j); word0; word0 = word0->next) {
	      if (underline->x0 < word0->xMin + underlineSlack &&
		  word0->xMax - underlineSlack < underline->x1) {
		word0->underlined = gTrue;
	      }
	    }
	  }
	}
      } else {
	// rot = 1
	if (pools[1]->minBaseIdx <= pools[1]->maxBaseIdx) {
	  startBaseIdx = pools[1]->getBaseIdx(underline->x0 - maxUnderlineGap);
	  endBaseIdx = pools[1]->getBaseIdx(underline->x0 - minUnderlineGap);
	  for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	    for (word0 = pools[1]->getPool(j); word0; word0 = word0->next) {
	      if (underline->y0 < word0->yMin + underlineSlack &&
		  word0->yMax - underlineSlack < underline->y1) {
		word0->underlined = gTrue;
	      }
	    }
	  }
	}

	// rot = 3
	if (pools[3]->minBaseIdx <= pools[3]->maxBaseIdx) {
	  startBaseIdx = pools[3]->getBaseIdx(underline->x0 + minUnderlineGap);
	  endBaseIdx = pools[3]->getBaseIdx(underline->x0 + maxUnderlineGap);
	  for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	    for (word0 = pools[3]->getPool(j); word0; word0 = word0->next) {
	      if (underline->y0 < word0->yMin + underlineSlack &&
		  word0->yMax - underlineSlack < underline->y1) {
		word0->underlined = gTrue;
	      }
	    }
	  }
	}
      }
    }

    //----- handle links
    for (i = 0; i < links->getLength(); ++i) {
      link = (TextLink *)links->get(i);

      // rot = 0
      if (pools[0]->minBaseIdx <= pools[0]->maxBaseIdx) {
	startBaseIdx = pools[0]->getBaseIdx(link->yMin);
	endBaseIdx = pools[0]->getBaseIdx(link->yMax);
	for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	  for (word0 = pools[0]->getPool(j); word0; word0 = word0->next) {
	    if (link->xMin < word0->xMin + hyperlinkSlack &&
		word0->xMax - hyperlinkSlack < link->xMax &&
		link->yMin < word0->yMin + hyperlinkSlack &&
		word0->yMax - hyperlinkSlack < link->yMax) {
	      word0->link = link->link;
	    }
	  }
	}
      }

      // rot = 2
      if (pools[2]->minBaseIdx <= pools[2]->maxBaseIdx) {
	startBaseIdx = pools[2]->getBaseIdx(link->yMin);
	endBaseIdx = pools[2]->getBaseIdx(link->yMax);
	for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	  for (word0 = pools[2]->getPool(j); word0; word0 = word0->next) {
	    if (link->xMin < word0->xMin + hyperlinkSlack &&
		word0->xMax - hyperlinkSlack < link->xMax &&
		link->yMin < word0->yMin + hyperlinkSlack &&
		word0->yMax - hyperlinkSlack < link->yMax) {
	      word0->link = link->link;
	    }
	  }
	}
      }

      // rot = 1
      if (pools[1]->minBaseIdx <= pools[1]->maxBaseIdx) {
	startBaseIdx = pools[1]->getBaseIdx(link->xMin);
	endBaseIdx = pools[1]->getBaseIdx(link->xMax);
	for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	  for (word0 = pools[1]->getPool(j); word0; word0 = word0->next) {
	    if (link->yMin < word0->yMin + hyperlinkSlack &&
		word0->yMax - hyperlinkSlack < link->yMax &&
		link->xMin < word0->xMin + hyperlinkSlack &&
		word0->xMax - hyperlinkSlack < link->xMax) {
	      word0->link = link->link;
	    }
	  }
	}
      }

      // rot = 3
      if (pools[3]->minBaseIdx <= pools[3]->maxBaseIdx) {
	startBaseIdx = pools[3]->getBaseIdx(link->xMin);
	endBaseIdx = pools[3]->getBaseIdx(link->xMax);
	for (j = startBaseIdx; j <= endBaseIdx; ++j) {
	  for (word0 = pools[3]->getPool(j); word0; word0 = word0->next) {
	    if (link->yMin < word0->yMin + hyperlinkSlack &&
		word0->yMax - hyperlinkSlack < link->yMax &&
		link->xMin < word0->xMin + hyperlinkSlack &&
		word0->xMax - hyperlinkSlack < link->xMax) {
	      word0->link = link->link;
	    }
	  }
	}
      }
    }
  }

  //----- assemble the blocks

  //~ add an outer loop for writing mode (vertical text)

  // build blocks for each rotation value
  for (rot = 0; rot < 4; ++rot) {
    pool = pools[rot];
    poolMinBaseIdx = pool->minBaseIdx;
    count[rot] = 0;

    // add blocks until no more words are left
    while (1) {

      // find the first non-empty line in the pool
      for (;
	   poolMinBaseIdx <= pool->maxBaseIdx &&
	     !pool->getPool(poolMinBaseIdx);
	   ++poolMinBaseIdx) ;
      if (poolMinBaseIdx > pool->maxBaseIdx) {
	break;
      }

      // look for the left-most word in the first four lines of the
      // pool -- this avoids starting with a superscript word
      startBaseIdx = poolMinBaseIdx;
      for (baseIdx = poolMinBaseIdx + 1;
	   baseIdx < poolMinBaseIdx + 4 && baseIdx <= pool->maxBaseIdx;
	   ++baseIdx) {
	if (!pool->getPool(baseIdx)) {
	  continue;
	}
	if (pool->getPool(baseIdx)->primaryCmp(pool->getPool(startBaseIdx))
	    < 0) {
	  startBaseIdx = baseIdx;
	}
      }

      // create a new block
      word0 = pool->getPool(startBaseIdx);
      pool->setPool(startBaseIdx, word0->next);
      word0->next = nullptr;
      blk = new TextBlock(this, rot);
      blk->addWord(word0);

      fontSize = word0->fontSize;
      minBase = maxBase = word0->base;
      colSpace1 = minColSpacing1 * fontSize;
      colSpace2 = minColSpacing2 * fontSize;
      lineSpace = maxLineSpacingDelta * fontSize;
      intraLineSpace = maxIntraLineDelta * fontSize;

      // add words to the block
      do {
	found = gFalse;

	// look for words on the line above the current top edge of
	// the block
	newMinBase = minBase;
	for (baseIdx = pool->getBaseIdx(minBase);
	     baseIdx >= pool->getBaseIdx(minBase - lineSpace);
	     --baseIdx) {
	  word0 = nullptr;
	  word1 = pool->getPool(baseIdx);
	  while (word1) {
	    if (word1->base < minBase &&
		word1->base >= minBase - lineSpace &&
		((rot == 0 || rot == 2)
		 ? (word1->xMin < blk->xMax && word1->xMax > blk->xMin)
		 : (word1->yMin < blk->yMax && word1->yMax > blk->yMin)) &&
		fabs(word1->fontSize - fontSize) <
		  maxBlockFontSizeDelta1 * fontSize) {
	      word2 = word1;
	      if (word0) {
		word0->next = word1->next;
	      } else {
		pool->setPool(baseIdx, word1->next);
	      }
	      word1 = word1->next;
	      word2->next = nullptr;
	      blk->addWord(word2);
	      found = gTrue;
	      newMinBase = word2->base;
	    } else {
	      word0 = word1;
	      word1 = word1->next;
	    }
	  }
	}
	minBase = newMinBase;

	// look for words on the line below the current bottom edge of
	// the block
	newMaxBase = maxBase;
	for (baseIdx = pool->getBaseIdx(maxBase);
	     baseIdx <= pool->getBaseIdx(maxBase + lineSpace);
	     ++baseIdx) {
	  word0 = nullptr;
	  word1 = pool->getPool(baseIdx);
	  while (word1) {
	    if (word1->base > maxBase &&
		word1->base <= maxBase + lineSpace &&
		((rot == 0 || rot == 2)
		 ? (word1->xMin < blk->xMax && word1->xMax > blk->xMin)
		 : (word1->yMin < blk->yMax && word1->yMax > blk->yMin)) &&
		fabs(word1->fontSize - fontSize) <
		  maxBlockFontSizeDelta1 * fontSize) {
	      word2 = word1;
	      if (word0) {
		word0->next = word1->next;
	      } else {
		pool->setPool(baseIdx, word1->next);
	      }
	      word1 = word1->next;
	      word2->next = nullptr;
	      blk->addWord(word2);
	      found = gTrue;
	      newMaxBase = word2->base;
	    } else {
	      word0 = word1;
	      word1 = word1->next;
	    }
	  }
	}
	maxBase = newMaxBase;

	// look for words that are on lines already in the block, and
	// that overlap the block horizontally
	for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
	     baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
	     ++baseIdx) {
	  word0 = nullptr;
	  word1 = pool->getPool(baseIdx);
	  while (word1) {
	    if (word1->base >= minBase - intraLineSpace &&
		word1->base <= maxBase + intraLineSpace &&
		((rot == 0 || rot == 2)
		 ? (word1->xMin < blk->xMax + colSpace1 &&
		    word1->xMax > blk->xMin - colSpace1)
		 : (word1->yMin < blk->yMax + colSpace1 &&
		    word1->yMax > blk->yMin - colSpace1)) &&
		fabs(word1->fontSize - fontSize) <
		  maxBlockFontSizeDelta2 * fontSize) {
	      word2 = word1;
	      if (word0) {
		word0->next = word1->next;
	      } else {
		pool->setPool(baseIdx, word1->next);
	      }
	      word1 = word1->next;
	      word2->next = nullptr;
	      blk->addWord(word2);
	      found = gTrue;
	    } else {
	      word0 = word1;
	      word1 = word1->next;
	    }
	  }
	}

	// only check for outlying words (the next two chunks of code)
	// if we didn't find anything else
	if (found) {
	  continue;
	}

	// scan down the left side of the block, looking for words
	// that are near (but not overlapping) the block; if there are
	// three or fewer, add them to the block
	n = 0;
	for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
	     baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
	     ++baseIdx) {
	  word1 = pool->getPool(baseIdx);
	  while (word1) {
	    if (word1->base >= minBase - intraLineSpace &&
		word1->base <= maxBase + intraLineSpace &&
		((rot == 0 || rot == 2)
		 ? (word1->xMax <= blk->xMin &&
		    word1->xMax > blk->xMin - colSpace2)
		 : (word1->yMax <= blk->yMin &&
		    word1->yMax > blk->yMin - colSpace2)) &&
		fabs(word1->fontSize - fontSize) <
		  maxBlockFontSizeDelta3 * fontSize) {
	      ++n;
	      break;
	    }
	    word1 = word1->next;
	  }
	}
	if (n > 0 && n <= 3) {
	  for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
	       baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
	       ++baseIdx) {
	    word0 = nullptr;
	    word1 = pool->getPool(baseIdx);
	    while (word1) {
	      if (word1->base >= minBase - intraLineSpace &&
		  word1->base <= maxBase + intraLineSpace &&
		  ((rot == 0 || rot == 2)
		   ? (word1->xMax <= blk->xMin &&
		      word1->xMax > blk->xMin - colSpace2)
		   : (word1->yMax <= blk->yMin &&
		      word1->yMax > blk->yMin - colSpace2)) &&
		  fabs(word1->fontSize - fontSize) <
		    maxBlockFontSizeDelta3 * fontSize) {
		word2 = word1;
		if (word0) {
		  word0->next = word1->next;
		} else {
		  pool->setPool(baseIdx, word1->next);
		}
		word1 = word1->next;
		word2->next = nullptr;
		blk->addWord(word2);
		if (word2->base < minBase) {
		  minBase = word2->base;
		} else if (word2->base > maxBase) {
		  maxBase = word2->base;
		}
		found = gTrue;
		break;
	      } else {
		word0 = word1;
		word1 = word1->next;
	      }
	    }
	  }
	}

	// scan down the right side of the block, looking for words
	// that are near (but not overlapping) the block; if there are
	// three or fewer, add them to the block
	n = 0;
	for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
	     baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
	     ++baseIdx) {
	  word1 = pool->getPool(baseIdx);
	  while (word1) {
	    if (word1->base >= minBase - intraLineSpace &&
		word1->base <= maxBase + intraLineSpace &&
		((rot == 0 || rot == 2)
		 ? (word1->xMin >= blk->xMax &&
		    word1->xMin < blk->xMax + colSpace2)
		 : (word1->yMin >= blk->yMax &&
		    word1->yMin < blk->yMax + colSpace2)) &&
		fabs(word1->fontSize - fontSize) <
		  maxBlockFontSizeDelta3 * fontSize) {
	      ++n;
	      break;
	    }
	    word1 = word1->next;
	  }
	}
	if (n > 0 && n <= 3) {
	  for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
	       baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
	       ++baseIdx) {
	    word0 = nullptr;
	    word1 = pool->getPool(baseIdx);
	    while (word1) {
	      if (word1->base >= minBase - intraLineSpace &&
		  word1->base <= maxBase + intraLineSpace &&
		  ((rot == 0 || rot == 2)
		   ? (word1->xMin >= blk->xMax &&
		      word1->xMin < blk->xMax + colSpace2)
		   : (word1->yMin >= blk->yMax &&
		      word1->yMin < blk->yMax + colSpace2)) &&
		  fabs(word1->fontSize - fontSize) <
		    maxBlockFontSizeDelta3 * fontSize) {
		word2 = word1;
		if (word0) {
		  word0->next = word1->next;
		} else {
		  pool->setPool(baseIdx, word1->next);
		}
		word1 = word1->next;
		word2->next = nullptr;
		blk->addWord(word2);
		if (word2->base < minBase) {
		  minBase = word2->base;
		} else if (word2->base > maxBase) {
		  maxBase = word2->base;
		}
		found = gTrue;
		break;
	      } else {
		word0 = word1;
		word1 = word1->next;
	      }
	    }
	  }
	}

      } while (found);

      //~ need to compute the primary writing mode (horiz/vert) in
      //~ addition to primary rotation

      // coalesce the block, and add it to the list
      blk->coalesce(uMap, fixedPitch);
      if (lastBlk) {
	lastBlk->next = blk;
      } else {
	blkList = blk;
      }
      lastBlk = blk;
      count[rot] += blk->charCount;
      ++nBlocks;
    }

    if (count[rot] > count[primaryRot]) {
      primaryRot = rot;
    }
  }

#if 0 // for debugging
  printf("*** rotation ***\n");
  for (rot = 0; rot < 4; ++rot) {
    printf("  %d: %6d\n", rot, count[rot]);
  }
  printf("  primary rot = %d\n", primaryRot);
  printf("\n");
#endif

#if 0 // for debugging
  printf("*** blocks ***\n");
  for (blk = blkList; blk; blk = blk->next) {
    printf("block: rot=%d x=%.2f..%.2f y=%.2f..%.2f\n",
	   blk->rot, blk->xMin, blk->xMax, blk->yMin, blk->yMax);
    for (line = blk->lines; line; line = line->next) {
      printf("  line: x=%.2f..%.2f y=%.2f..%.2f base=%.2f\n",
	     line->xMin, line->xMax, line->yMin, line->yMax, line->base);
      for (word0 = line->words; word0; word0 = word0->next) {
	printf("    word: x=%.2f..%.2f y=%.2f..%.2f base=%.2f fontSize=%.2f space=%d: '",
	       word0->xMin, word0->xMax, word0->yMin, word0->yMax,
	       word0->base, word0->fontSize, word0->spaceAfter);
	for (i = 0; i < word0->len; ++i) {
	  fputc(word0->text[i] & 0xff, stdout);
	}
	printf("'\n");
      }
    }
  }
  printf("\n");
#endif

  // determine the primary direction
  lrCount = 0;
  for (blk = blkList; blk; blk = blk->next) {
    for (line = blk->lines; line; line = line->next) {
      for (word0 = line->words; word0; word0 = word0->next) {
	for (i = 0; i < word0->len; ++i) {
	  if (unicodeTypeL(word0->text[i])) {
	    ++lrCount;
	  } else if (unicodeTypeR(word0->text[i])) {
	    --lrCount;
	  }
	}
      }
    }
  }
  primaryLR = lrCount >= 0;

#if 0 // for debugging
  printf("*** direction ***\n");
  printf("lrCount = %d\n", lrCount);
  printf("primaryLR = %d\n", primaryLR);
#endif

  //----- column assignment

  // sort blocks into xy order for column assignment
  if (blocks)
    gfree (blocks);
  if (physLayout && fixedPitch) {

    blocks = (TextBlock **)gmallocn(nBlocks, sizeof(TextBlock *));
    for (blk = blkList, i = 0; blk; blk = blk->next, ++i) {
      blocks[i] = blk;
      col1 = 0; // make gcc happy
      switch (primaryRot) {
      case 0:
	col1 = (int)(blk->xMin / fixedPitch + 0.5);
	break;
      case 1:
	col1 = (int)(blk->yMin / fixedPitch + 0.5);
	break;
      case 2:
	col1 = (int)((pageWidth - blk->xMax) / fixedPitch + 0.5);
	break;
      case 3:
	col1 = (int)((pageHeight - blk->yMax) / fixedPitch + 0.5);
	break;
      }
      blk->col = col1;
      for (line = blk->lines; line; line = line->next) {
	for (j = 0; j <= line->len; ++j) {
	  line->col[j] += col1;
	}
      }
    }

  } else {

    // sort blocks into xy order for column assignment
    blocks = (TextBlock **)gmallocn(nBlocks, sizeof(TextBlock *));
    for (blk = blkList, i = 0; blk; blk = blk->next, ++i) {
      blocks[i] = blk;
    }
    if (blocks) {
      qsort(blocks, nBlocks, sizeof(TextBlock *), &TextBlock::cmpXYPrimaryRot);
    }

    // column assignment
    for (i = 0; i < nBlocks; ++i) {
      blk0 = blocks[i];
      col1 = 0;
      for (j = 0; j < i; ++j) {
	blk1 = blocks[j];
	col2 = 0; // make gcc happy
	switch (primaryRot) {
	case 0:
	  if (blk0->xMin > blk1->xMax) {
	    col2 = blk1->col + blk1->nColumns + 3;
	  } else if (blk1->xMax == blk1->xMin) {
	    col2 = blk1->col;
	  } else {
	    col2 = blk1->col + (int)(((blk0->xMin - blk1->xMin) /
				      (blk1->xMax - blk1->xMin)) *
				     blk1->nColumns);
	  }
	  break;
	case 1:
	  if (blk0->yMin > blk1->yMax) {
	    col2 = blk1->col + blk1->nColumns + 3;
	  } else if (blk1->yMax == blk1->yMin) {
	    col2 = blk1->col;
	  } else {
	    col2 = blk1->col + (int)(((blk0->yMin - blk1->yMin) /
				      (blk1->yMax - blk1->yMin)) *
				     blk1->nColumns);
	  }
	  break;
	case 2:
	  if (blk0->xMax < blk1->xMin) {
	    col2 = blk1->col + blk1->nColumns + 3;
	  } else if (blk1->xMin == blk1->xMax) {
	    col2 = blk1->col;
	  } else {
	    col2 = blk1->col + (int)(((blk0->xMax - blk1->xMax) /
				      (blk1->xMin - blk1->xMax)) *
				     blk1->nColumns);
	  }
	  break;
	case 3:
	  if (blk0->yMax < blk1->yMin) {
	    col2 = blk1->col + blk1->nColumns + 3;
	  } else if (blk1->yMin == blk1->yMax) {
	    col2 = blk1->col;
	  } else {
	    col2 = blk1->col + (int)(((blk0->yMax - blk1->yMax) /
				      (blk1->yMin - blk1->yMax)) *
				     blk1->nColumns);
	  }
	  break;
	}
	if (col2 > col1) {
	  col1 = col2;
	}
      }
      blk0->col = col1;
      for (line = blk0->lines; line; line = line->next) {
	for (j = 0; j <= line->len; ++j) {
	  line->col[j] += col1;
	}
      }
    }

  }

#if 0 // for debugging
  printf("*** blocks, after column assignment ***\n");
  for (blk = blkList; blk; blk = blk->next) {
    printf("block: rot=%d x=%.2f..%.2f y=%.2f..%.2f col=%d nCols=%d\n",
	   blk->rot, blk->xMin, blk->xMax, blk->yMin, blk->yMax, blk->col,
	   blk->nColumns);
    for (line = blk->lines; line; line = line->next) {
      printf("  line: col[0]=%d\n", line->col[0]);
      for (word0 = line->words; word0; word0 = word0->next) {
	printf("    word: x=%.2f..%.2f y=%.2f..%.2f base=%.2f fontSize=%.2f space=%d: '",
	       word0->xMin, word0->xMax, word0->yMin, word0->yMax,
	       word0->base, word0->fontSize, word0->spaceAfter);
	for (i = 0; i < word0->len; ++i) {
	  fputc(word0->text[i] & 0xff, stdout);
	}
	printf("'\n");
      }
    }
  }
  printf("\n");
#endif

  //----- reading order sort

  // compute space on left and right sides of each block
  for (i = 0; i < nBlocks; ++i) {
    blk0 = blocks[i];
    for (j = 0; j < nBlocks; ++j) {
      blk1 = blocks[j];
      if (blk1 != blk0) {
	blk0->updatePriMinMax(blk1);
      }
    }
  }

#if 0 // for debugging
  printf("PAGE\n");
#endif

  int sortPos = 0;
  GBool *visited = (GBool *)gmallocn(nBlocks, sizeof(GBool));
  for (i = 0; i < nBlocks; i++) {
    visited[i] = gFalse;
  }

  double bxMin0, byMin0, bxMin1, byMin1;
  int numTables = 0;
  int tableId = -1;
  int correspondenceX, correspondenceY;
  double xCentre1, yCentre1, xCentre2, yCentre2;
  double xCentre3, yCentre3, xCentre4, yCentre4;
  double deltaX, deltaY;
  TextBlock *fblk2 = nullptr, *fblk3 = nullptr, *fblk4 = nullptr;

  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    blk1->ExMin = blk1->xMin;
    blk1->ExMax = blk1->xMax;
    blk1->EyMin = blk1->yMin;
    blk1->EyMax = blk1->yMax;

    bxMin0 = DBL_MAX;
    byMin0 = DBL_MAX;
    bxMin1 = DBL_MAX;
    byMin1 = DBL_MAX;

    fblk2 = nullptr;
    fblk3 = nullptr;
    fblk4 = nullptr;

    /*  find fblk2, fblk3 and fblk4 so that
     *  fblk2 is on the right of blk1 and overlap with blk1 in y axis
     *  fblk3 is under blk1 and overlap with blk1 in x axis
     *  fblk4 is under blk1 and on the right of blk1
     *  and they are closest to blk1
     */
    for (blk2 = blkList; blk2; blk2 = blk2->next) {
      if (blk2 != blk1) {
        if (blk2->yMin <= blk1->yMax &&
            blk2->yMax >= blk1->yMin &&
            blk2->xMin > blk1->xMax &&
            blk2->xMin < bxMin0) {
          bxMin0 = blk2->xMin;
          fblk2 = blk2;
        } else if (blk2->xMin <= blk1->xMax &&
                   blk2->xMax >= blk1->xMin &&
                   blk2->yMin > blk1->yMax &&
                   blk2->yMin < byMin0) {
          byMin0 = blk2->yMin;
          fblk3 = blk2;
        } else if (blk2->xMin > blk1->xMax &&
                   blk2->xMin < bxMin1 &&
                   blk2->yMin > blk1->yMax &&
                   blk2->yMin < byMin1) {
          bxMin1 = blk2->xMin;
          byMin1 = blk2->yMin;
          fblk4 = blk2;
        }
      }
    }

    /*  fblk4 can not overlap with fblk3 in x and with fblk2 in y
     *  fblk2 can not overlap with fblk3 in x and y
     *  fblk4 has to overlap with fblk3 in y and with fblk2 in x
     */
    if (fblk2 != nullptr &&
        fblk3 != nullptr &&
        fblk4 != nullptr) {
      if (((fblk3->xMin <= fblk4->xMax && fblk3->xMax >= fblk4->xMin) ||
           (fblk2->yMin <= fblk4->yMax && fblk2->yMax >= fblk4->yMin) ||
           (fblk2->xMin <= fblk3->xMax && fblk2->xMax >= fblk3->xMin) ||
           (fblk2->yMin <= fblk3->yMax && fblk2->yMax >= fblk3->yMin)) ||
          !(fblk4->xMin <= fblk2->xMax && fblk4->xMax >= fblk2->xMin &&
            fblk4->yMin <= fblk3->yMax && fblk4->yMax >= fblk3->yMin)) {
        fblk2 = nullptr;
        fblk3 = nullptr;
        fblk4 = nullptr;
      }
    }

    // if we found any then look whether they form a table
    if (fblk2 != nullptr &&
        fblk3 != nullptr &&
        fblk4 != nullptr) {
      tableId = -1;
      correspondenceX = 0;
      correspondenceY = 0;
      deltaX = 0.0;
      deltaY = 0.0;

      if (blk1->lines && blk1->lines->words)
        deltaX = blk1->lines->words->getFontSize();
      if (fblk2->lines && fblk2->lines->words)
        deltaX = deltaX < fblk2->lines->words->getFontSize() ?
                   deltaX : fblk2->lines->words->getFontSize();
      if (fblk3->lines && fblk3->lines->words)
        deltaX = deltaX < fblk3->lines->words->getFontSize() ?
                   deltaX : fblk3->lines->words->getFontSize();
      if (fblk4->lines && fblk4->lines->words)
        deltaX = deltaX < fblk4->lines->words->getFontSize() ?
                   deltaX : fblk4->lines->words->getFontSize();

      deltaY = deltaX;

      deltaX *= minColSpacing1;
      deltaY *= maxIntraLineDelta;

      xCentre1 = (blk1->xMax + blk1->xMin) / 2.0;
      yCentre1 = (blk1->yMax + blk1->yMin) / 2.0;
      xCentre2 = (fblk2->xMax + fblk2->xMin) / 2.0;
      yCentre2 = (fblk2->yMax + fblk2->yMin) / 2.0;
      xCentre3 = (fblk3->xMax + fblk3->xMin) / 2.0;
      yCentre3 = (fblk3->yMax + fblk3->yMin) / 2.0;
      xCentre4 = (fblk4->xMax + fblk4->xMin) / 2.0;
      yCentre4 = (fblk4->yMax + fblk4->yMin) / 2.0;

      // are blocks centrally aligned in x ?
      if (fabs (xCentre1 - xCentre3) <= deltaX &&
          fabs (xCentre2 - xCentre4) <= deltaX)
        correspondenceX++;

      // are blocks centrally aligned in y ?
      if (fabs (yCentre1 - yCentre2) <= deltaY &&
          fabs (yCentre3 - yCentre4) <= deltaY)
        correspondenceY++;

      // are blocks aligned to the left ?
      if (fabs (blk1->xMin - fblk3->xMin) <= deltaX &&
          fabs (fblk2->xMin - fblk4->xMin) <= deltaX)
        correspondenceX++;

      // are blocks aligned to the right ?
      if (fabs (blk1->xMax - fblk3->xMax) <= deltaX &&
          fabs (fblk2->xMax - fblk4->xMax) <= deltaX)
        correspondenceX++;

      // are blocks aligned to the top ?
      if (fabs (blk1->yMin - fblk2->yMin) <= deltaY &&
          fabs (fblk3->yMin - fblk4->yMin) <= deltaY)
        correspondenceY++;

      // are blocks aligned to the bottom ?
      if (fabs (blk1->yMax - fblk2->yMax) <= deltaY &&
          fabs (fblk3->yMax - fblk4->yMax) <= deltaY)
        correspondenceY++;

      // are blocks aligned in x and y ?
      if (correspondenceX > 0 &&
          correspondenceY > 0) {

        // find maximal tableId
        tableId = tableId < fblk4->tableId ? fblk4->tableId : tableId;
        tableId = tableId < fblk3->tableId ? fblk3->tableId : tableId;
        tableId = tableId < fblk2->tableId ? fblk2->tableId : tableId;
        tableId = tableId < blk1->tableId ? blk1->tableId : tableId;

        // if the tableId is -1, then we found new table
        if (tableId < 0) {
          tableId = numTables;
          numTables++;
        }

        blk1->tableId = tableId;
        fblk2->tableId = tableId;
        fblk3->tableId = tableId;
        fblk4->tableId = tableId;
      }
    }
  }

  /*  set extended bounding boxes of all table entries
   *  so that they contain whole table
   *  (we need to process whole table size when comparing it
   *   with regular text blocks)
   */
  PDFRectangle *envelopes = new PDFRectangle [numTables];
  TextBlock **ending_blocks = new TextBlock* [numTables];

  for (i = 0; i < numTables; i++) {
    envelopes[i].x1 = DBL_MAX;
    envelopes[i].x2 = DBL_MIN;
    envelopes[i].y1 = DBL_MAX;
    envelopes[i].y2 = DBL_MIN;
  }

  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    if (blk1->tableId >= 0) {
      if (blk1->ExMin < envelopes[blk1->tableId].x1) {
        envelopes[blk1->tableId].x1 = blk1->ExMin;
        if (!blk1->page->primaryLR)
          ending_blocks[blk1->tableId] = blk1;
      }

      if (blk1->ExMax > envelopes[blk1->tableId].x2) {
        envelopes[blk1->tableId].x2 = blk1->ExMax;
        if (blk1->page->primaryLR)
          ending_blocks[blk1->tableId] = blk1;
      }

      envelopes[blk1->tableId].y1 = blk1->EyMin < envelopes[blk1->tableId].y1 ?
                                      blk1->EyMin : envelopes[blk1->tableId].y1;
      envelopes[blk1->tableId].y2 = blk1->EyMax > envelopes[blk1->tableId].y2 ?
                                      blk1->EyMax : envelopes[blk1->tableId].y2;
    }
  }

  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    if (blk1->tableId >= 0 &&
        blk1->xMin <= ending_blocks[blk1->tableId]->xMax &&
        blk1->xMax >= ending_blocks[blk1->tableId]->xMin) {
      blk1->tableEnd = gTrue;
    }
  }

  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    if (blk1->tableId >= 0) {
      blk1->ExMin = envelopes[blk1->tableId].x1;
      blk1->ExMax = envelopes[blk1->tableId].x2;
      blk1->EyMin = envelopes[blk1->tableId].y1;
      blk1->EyMax = envelopes[blk1->tableId].y2;
    }
  }
  delete[] envelopes;
  delete[] ending_blocks;


  /*  set extended bounding boxes of all other blocks
   *  so that they extend in x without hitting neighbours
   */
  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    if (!(blk1->tableId >= 0)) {
      double xMax = DBL_MAX;
      double xMin = DBL_MIN;

      for (blk2 = blkList; blk2; blk2 = blk2->next) {
        if (blk2 == blk1)
           continue;

        if (blk1->yMin <= blk2->yMax && blk1->yMax >= blk2->yMin) {
          if (blk2->xMin < xMax && blk2->xMin > blk1->xMax)
            xMax = blk2->xMin;

          if (blk2->xMax > xMin && blk2->xMax < blk1->xMin)
            xMin = blk2->xMax;
        }
      }

      for (blk2 = blkList; blk2; blk2 = blk2->next) {
        if (blk2 == blk1)
           continue;

        if (blk2->xMax > blk1->ExMax &&
            blk2->xMax <= xMax &&
            blk2->yMin >= blk1->yMax) {
          blk1->ExMax = blk2->xMax;
        }

        if (blk2->xMin < blk1->ExMin &&
            blk2->xMin >= xMin &&
            blk2->yMin >= blk1->yMax)
          blk1->ExMin = blk2->xMin;
      }
    }
  }

  i = -1;
  for (blk1 = blkList; blk1; blk1 = blk1->next) {
    i++;
    sortPos = blk1->visitDepthFirst(blkList, i, blocks, sortPos, visited);
  }
  if (visited) {
    gfree(visited);
  }

#if 0 // for debugging
  printf("*** blocks, after ro sort ***\n");
  for (i = 0; i < nBlocks; ++i) {
    blk = blocks[i];
    printf("block: rot=%d x=%.2f..%.2f y=%.2f..%.2f space=%.2f..%.2f\n",
	   blk->rot, blk->xMin, blk->xMax, blk->yMin, blk->yMax,
	   blk->priMin, blk->priMax);
    for (line = blk->lines; line; line = line->next) {
      printf("  line:\n");
      for (word0 = line->words; word0; word0 = word0->next) {
	printf("    word: x=%.2f..%.2f y=%.2f..%.2f base=%.2f fontSize=%.2f space=%d: '",
	       word0->xMin, word0->xMax, word0->yMin, word0->yMax,
	       word0->base, word0->fontSize, word0->spaceAfter);
	for (j = 0; j < word0->len; ++j) {
	  fputc(word0->text[j] & 0xff, stdout);
	}
	printf("'\n");
      }
    }
  }
  printf("\n");
  fflush(stdout);
#endif

  // build the flows
  //~ this needs to be adjusted for writing mode (vertical text)
  //~ this also needs to account for right-to-left column ordering
  while (flows) {
    flow = flows;
    flows = flows->next;
    delete flow;
  }
  flow = nullptr;
  flows = lastFlow = nullptr;
  // assume blocks are already in reading order,
  // and construct flows accordingly.
  for (i = 0; i < nBlocks; i++) {
    blk = blocks[i];
    blk->next = nullptr;
    if (flow) {
      blk1 = blocks[i - 1];
      blkSpace = maxBlockSpacing * blk1->lines->words->fontSize;
      if (blk1->secondaryDelta(blk) <= blkSpace &&
	  blk->isBelow(blk1) &&
	  flow->blockFits(blk, blk1)) {
	flow->addBlock(blk);
	continue;
      }
    }
    flow = new TextFlow(this, blk);
    if (lastFlow) {
      lastFlow->next = flow;
    } else {
      flows = flow;
    }
    lastFlow = flow;
  }

#if 0 // for debugging
  printf("*** flows ***\n");
  for (flow = flows; flow; flow = flow->next) {
    printf("flow: x=%.2f..%.2f y=%.2f..%.2f pri:%.2f..%.2f\n",
	   flow->xMin, flow->xMax, flow->yMin, flow->yMax,
	   flow->priMin, flow->priMax);
    for (blk = flow->blocks; blk; blk = blk->next) {
      printf("  block: rot=%d x=%.2f..%.2f y=%.2f..%.2f pri=%.2f..%.2f\n",
	     blk->rot, blk->ExMin, blk->ExMax, blk->EyMin, blk->EyMax,
	     blk->priMin, blk->priMax);
      for (line = blk->lines; line; line = line->next) {
	printf("    line:\n");
	for (word0 = line->words; word0; word0 = word0->next) {
	  printf("      word: x=%.2f..%.2f y=%.2f..%.2f base=%.2f fontSize=%.2f space=%d: '",
		 word0->xMin, word0->xMax, word0->yMin, word0->yMax,
		 word0->base, word0->fontSize, word0->spaceAfter);
	  for (i = 0; i < word0->len; ++i) {
	    fputc(word0->text[i] & 0xff, stdout);
	  }
	  printf("'\n");
	}
      }
    }
  }
  printf("\n");
#endif

  if (uMap) {
    uMap->decRefCnt();
  }
}

GBool TextPage::findText(Unicode *s, int len,
			 GBool startAtTop, GBool stopAtBottom,
			 GBool startAtLast, GBool stopAtLast,
			 GBool caseSensitive, GBool backward,
			 GBool wholeWord,
			 double *xMin, double *yMin,
			 double *xMax, double *yMax) {
  TextBlock *blk;
  TextLine *line;
  Unicode *s2, *txt, *reordered;
  Unicode *p;
  int txtSize, m, i, j, k;
  double xStart, yStart, xStop, yStop;
  double xMin0, yMin0, xMax0, yMax0;
  double xMin1, yMin1, xMax1, yMax1;
  GBool found;


  if (rawOrder) {
    return gFalse;
  }

  // handle right-to-left text
  reordered = (Unicode*)gmallocn(len, sizeof(Unicode));
  reorderText(s, len, nullptr, primaryLR, nullptr, reordered);

  // normalize the search string
  s2 = unicodeNormalizeNFKC(reordered, len, &len, nullptr);

  // convert the search string to uppercase
  if (!caseSensitive) {
    for (i = 0; i < len; ++i) {
      s2[i] = unicodeToUpper(s2[i]);
    }
  }

  txt = nullptr;
  txtSize = 0;

  xStart = yStart = xStop = yStop = 0;
  if (startAtLast && haveLastFind) {
    xStart = lastFindXMin;
    yStart = lastFindYMin;
  } else if (!startAtTop) {
    xStart = *xMin;
    yStart = *yMin;
  }
  if (stopAtLast && haveLastFind) {
    xStop = lastFindXMin;
    yStop = lastFindYMin;
  } else if (!stopAtBottom) {
    xStop = *xMax;
    yStop = *yMax;
  }

  found = gFalse;
  xMin0 = xMax0 = yMin0 = yMax0 = 0; // make gcc happy
  xMin1 = xMax1 = yMin1 = yMax1 = 0; // make gcc happy

  for (i = backward ? nBlocks - 1 : 0;
       backward ? i >= 0 : i < nBlocks;
       i += backward ? -1 : 1) {
    blk = blocks[i];

    // check: is the block above the top limit?
    // (this only works if the page's primary rotation is zero --
    // otherwise the blocks won't be sorted in the useful order)
    if (!startAtTop && primaryRot == 0 &&
	(backward ? blk->yMin > yStart : blk->yMax < yStart)) {
      continue;
    }

    // check: is the block below the bottom limit?
    // (this only works if the page's primary rotation is zero --
    // otherwise the blocks won't be sorted in the useful order)
    if (!stopAtBottom && primaryRot == 0 &&
	(backward ? blk->yMax < yStop : blk->yMin > yStop)) {
      break;
    }

    for (line = blk->lines; line; line = line->next) {

      // check: is the line above the top limit?
      // (this only works if the page's primary rotation is zero --
      // otherwise the lines won't be sorted in the useful order)
      if (!startAtTop && primaryRot == 0 &&
	  (backward ? line->yMin > yStart : line->yMin < yStart)) {
	continue;
      }

      // check: is the line below the bottom limit?
      // (this only works if the page's primary rotation is zero --
      // otherwise the lines won't be sorted in the useful order)
      if (!stopAtBottom && primaryRot == 0 &&
	  (backward ? line->yMin < yStop : line->yMin > yStop)) {
	continue;
      }

      if (!line->normalized)
	line->normalized = unicodeNormalizeNFKC(line->text, line->len, 
						&line->normalized_len, 
						&line->normalized_idx,
						true);
      // convert the line to uppercase
      m = line->normalized_len;
      if (!caseSensitive) {
	if (m > txtSize) {
	  txt = (Unicode *)greallocn(txt, m, sizeof(Unicode));
	  txtSize = m;
	}
	for (k = 0; k < m; ++k) {
	  txt[k] = unicodeToUpper(line->normalized[k]);
	  }
      } else {
	txt = line->normalized;
      }

      // search each position in this line
      j = backward ? m - len : 0;
      p = txt + j;
      while (backward ? j >= 0 : j <= m - len) {
        if (!wholeWord ||
            ((j == 0 || !unicodeTypeAlphaNum(txt[j - 1])) &&
             (j + len == m || !unicodeTypeAlphaNum(txt[j + len])))) {

          // compare the strings
          for (k = 0; k < len; ++k) {
            if (p[k] != s2[k]) {
              break;
            }
          }

          // found it
          if (k == len) {
            // where s2 matches a subsequence of a compatibility equivalence
            // decomposition, highlight the entire glyph, since we don't know
            // the internal layout of subglyph components
            int normStart = line->normalized_idx[j];
            int normAfterEnd = line->normalized_idx[j + len - 1] + 1;
            switch (line->rot) {
            case 0:
              xMin1 = line->edge[normStart];
              xMax1 = line->edge[normAfterEnd];
              yMin1 = line->yMin;
              yMax1 = line->yMax;
              break;
            case 1:
              xMin1 = line->xMin;
              xMax1 = line->xMax;
              yMin1 = line->edge[normStart];
              yMax1 = line->edge[normAfterEnd];
              break;
            case 2:
              xMin1 = line->edge[normAfterEnd];
              xMax1 = line->edge[normStart];
              yMin1 = line->yMin;
              yMax1 = line->yMax;
              break;
            case 3:
              xMin1 = line->xMin;
              xMax1 = line->xMax;
              yMin1 = line->edge[normAfterEnd];
              yMax1 = line->edge[normStart];
              break;
            }
            if (backward) {
              if ((startAtTop ||
                   yMin1 < yStart || (yMin1 == yStart && xMin1 < xStart)) &&
                  (stopAtBottom ||
                   yMin1 > yStop || (yMin1 == yStop && xMin1 > xStop))) {
                if (!found ||
                    yMin1 > yMin0 || (yMin1 == yMin0 && xMin1 > xMin0)) {
                  xMin0 = xMin1;
                  xMax0 = xMax1;
                  yMin0 = yMin1;
                  yMax0 = yMax1;
                  found = gTrue;
                }
              }
            } else {
              if ((startAtTop ||
                   yMin1 > yStart || (yMin1 == yStart && xMin1 > xStart)) &&
                  (stopAtBottom ||
                   yMin1 < yStop || (yMin1 == yStop && xMin1 < xStop))) {
                if (!found ||
                    yMin1 < yMin0 || (yMin1 == yMin0 && xMin1 < xMin0)) {
                  xMin0 = xMin1;
                  xMax0 = xMax1;
                  yMin0 = yMin1;
                  yMax0 = yMax1;
                  found = gTrue;
                }
              }
            }
          }
	}
	if (backward) {
	  --j;
	  --p;
	} else {
	  ++j;
	  ++p;
	}
      }
    }
  }

  gfree(s2);
  gfree(reordered);
  if (!caseSensitive) {
    gfree(txt);
  }

  if (found) {
    *xMin = xMin0;
    *xMax = xMax0;
    *yMin = yMin0;
    *yMax = yMax0;
    lastFindXMin = xMin0;
    lastFindYMin = yMin0;
    haveLastFind = gTrue;
    return gTrue;
  }

  return gFalse;
}

GooString *TextPage::getText(double xMin, double yMin,
			   double xMax, double yMax) {
  GooString *s;
  UnicodeMap *uMap;
  TextBlock *blk;
  TextLine *line;
  TextLineFrag *frags;
  int nFrags, fragsSize;
  TextLineFrag *frag;
  char space[8], eol[16];
  int spaceLen, eolLen;
  int lastRot;
  double x, y, delta;
  int col, idx0, idx1, i, j;
  GBool multiLine, oneRot;

  s = new GooString();

  // get the output encoding
  if (!(uMap = globalParams->getTextEncoding())) {
    return s;
  }

  if (rawOrder) {
    TextWord*  word;
    char mbc[16];
    int  mbc_len;

    for (word = rawWords; word && word <= rawLastWord; word = word->next) {
      for (j = 0; j < word->getLength(); ++j) {
        double gXMin, gXMax, gYMin, gYMax;
        word->getCharBBox(j, &gXMin, &gYMin, &gXMax, &gYMax);
        if (xMin <= gXMin && gXMax <= xMax && yMin <= gYMin && gYMax <= yMax)
        {
          mbc_len = uMap->mapUnicode( *(word->getChar(j)), mbc, sizeof(mbc) );
          s->append(mbc, mbc_len);
        }
      }
    }
    return s;
  }

  spaceLen = uMap->mapUnicode(0x20, space, sizeof(space));
  eolLen = 0; // make gcc happy
  switch (globalParams->getTextEOL()) {
  case eolUnix:
    eolLen = uMap->mapUnicode(0x0a, eol, sizeof(eol));
    break;
  case eolDOS:
    eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
    eolLen += uMap->mapUnicode(0x0a, eol + eolLen, sizeof(eol) - eolLen);
    break;
  case eolMac:
    eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
    break;
  }

  //~ writing mode (horiz/vert)

  // collect the line fragments that are in the rectangle
  fragsSize = 256;
  frags = (TextLineFrag *)gmallocn(fragsSize, sizeof(TextLineFrag));
  nFrags = 0;
  lastRot = -1;
  oneRot = gTrue;
  for (i = 0; i < nBlocks; ++i) {
    blk = blocks[i];
    if (xMin < blk->xMax && blk->xMin < xMax &&
	yMin < blk->yMax && blk->yMin < yMax) {
      for (line = blk->lines; line; line = line->next) {
	if (xMin < line->xMax && line->xMin < xMax &&
	    yMin < line->yMax && line->yMin < yMax) {
	  idx0 = idx1 = -1;
	  switch (line->rot) {
	  case 0:
	    y = 0.5 * (line->yMin + line->yMax);
	    if (yMin < y && y < yMax) {
	      j = 0;
	      while (j < line->len) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) > xMin) {
		  idx0 = j;
		  break;
		}
		++j;
	      }
	      j = line->len - 1;
	      while (j >= 0) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) < xMax) {
		  idx1 = j;
		  break;
		}
		--j;
	      }
	    }
	    break;
	  case 1:
	    x = 0.5 * (line->xMin + line->xMax);
	    if (xMin < x && x < xMax) {
	      j = 0;
	      while (j < line->len) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) > yMin) {
		  idx0 = j;
		  break;
		}
		++j;
	      }
	      j = line->len - 1;
	      while (j >= 0) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) < yMax) {
		  idx1 = j;
		  break;
		}
		--j;
	      }
	    }
	    break;
	  case 2:
	    y = 0.5 * (line->yMin + line->yMax);
	    if (yMin < y && y < yMax) {
	      j = 0;
	      while (j < line->len) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) < xMax) {
		  idx0 = j;
		  break;
		}
		++j;
	      }
	      j = line->len - 1;
	      while (j >= 0) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) > xMin) {
		  idx1 = j;
		  break;
		}
		--j;
	      }
	    }
	    break;
	  case 3:
	    x = 0.5 * (line->xMin + line->xMax);
	    if (xMin < x && x < xMax) {
	      j = 0;
	      while (j < line->len) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) < yMax) {
		  idx0 = j;
		  break;
		}
		++j;
	      }
	      j = line->len - 1;
	      while (j >= 0) {
		if (0.5 * (line->edge[j] + line->edge[j+1]) > yMin) {
		  idx1 = j;
		  break;
		}
		--j;
	      }
	    }
	    break;
	  }
	  if (idx0 >= 0 && idx1 >= 0) {
	    if (nFrags == fragsSize) {
	      fragsSize *= 2;
	      frags = (TextLineFrag *)
		          greallocn(frags, fragsSize, sizeof(TextLineFrag));
	    }
	    frags[nFrags].init(line, idx0, idx1 - idx0 + 1);
	    ++nFrags;
	    if (lastRot >= 0 && line->rot != lastRot) {
	      oneRot = gFalse;
	    }
	    lastRot = line->rot;
	  }
	}
      }
    }
  }

  // sort the fragments and generate the string
  if (nFrags > 0) {

    for (i = 0; i < nFrags; ++i) {
      frags[i].computeCoords(oneRot);
    }
    assignColumns(frags, nFrags, oneRot);

    // if all lines in the region have the same rotation, use it;
    // otherwise, use the page's primary rotation
    if (oneRot) {
      qsort(frags, nFrags, sizeof(TextLineFrag),
	    &TextLineFrag::cmpYXLineRot);
    } else {
      qsort(frags, nFrags, sizeof(TextLineFrag),
	    &TextLineFrag::cmpYXPrimaryRot);
    }
    i = 0;
    while (i < nFrags) {
      delta = maxIntraLineDelta * frags[i].line->words->fontSize;
      for (j = i+1;
	   j < nFrags && fabs(frags[j].base - frags[i].base) < delta;
	   ++j) ;
      qsort(frags + i, j - i, sizeof(TextLineFrag),
	    oneRot ? &TextLineFrag::cmpXYColumnLineRot
	           : &TextLineFrag::cmpXYColumnPrimaryRot);
      i = j;
    }

    col = 0;
    multiLine = gFalse;
    for (i = 0; i < nFrags; ++i) {
      frag = &frags[i];

      // insert a return
      if (frag->col < col ||
	  (i > 0 && fabs(frag->base - frags[i-1].base) >
	              maxIntraLineDelta * frags[i-1].line->words->fontSize)) {
	s->append(eol, eolLen);
	col = 0;
	multiLine = gTrue;
      }

      // column alignment
      for (; col < frag->col; ++col) {
	s->append(space, spaceLen);
      }

      // get the fragment text
      col += dumpFragment(frag->line->text + frag->start, frag->len, uMap, s);
    }

    if (multiLine) {
      s->append(eol, eolLen);
    }
  }

  gfree(frags);
  uMap->decRefCnt();

  return s;
}

class TextSelectionVisitor {
public:
  TextSelectionVisitor (TextPage *page);
  virtual ~TextSelectionVisitor () { }
  TextSelectionVisitor(const TextSelectionVisitor &) = delete;
  TextSelectionVisitor& operator=(const TextSelectionVisitor &) = delete;
  virtual void visitBlock (TextBlock *block,
			   TextLine *begin,
			   TextLine *end,
			   PDFRectangle *selection) = 0;
  virtual void visitLine (TextLine *line, 
			  TextWord *begin,
			  TextWord *end,
			  int edge_begin,
			  int edge_end,
			  PDFRectangle *selection) = 0;
  virtual void visitWord (TextWord *word, int begin, int end,
			  PDFRectangle *selection) = 0;

protected:
  TextPage *page;
};

TextSelectionVisitor::TextSelectionVisitor (TextPage *page)
  : page(page)
{
}


class TextSelectionDumper : public TextSelectionVisitor {
public:
  TextSelectionDumper(TextPage *page);
  ~TextSelectionDumper();

  void visitBlock (TextBlock *block, 
			   TextLine *begin,
			   TextLine *end,
			   PDFRectangle *selection) override { };
  void visitLine (TextLine *line,
			  TextWord *begin,
			  TextWord *end,
			  int edge_begin,
			  int edge_end,
			  PDFRectangle *selection) override;
  void visitWord (TextWord *word, int begin, int end,
			  PDFRectangle *selection) override;
  void endPage();

  GooString *getText(void);
  GooList **takeWordList(int *nLines);

private:

  void startLine();
  void finishLine();

  GooList **lines;
  int nLines, linesSize;
  GooList *words;
  int tableId;
  TextBlock *currentBlock;
};

TextSelectionDumper::TextSelectionDumper(TextPage *page)
    : TextSelectionVisitor(page)
{
  linesSize = 256;
  lines = (GooList **)gmallocn(linesSize, sizeof(GooList *));
  nLines = 0;

  tableId = -1;
  currentBlock = nullptr;
  words = nullptr;
}

TextSelectionDumper::~TextSelectionDumper()
{
  for (int i = 0; i < nLines; i++)
    deleteGooList(lines[i], TextWordSelection);
  gfree(lines);
}

void TextSelectionDumper::startLine()
{
  finishLine();
  words = new GooList();
}

void TextSelectionDumper::finishLine()
{
  if (nLines == linesSize) {
    linesSize *= 2;
    lines = (GooList **)grealloc(lines, linesSize * sizeof(GooList *));
  }

  if (words && words->getLength() > 0)
    lines[nLines++] = words;
  else if (words)
    delete words;
  words = nullptr;
}

void TextSelectionDumper::visitLine (TextLine *line,
				     TextWord *begin,
				     TextWord *end,
				     int edge_begin,
				     int edge_end,
				     PDFRectangle *selection)
{
  TextLineFrag frag;

  frag.init(line, edge_begin, edge_end - edge_begin);

  if (tableId >= 0 && frag.line->blk->tableId < 0) {
    finishLine();

    tableId = -1;
    currentBlock = nullptr;
  }

  if (frag.line->blk->tableId >= 0) { // a table
    if (tableId == -1) {
      tableId = frag.line->blk->tableId;
      currentBlock = frag.line->blk;
    }

    if (currentBlock == frag.line->blk) { // the same block
      startLine();
    } else { // another block
      if (currentBlock->tableEnd) { // previous block ended its row
        startLine();
      }
      currentBlock = frag.line->blk;
    }
  } else { // not a table
    startLine();
  }
}

void TextSelectionDumper::visitWord (TextWord *word, int begin, int end,
                                     PDFRectangle *selection)
{
  words->append(new TextWordSelection(word, begin, end));
}

void TextSelectionDumper::endPage()
{
  finishLine();
}

GooString *TextSelectionDumper::getText (void)
{
  GooString *text;
  int i, j;
  UnicodeMap *uMap;
  char space[8], eol[16];
  int spaceLen, eolLen;

  text = new GooString();

  if (!(uMap = globalParams->getTextEncoding()))
    return text;

  spaceLen = uMap->mapUnicode(0x20, space, sizeof(space));
  eolLen = uMap->mapUnicode(0x0a, eol, sizeof(eol));

  for (i = 0; i < nLines; i++) {
    GooList *lineWords = lines[i];
    for (j = 0; j < lineWords->getLength(); j++) {
      TextWordSelection *sel = (TextWordSelection *)lineWords->get(j);

      page->dumpFragment (sel->word->text + sel->begin, sel->end - sel->begin, uMap, text);
      if (j < lineWords->getLength() - 1)
        text->append(space, spaceLen);
    }
    if (i < nLines - 1)
      text->append(eol, eolLen);
  }

  uMap->decRefCnt();

  return text;
}

GooList **TextSelectionDumper::takeWordList(int *nLinesOut)
{
  GooList **returnValue = lines;

  *nLinesOut = nLines;
  if (nLines == 0)
    return nullptr;

  nLines = 0;
  lines = nullptr;

  return returnValue;
}

class TextSelectionSizer : public TextSelectionVisitor {
public:
  TextSelectionSizer(TextPage *page, double scale);
  ~TextSelectionSizer() { }

  void visitBlock (TextBlock *block,
			   TextLine *begin,
			   TextLine *end,
			   PDFRectangle *selection) override { };
  void visitLine (TextLine *line, 
			  TextWord *begin,
			  TextWord *end,
			  int edge_begin,
			  int edge_end,
			  PDFRectangle *selection) override;
  void visitWord (TextWord *word, int begin, int end,
			  PDFRectangle *selection) override { };

  GooList *getRegion () { return list; }

private:
  GooList *list;
  double scale;
};

TextSelectionSizer::TextSelectionSizer(TextPage *page, double scale)
  : TextSelectionVisitor(page),
    scale(scale)
{
  list = new GooList();
}

void TextSelectionSizer::visitLine (TextLine *line, 
				    TextWord *begin,
				    TextWord *end,
				    int edge_begin,
				    int edge_end,
				    PDFRectangle *selection)
{
  PDFRectangle *rect;
  double x1, y1, x2, y2, margin;

  margin = (line->yMax - line->yMin) / 8;
  x1 = line->edge[edge_begin];
  y1 = line->yMin - margin;
  x2 = line->edge[edge_end];
  y2 = line->yMax + margin;

  rect = new PDFRectangle (floor (x1 * scale), 
			   floor (y1 * scale),
			   ceil (x2 * scale),
			   ceil (y2 * scale));
  list->append (rect);
}


class TextSelectionPainter : public TextSelectionVisitor {
public:
  TextSelectionPainter(TextPage *page,
		       double scale,
		       int rotation,
		       OutputDev *out,
		       GfxColor *box_color,
		       GfxColor *glyph_color);
  ~TextSelectionPainter();

  void visitBlock (TextBlock *block,
			   TextLine *begin,
			   TextLine *end,
			   PDFRectangle *selection) override { };
  void visitLine (TextLine *line, 
			  TextWord *begin,
			  TextWord *end,
			  int edge_begin,
			  int edge_end,
			  PDFRectangle *selection) override;
  void visitWord (TextWord *word, int begin, int end,
			  PDFRectangle *selection) override;
  void endPage();

private:
  OutputDev *out;
  GfxColor *box_color, *glyph_color;
  GfxState *state;
  GooList *selectionList;
  Matrix ctm, ictm;
};

TextSelectionPainter::TextSelectionPainter(TextPage *page,
					   double scale,
					   int rotation,
					   OutputDev *out,
					   GfxColor *box_color,
					   GfxColor *glyph_color)
  : TextSelectionVisitor(page),
    out(out),
    box_color(box_color),
    glyph_color(glyph_color)
{
  PDFRectangle box(0, 0, page->pageWidth, page->pageHeight);

  selectionList = new GooList();
  state = new GfxState(72 * scale, 72 * scale, &box, rotation, gFalse);

  state->getCTM(&ctm);
  ctm.invertTo(&ictm);

  out->startPage(0, state, nullptr);
  out->setDefaultCTM (state->getCTM());

  state->setFillColorSpace(new GfxDeviceRGBColorSpace());
  state->setFillColor(box_color);
  out->updateFillColor(state);
}

TextSelectionPainter::~TextSelectionPainter()
{
  deleteGooList(selectionList, TextWordSelection);
  delete state;
}

void TextSelectionPainter::visitLine (TextLine *line,
				      TextWord *begin,
				      TextWord *end,
				      int edge_begin,
				      int edge_end,
				      PDFRectangle *selection)
{
  double x1, y1, x2, y2, margin;

  margin = (line->yMax - line->yMin) / 8;
  x1 = floor (line->edge[edge_begin]);
  y1 = floor (line->yMin - margin);
  x2 = ceil (line->edge[edge_end]);
  y2 = ceil (line->yMax + margin);

  ctm.transform(line->edge[edge_begin], line->yMin - margin, &x1, &y1);
  ctm.transform(line->edge[edge_end], line->yMax + margin, &x2, &y2);

  x1 = floor (x1);
  y1 = floor (y1);
  x2 = ceil (x2);
  y2 = ceil (y2);

  ictm.transform(x1, y1, &x1, &y1);
  ictm.transform(x2, y2, &x2, &y2);

  state->moveTo(x1, y1);
  state->lineTo(x2, y1);
  state->lineTo(x2, y2);
  state->lineTo(x1, y2);
  state->closePath();
}

void TextSelectionPainter::visitWord (TextWord *word, int begin, int end,
				      PDFRectangle *selection)
{
  selectionList->append(new TextWordSelection(word, begin, end));
}

void TextSelectionPainter::endPage()
{
  out->fill(state);

  out->saveState(state);
  out->clip(state);

  state->clearPath();

  state->setFillColor(glyph_color);
  out->updateFillColor(state);

  for (int i = 0; i < selectionList->getLength(); i++) {
    TextWordSelection *sel = (TextWordSelection *) selectionList->get(i);
    int begin = sel->begin;

    while (begin < sel->end) {
      TextFontInfo *font = sel->word->font[begin];
      font->gfxFont->incRefCnt();
      Matrix *mat = &sel->word->textMat[begin];

      state->setTextMat(mat->m[0], mat->m[1], mat->m[2], mat->m[3], 0, 0);
      state->setFont(font->gfxFont, 1);
      out->updateFont(state);

      int fEnd = begin + 1;
      while (fEnd < sel->end && font->matches(sel->word->font[fEnd]) &&
	     mat->m[0] == sel->word->textMat[fEnd].m[0] &&
	     mat->m[1] == sel->word->textMat[fEnd].m[1] &&
	     mat->m[2] == sel->word->textMat[fEnd].m[2] &&
	     mat->m[3] == sel->word->textMat[fEnd].m[3])
	fEnd++;

      /* The only purpose of this string is to let the output device query
       * it's length.  Might want to change this interface later. */
      GooString *string = new GooString ((char *) sel->word->charcode, fEnd - begin);
      out->beginString(state, string);

      for (int i = begin; i < fEnd; i++) {
        if (i != begin && sel->word->charPos[i] == sel->word->charPos[i - 1])
          continue;

	out->drawChar(state, sel->word->textMat[i].m[4], sel->word->textMat[i].m[5], 0, 0, 0, 0,
		      sel->word->charcode[i], 1, nullptr, 0);
      }
      out->endString(state);
      delete string;
      begin = fEnd;
    }
  }

  out->restoreState(state);
  out->endPage ();
}

void TextWord::visitSelection(TextSelectionVisitor *visitor,
			      PDFRectangle *selection,
			      SelectionStyle style)
{
  int i, begin, end;
  double mid;

  begin = len;
  end = 0;
  for (i = 0; i < len; i++) {
    mid = (edge[i] + edge[i + 1]) / 2;
    if (selection->x1 < mid || selection->x2 < mid)
      if (i < begin)
	begin = i;
    if (mid < selection->x1 || mid < selection->x2)
      end = i + 1;
  }

  /* Skip empty selection. */
  if (end <= begin)
    return;

  visitor->visitWord (this, begin, end, selection);
}

void TextLine::visitSelection(TextSelectionVisitor *visitor,
			      PDFRectangle *selection,
			      SelectionStyle style) {
  TextWord *p, *begin, *end, *current;
  int i, edge_begin, edge_end;
  PDFRectangle child_selection;

  begin = nullptr;
  end = nullptr;
  current = nullptr;
  for (p = words; p != nullptr; p = p->next) {
    if (blk->page->primaryLR) {
      if ((selection->x1 < p->xMax) ||
	  (selection->x2 < p->xMax))
        if (begin == nullptr) 
	  begin = p;

      if (((selection->x1 > p->xMin) ||
	   (selection->x2 > p->xMin)) && (begin != nullptr)) {
        end = p->next;
        current = p;
      }
    } else {
      if ((selection->x1 > p->xMin) ||
	  (selection->x2 > p->xMin))
        if (begin == nullptr) 
	  begin = p;

      if (((selection->x1 < p->xMax) ||
	   (selection->x2 < p->xMax)) && (begin != nullptr)) {
        end = p->next;
        current = p;
      }
    }
  }

  if (!current)
    current = begin;
  
  child_selection = *selection;
  if (style == selectionStyleWord) {
    child_selection.x1 = begin ? begin->xMin : xMin;
    if (end && end->xMax != -1) {
      child_selection.x2 = current->xMax;
    } else {
      child_selection.x2 = xMax;
    }
  }

  edge_begin = len;
  edge_end = 0;
  for (i = 0; i < len; i++) {
    double mid = (edge[i] + edge[i + 1]) /  2;
    if (child_selection.x1 < mid || child_selection.x2 < mid)
      if (i < edge_begin)
	edge_begin = i;
    if (mid < child_selection.x2 || mid < child_selection.x1)
      edge_end = i + 1;
  }

  /* Skip empty selection. */
  if (edge_end <= edge_begin)
    return;

  visitor->visitLine (this, begin, end, edge_begin, edge_end,
		      &child_selection);

  for (p = begin; p != end; p = p->next)
    p->visitSelection (visitor, &child_selection, style);
}

void TextBlock::visitSelection(TextSelectionVisitor *visitor,
			       PDFRectangle *selection,
			       SelectionStyle style) {
  PDFRectangle child_selection;
  double x[2], y[2], d, best_d[2];
  TextLine *p, *best_line[2];
  int i, count = 0, best_count[2], start, stop;
  GBool all[2];

  x[0] = selection->x1;
  y[0] = selection->y1;
  x[1] = selection->x2;
  y[1] = selection->y2;

  for (i = 0; i < 2; i++) {
    // the first/last lines are often not nearest
    // the corners, so we have to force them to be
    // selected when the selection runs outside this
    // block.
    if (page->primaryLR) {
      all[i] = x[i] >= this->xMax && y[i] >= this->yMax;
      if (x[i] <= this->xMin && y[i] <= this->yMin) {
	best_line[i] = this->lines;
	best_count[i] = 1;
      } else {
	best_line[i] = nullptr;
	best_count[i] = 0;
      }
    } else {
      all[i] = x[i] <= this->xMin && y[i] >= this->yMax;
      if (x[i] >= this->xMax && y[i] <= this->yMin) {
	best_line[i] = this->lines;
	best_count[i] = 1;
      } else {
	best_line[i] = nullptr;
	best_count[i] = 0;
      }
    }
    best_d[i] = 0;
  }

  // find the nearest line to the selection points
  // using the manhattan distance.
  for (p = this->lines; p; p = p->next) {
    count++;
    for (i = 0; i < 2; i++) {
      d = fmax(p->xMin - x[i], 0.0) +
	fmax(x[i] - p->xMax, 0.0) +
	fmax(p->yMin - y[i], 0.0) +
	fmax(y[i] - p->yMax, 0.0);
      if (!best_line[i] || all[i] ||
	  d < best_d[i]) {
	best_line[i] = p;
	best_count[i] = count;
	best_d[i] = d;
      }
    }
  }
  // assert: best is always set.
  if (!best_line[0] || !best_line[1]) {
    return;
  }

  // Now decide which point was first.
  if (best_count[0] < best_count[1] ||
      (best_count[0] == best_count[1] &&
       y[0] < y[1])) {
    start = 0;
    stop = 1;
  } else {
    start = 1;
    stop = 0;
  }

  visitor->visitBlock(this, best_line[start], best_line[stop], selection);

  for (p = best_line[start]; p; p = p->next) {
    if (page->primaryLR) {
      child_selection.x1 = p->xMin;
      child_selection.x2 = p->xMax;
    } else {
      child_selection.x1 = p->xMax;
      child_selection.x2 = p->xMin;
    }
    child_selection.y1 = p->yMin;
    child_selection.y2 = p->yMax;
    if (style == selectionStyleLine) {
      if (p == best_line[start]) {
	child_selection.x1 = 0;
	child_selection.y1 = 0;
      }
      if (p == best_line[stop]) {
	child_selection.x2 = page->pageWidth;
	child_selection.y2 = page->pageHeight;
      }
    } else {
      if (p == best_line[start]) {
	child_selection.x1 = fmax(p->xMin, fmin(p->xMax, x[start]));
	child_selection.y1 = fmax(p->yMin, fmin(p->yMax, y[start]));
      }
      if (p == best_line[stop]) {
	child_selection.x2 = fmax(p->xMin, fmin(p->xMax, x[stop]));
	child_selection.y2 = fmax(p->yMin, fmin(p->yMax, y[stop]));
      }
    }
    p->visitSelection(visitor, &child_selection, style);
    if (p == best_line[stop]) {
      return;
    }
  }
}

void TextPage::visitSelection(TextSelectionVisitor *visitor,
			      PDFRectangle *selection,
			      SelectionStyle style)
{
  PDFRectangle child_selection;
  double x[2], y[2], d, best_d[2];
  double xMin, yMin, xMax, yMax;
  TextFlow *flow, *best_flow[2];
  TextBlock *blk, *best_block[2];
  int i, count = 0, best_count[2], start, stop;

  if (!flows)
    return;

  x[0] = selection->x1;
  y[0] = selection->y1;
  x[1] = selection->x2;
  y[1] = selection->y2;

  xMin = pageWidth;
  yMin = pageHeight;
  xMax = 0.0;
  yMax = 0.0;

  for (i = 0; i < 2; i++) {
    best_block[i] = nullptr;
    best_flow[i] = nullptr;
    best_count[i] = 0;
    best_d[i] = 0;
  }

  // find the nearest blocks to the selection points
  // using the manhattan distance.
  for (flow = flows; flow; flow = flow->next) {
    for (blk = flow->blocks; blk; blk = blk->next) {
      count++;
      // the first/last blocks in reading order are
      // often not the closest to the page corners;
      // track the corners, force those blocks to
      // be selected if the selection runs across
      // multiple pages.
      xMin = fmin(xMin, blk->xMin);
      yMin = fmin(yMin, blk->yMin);
      xMax = fmax(xMax, blk->xMax);
      yMax = fmax(yMax, blk->yMax);
      for (i = 0; i < 2; i++) {
	d = fmax(blk->xMin - x[i], 0.0) +
	  fmax(x[i] - blk->xMax, 0.0) +
	  fmax(blk->yMin - y[i], 0.0) +
	  fmax(y[i] - blk->yMax, 0.0);
	if (!best_block[i] ||
	    d < best_d[i] ||
	    (!blk->next && !flow->next &&
	     x[i] >= fmin(xMax, pageWidth) &&
	     y[i] >= fmin(yMax, pageHeight))) {
	  best_block[i] = blk;
	  best_flow[i] = flow;
	  best_count[i] = count;
	  best_d[i] = d;
	}
      }
    }
  }
  for (i = 0; i < 2; i++) {
    if (primaryLR) {
      if (x[i] < xMin && y[i] < yMin) {
	best_block[i] = flows->blocks;
	best_flow[i] = flows;
	best_count[i] = 1;
      }
    } else {
      if (x[i] > xMax && y[i] < yMin) {
	best_block[i] = flows->blocks;
	best_flow[i] = flows;
	best_count[i] = 1;
      }
    }
  }
  // assert: best is always set.
  if (!best_block[0] || !best_block[1]) {
    return;
  }

  // Now decide which point was first.
  if (best_count[0] < best_count[1] ||
      (best_count[0] == best_count[1] && y[0] < y[1])) {
    start = 0;
    stop = 1;
  } else {
    start = 1;
    stop = 0;
  }

  for (flow = best_flow[start]; flow; flow = flow->next) {
    if (flow == best_flow[start]) {
      blk = best_block[start];
    } else {
      blk = flow->blocks;
    }
    for (; blk; blk = blk->next) {
      if (primaryLR) {
	child_selection.x1 = blk->xMin;
	child_selection.x2 = blk->xMax;
      } else {
	child_selection.x1 = blk->xMax;
	child_selection.x2 = blk->xMin;
      }
      child_selection.y1 = blk->yMin;
      child_selection.y2 = blk->yMax;
      if (blk == best_block[start]) {
	child_selection.x1 = fmax(blk->xMin, fmin(blk->xMax, x[start]));
	child_selection.y1 = fmax(blk->yMin, fmin(blk->yMax, y[start]));
      }
      if (blk == best_block[stop]) {
	child_selection.x2 = fmax(blk->xMin, fmin(blk->xMax, x[stop]));
	child_selection.y2 = fmax(blk->yMin, fmin(blk->yMax, y[stop]));
	blk->visitSelection(visitor, &child_selection, style);
	return;
      }
      blk->visitSelection(visitor, &child_selection, style);
    }
  }
}

void TextPage::drawSelection(OutputDev *out,
			     double scale,
			     int rotation,
			     PDFRectangle *selection,
			     SelectionStyle style,
			     GfxColor *glyph_color, GfxColor *box_color)
{
  TextSelectionPainter painter(this, scale, rotation, 
			       out, box_color, glyph_color);

  visitSelection(&painter, selection, style);
  painter.endPage();
}

GooList *TextPage::getSelectionRegion(PDFRectangle *selection,
				      SelectionStyle style,
				      double scale) {
  TextSelectionSizer sizer(this, scale);

  visitSelection(&sizer, selection, style);

  return sizer.getRegion();
}

GooString *TextPage::getSelectionText(PDFRectangle *selection,
				      SelectionStyle style)
{
  TextSelectionDumper dumper(this);

  visitSelection(&dumper, selection, style);
  dumper.endPage();

  return dumper.getText();
}

GooList **TextPage::getSelectionWords(PDFRectangle *selection,
                                      SelectionStyle style,
                                      int *nLines)
{
  TextSelectionDumper dumper(this);

  visitSelection(&dumper, selection, style);
  dumper.endPage();

  return dumper.takeWordList(nLines);
}

GBool TextPage::findCharRange(int pos, int length,
			      double *xMin, double *yMin,
			      double *xMax, double *yMax) {
  TextBlock *blk;
  TextLine *line;
  TextWord *word;
  double xMin0, xMax0, yMin0, yMax0;
  double xMin1, xMax1, yMin1, yMax1;
  GBool first;
  int i, j0, j1;

  if (rawOrder) {
    return gFalse;
  }

  //~ this doesn't correctly handle ranges split across multiple lines
  //~ (the highlighted region is the bounding box of all the parts of
  //~ the range)
  first = gTrue;
  xMin0 = xMax0 = yMin0 = yMax0 = 0; // make gcc happy
  xMin1 = xMax1 = yMin1 = yMax1 = 0; // make gcc happy
  for (i = 0; i < nBlocks; ++i) {
    blk = blocks[i];
    for (line = blk->lines; line; line = line->next) {
      for (word = line->words; word; word = word->next) {
	if (pos < word->charPos[word->len] &&
	    pos + length > word->charPos[0]) {
	  for (j0 = 0;
	       j0 < word->len && pos >= word->charPos[j0 + 1];
	       ++j0) ;
	  for (j1 = word->len - 1;
	       j1 > j0 && pos + length <= word->charPos[j1];
	       --j1) ;
	  switch (line->rot) {
	  case 0:
	    xMin1 = word->edge[j0];
	    xMax1 = word->edge[j1 + 1];
	    yMin1 = word->yMin;
	    yMax1 = word->yMax;
	    break;
	  case 1:
	    xMin1 = word->xMin;
	    xMax1 = word->xMax;
	    yMin1 = word->edge[j0];
	    yMax1 = word->edge[j1 + 1];
	    break;
	  case 2:
	    xMin1 = word->edge[j1 + 1];
	    xMax1 = word->edge[j0];
	    yMin1 = word->yMin;
	    yMax1 = word->yMax;
	    break;
	  case 3:
	    xMin1 = word->xMin;
	    xMax1 = word->xMax;
	    yMin1 = word->edge[j1 + 1];
	    yMax1 = word->edge[j0];
	    break;
	  }
	  if (first || xMin1 < xMin0) {
	    xMin0 = xMin1;
	  }
	  if (first || xMax1 > xMax0) {
	    xMax0 = xMax1;
	  }
	  if (first || yMin1 < yMin0) {
	    yMin0 = yMin1;
	  }
	  if (first || yMax1 > yMax0) {
	    yMax0 = yMax1;
	  }
	  first = gFalse;
	}
      }
    }
  }
  if (!first) {
    *xMin = xMin0;
    *xMax = xMax0;
    *yMin = yMin0;
    *yMax = yMax0;
    return gTrue;
  }
  return gFalse;
}

void TextPage::dump(void *outputStream, TextOutputFunc outputFunc,
		    GBool physLayout) {
  UnicodeMap *uMap;
  TextFlow *flow;
  TextBlock *blk;
  TextLine *line;
  TextLineFrag *frags;
  TextWord *word;
  int nFrags, fragsSize;
  TextLineFrag *frag;
  char space[8], eol[16], eop[8];
  int spaceLen, eolLen, eopLen;
  GBool pageBreaks;
  GooString *s;
  double delta;
  int col, i, j, d, n;

  // get the output encoding
  if (!(uMap = globalParams->getTextEncoding())) {
    return;
  }
  spaceLen = uMap->mapUnicode(0x20, space, sizeof(space));
  eolLen = 0; // make gcc happy
  switch (globalParams->getTextEOL()) {
  case eolUnix:
    eolLen = uMap->mapUnicode(0x0a, eol, sizeof(eol));
    break;
  case eolDOS:
    eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
    eolLen += uMap->mapUnicode(0x0a, eol + eolLen, sizeof(eol) - eolLen);
    break;
  case eolMac:
    eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
    break;
  }
  eopLen = uMap->mapUnicode(0x0c, eop, sizeof(eop));
  pageBreaks = globalParams->getTextPageBreaks();

  //~ writing mode (horiz/vert)

  // output the page in raw (content stream) order
  if (rawOrder) {

    for (word = rawWords; word; word = word->next) {
      s = new GooString();
      dumpFragment(word->text, word->len, uMap, s);
      (*outputFunc)(outputStream, s->getCString(), s->getLength());
      delete s;
      if (word->next &&
	  fabs(word->next->base - word->base) <
	    maxIntraLineDelta * word->fontSize &&
	  word->next->xMin >
	    word->xMax - minDupBreakOverlap * word->fontSize) {
	if (word->next->xMin > word->xMax + minWordSpacing * word->fontSize) {
	  (*outputFunc)(outputStream, space, spaceLen);
	}
      } else {
	(*outputFunc)(outputStream, eol, eolLen);
      }
    }

  // output the page, maintaining the original physical layout
  } else if (physLayout) {

    // collect the line fragments for the page and sort them
    fragsSize = 256;
    frags = (TextLineFrag *)gmallocn(fragsSize, sizeof(TextLineFrag));
    nFrags = 0;
    for (i = 0; i < nBlocks; ++i) {
      blk = blocks[i];
      for (line = blk->lines; line; line = line->next) {
	if (nFrags == fragsSize) {
	  fragsSize *= 2;
	  frags = (TextLineFrag *)greallocn(frags,
					    fragsSize, sizeof(TextLineFrag));
	}
	frags[nFrags].init(line, 0, line->len);
	frags[nFrags].computeCoords(gTrue);
	++nFrags;
      }
    }
    qsort(frags, nFrags, sizeof(TextLineFrag), &TextLineFrag::cmpYXPrimaryRot);
    i = 0;
    while (i < nFrags) {
      delta = maxIntraLineDelta * frags[i].line->words->fontSize;
      for (j = i+1;
	   j < nFrags && fabs(frags[j].base - frags[i].base) < delta;
	   ++j) ;
      qsort(frags + i, j - i, sizeof(TextLineFrag),
	    &TextLineFrag::cmpXYColumnPrimaryRot);
      i = j;
    }

#if 0 // for debugging
    printf("*** line fragments ***\n");
    for (i = 0; i < nFrags; ++i) {
      frag = &frags[i];
      printf("frag: x=%.2f..%.2f y=%.2f..%.2f base=%.2f '",
	     frag->xMin, frag->xMax, frag->yMin, frag->yMax, frag->base);
      for (n = 0; n < frag->len; ++n) {
	fputc(frag->line->text[frag->start + n] & 0xff, stdout);
      }
      printf("'\n");
    }
    printf("\n");
#endif

    // generate output
    col = 0;
    for (i = 0; i < nFrags; ++i) {
      frag = &frags[i];

      // column alignment
      for (; col < frag->col; ++col) {
	(*outputFunc)(outputStream, space, spaceLen);
      }

      // print the line
      s = new GooString();
      col += dumpFragment(frag->line->text + frag->start, frag->len, uMap, s);
      (*outputFunc)(outputStream, s->getCString(), s->getLength());
      delete s;

      // print one or more returns if necessary
      if (i == nFrags - 1 ||
	  frags[i+1].col < col ||
	  fabs(frags[i+1].base - frag->base) >
	    maxIntraLineDelta * frag->line->words->fontSize) {
	if (i < nFrags - 1) {
	  d = (int)((frags[i+1].base - frag->base) /
		    frag->line->words->fontSize);
	  if (d < 1) {
	    d = 1;
	  } else if (d > 5) {
	    d = 5;
	  }
	} else {
	  d = 1;
	}
	for (; d > 0; --d) {
	  (*outputFunc)(outputStream, eol, eolLen);
	}
	col = 0;
      }
    }

    gfree(frags);

  // output the page, "undoing" the layout
  } else {
    for (flow = flows; flow; flow = flow->next) {
      for (blk = flow->blocks; blk; blk = blk->next) {
	for (line = blk->lines; line; line = line->next) {
	  n = line->len;
	  if (line->hyphenated && (line->next || blk->next)) {
	    --n;
	  }
	  s = new GooString();
	  dumpFragment(line->text, n, uMap, s);
	  (*outputFunc)(outputStream, s->getCString(), s->getLength());
	  delete s;
	  // output a newline when a hyphen is not suppressed
	  if (n == line->len) {
	    (*outputFunc)(outputStream, eol, eolLen);
	  }
	}
      }
      (*outputFunc)(outputStream, eol, eolLen);
    }
  }

  // end of page
  if (pageBreaks) {
    (*outputFunc)(outputStream, eop, eopLen);
  }

  uMap->decRefCnt();
}

void TextPage::setMergeCombining(GBool merge) {
  mergeCombining = merge;
}

void TextPage::assignColumns(TextLineFrag *frags, int nFrags, GBool oneRot) {
  TextLineFrag *frag0, *frag1;
  int rot, col1, col2, i, j, k;

  // all text in the region has the same rotation -- recompute the
  // column numbers based only on the text in the region
  if (oneRot) {
    qsort(frags, nFrags, sizeof(TextLineFrag), &TextLineFrag::cmpXYLineRot);
    rot = frags[0].line->rot;
    for (i = 0; i < nFrags; ++i) {
      frag0 = &frags[i];
      col1 = 0;
      for (j = 0; j < i; ++j) {
	frag1 = &frags[j];
	col2 = 0; // make gcc happy
	switch (rot) {
	case 0:
	  if (frag0->xMin >= frag1->xMax) {
	    col2 = frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start]) + 1;
	  } else {
	    for (k = frag1->start;
		 k < frag1->start + frag1->len &&
		   frag0->xMin >= 0.5 * (frag1->line->edge[k] +
					 frag1->line->edge[k+1]);
		 ++k) ;
	    col2 = frag1->col +
	           frag1->line->col[k] - frag1->line->col[frag1->start];
	  }
	  break;
	case 1:
	  if (frag0->yMin >= frag1->yMax) {
	    col2 = frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start]) + 1;
	  } else {
	    for (k = frag1->start;
		 k < frag1->start + frag1->len &&
		   frag0->yMin >= 0.5 * (frag1->line->edge[k] +
					 frag1->line->edge[k+1]);
		 ++k) ;
	    col2 = frag1->col +
	           frag1->line->col[k] - frag1->line->col[frag1->start];
	  }
	  break;
	case 2:
	  if (frag0->xMax <= frag1->xMin) {
	    col2 = frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start]) + 1;
	  } else {
	    for (k = frag1->start;
		 k < frag1->start + frag1->len &&
		   frag0->xMax <= 0.5 * (frag1->line->edge[k] +
					 frag1->line->edge[k+1]);
		 ++k) ;
	    col2 = frag1->col +
	           frag1->line->col[k] - frag1->line->col[frag1->start];
	  }
	  break;
	case 3:
	  if (frag0->yMax <= frag1->yMin) {
	    col2 = frag1->col + (frag1->line->col[frag1->start + frag1->len] -
				 frag1->line->col[frag1->start]) + 1;
	  } else {
	    for (k = frag1->start;
		 k < frag1->start + frag1->len &&
		   frag0->yMax <= 0.5 * (frag1->line->edge[k] +
					 frag1->line->edge[k+1]);
		 ++k) ;
	    col2 = frag1->col +
	           frag1->line->col[k] - frag1->line->col[frag1->start];
	  }
	  break;
	}
	if (col2 > col1) {
	  col1 = col2;
	}
      }
      frag0->col = col1;
    }

  // the region includes text at different rotations -- use the
  // globally assigned column numbers, offset by the minimum column
  // number (i.e., shift everything over to column 0)
  } else {
    col1 = frags[0].col;
    for (i = 1; i < nFrags; ++i) {
      if (frags[i].col < col1) {
	col1 = frags[i].col;
      }
    }
    for (i = 0; i < nFrags; ++i) {
      frags[i].col -= col1;
    }
  }
}

int TextPage::dumpFragment(Unicode *text, int len, UnicodeMap *uMap,
			   GooString *s) {
  if (uMap->isUnicode()) {
    return reorderText(text, len, uMap, primaryLR, s, nullptr);
  } else {
    int nCols = 0;

    char buf[8];
    int buflen = 0;

    for (int i = 0; i < len; ++i) {
      buflen = uMap->mapUnicode(text[i], buf, sizeof(buf));
      s->append(buf, buflen);
      nCols += buflen;
    }

    return nCols;
  }
}

#ifdef TEXTOUT_WORD_LIST
TextWordList *TextPage::makeWordList(GBool physLayout) {
  return new TextWordList(this, physLayout);
}
#endif

//------------------------------------------------------------------------
// ActualText
//------------------------------------------------------------------------
ActualText::ActualText(TextPage *out) {
  out->incRefCnt();
  text = out;
  actualText = nullptr;
  actualTextNBytes = 0;
}

ActualText::~ActualText() {
  if (actualText)
    delete actualText;
  text->decRefCnt();
}

void ActualText::addChar(GfxState *state, double x, double y,
			 double dx, double dy,
			 CharCode c, int nBytes, Unicode *u, int uLen) {
  if (!actualText) {
    text->addChar(state, x, y, dx, dy, c, nBytes, u, uLen);
    return;
  }

  // Inside ActualText span.
  if (!actualTextNBytes) {
    actualTextX0 = x;
    actualTextY0 = y;
  }
  actualTextX1 = x + dx;
  actualTextY1 = y + dy;
  actualTextNBytes += nBytes;
}

void ActualText::begin(GfxState *state, GooString *text) {
  if (actualText)
    delete actualText;
  actualText = new GooString(text);
  actualTextNBytes = 0;
}

void ActualText::end(GfxState *state) {
  // ActualText span closed. Output the span text and the
  // extents of all the glyphs inside the span

  if (actualTextNBytes) {
    Unicode *uni = nullptr;
    int length;

    // now that we have the position info for all of the text inside
    // the marked content span, we feed the "ActualText" back through
    // text->addChar()
    length = TextStringToUCS4(actualText, &uni);
    text->addChar(state, actualTextX0, actualTextY0,
                  actualTextX1 - actualTextX0, actualTextY1 - actualTextY0,
                  0, actualTextNBytes, uni, length);
    gfree(uni);
  }

  delete actualText;
  actualText = nullptr;
  actualTextNBytes = 0;
}

//------------------------------------------------------------------------
// TextOutputDev
//------------------------------------------------------------------------

static void TextOutputDev_outputToFile(void *stream, const char *text, int len) {
  fwrite(text, 1, len, (FILE *)stream);
}

TextOutputDev::TextOutputDev(char *fileName, GBool physLayoutA,
			     double fixedPitchA, GBool rawOrderA,
			     GBool append) {
  text = nullptr;
  physLayout = physLayoutA;
  fixedPitch = physLayout ? fixedPitchA : 0;
  rawOrder = rawOrderA;
  doHTML = gFalse;
  ok = gTrue;

  // open file
  needClose = gFalse;
  if (fileName) {
    if (!strcmp(fileName, "-")) {
      outputStream = stdout;
#ifdef _WIN32
      // keep DOS from munging the end-of-line characters
      setmode(fileno(stdout), O_BINARY);
#endif
    } else if ((outputStream = fopen(fileName, append ? "ab" : "wb"))) {
      needClose = gTrue;
    } else {
      error(errIO, -1, "Couldn't open text file '{0:s}'", fileName);
      ok = gFalse;
      actualText = nullptr;
      return;
    }
    outputFunc = &TextOutputDev_outputToFile;
  } else {
    outputStream = nullptr;
  }

  // set up text object
  text = new TextPage(rawOrderA);
  actualText = new ActualText(text);
}

TextOutputDev::TextOutputDev(TextOutputFunc func, void *stream,
			     GBool physLayoutA, double fixedPitchA,
			     GBool rawOrderA) {
  outputFunc = func;
  outputStream = stream;
  needClose = gFalse;
  physLayout = physLayoutA;
  fixedPitch = physLayout ? fixedPitchA : 0;
  rawOrder = rawOrderA;
  doHTML = gFalse;
  text = new TextPage(rawOrderA);
  actualText = new ActualText(text);
  ok = gTrue;
}

TextOutputDev::~TextOutputDev() {
  if (needClose) {
#ifdef MACOS
    ICS_MapRefNumAndAssign((short)((FILE *)outputStream)->handle);
#endif
    fclose((FILE *)outputStream);
  }
  if (text) {
    text->decRefCnt();
  }
  delete actualText;
}

void TextOutputDev::startPage(int pageNum, GfxState *state, XRef *xref) {
  text->startPage(state);
}

void TextOutputDev::endPage() {
  text->endPage();
  text->coalesce(physLayout, fixedPitch, doHTML);
  if (outputStream) {
    text->dump(outputStream, outputFunc, physLayout);
  }
}

void TextOutputDev::restoreState(GfxState *state) {
  text->updateFont(state);
}

void TextOutputDev::updateFont(GfxState *state) {
  text->updateFont(state);
}

void TextOutputDev::beginString(GfxState *state, GooString *s) {
}

void TextOutputDev::endString(GfxState *state) {
}

void TextOutputDev::drawChar(GfxState *state, double x, double y,
			     double dx, double dy,
			     double originX, double originY,
			     CharCode c, int nBytes, Unicode *u, int uLen) {
  actualText->addChar(state, x, y, dx, dy, c, nBytes, u, uLen);
}

void TextOutputDev::incCharCount(int nChars) {
  text->incCharCount(nChars);
}

void TextOutputDev::beginActualText(GfxState *state, GooString *text)
{
  actualText->begin(state, text);
}

void TextOutputDev::endActualText(GfxState *state)
{
  actualText->end(state);
}

void TextOutputDev::stroke(GfxState *state) {
  GfxPath *path;
  GfxSubpath *subpath;
  double x[2], y[2];

  if (!doHTML) {
    return;
  }
  path = state->getPath();
  if (path->getNumSubpaths() != 1) {
    return;
  }
  subpath = path->getSubpath(0);
  if (subpath->getNumPoints() != 2) {
    return;
  }
  state->transform(subpath->getX(0), subpath->getY(0), &x[0], &y[0]);
  state->transform(subpath->getX(1), subpath->getY(1), &x[1], &y[1]);

  // look for a vertical or horizontal line
  if (x[0] == x[1] || y[0] == y[1]) {
    text->addUnderline(x[0], y[0], x[1], y[1]);
  }
}

void TextOutputDev::fill(GfxState *state) {
  GfxPath *path;
  GfxSubpath *subpath;
  double x[5], y[5];
  double rx0, ry0, rx1, ry1, t;
  int i;

  if (!doHTML) {
    return;
  }
  path = state->getPath();
  if (path->getNumSubpaths() != 1) {
    return;
  }
  subpath = path->getSubpath(0);
  if (subpath->getNumPoints() != 5) {
    return;
  }
  for (i = 0; i < 5; ++i) {
    if (subpath->getCurve(i)) {
      return;
    }
    state->transform(subpath->getX(i), subpath->getY(i), &x[i], &y[i]);
  }

  // look for a rectangle
  if (x[0] == x[1] && y[1] == y[2] && x[2] == x[3] && y[3] == y[4] &&
      x[0] == x[4] && y[0] == y[4]) {
    rx0 = x[0];
    ry0 = y[0];
    rx1 = x[2];
    ry1 = y[1];
  } else if (y[0] == y[1] && x[1] == x[2] && y[2] == y[3] && x[3] == x[4] &&
	     x[0] == x[4] && y[0] == y[4]) {
    rx0 = x[0];
    ry0 = y[0];
    rx1 = x[1];
    ry1 = y[2];
  } else {
    return;
  }
  if (rx1 < rx0) {
    t = rx0;
    rx0 = rx1;
    rx1 = t;
  }
  if (ry1 < ry0) {
    t = ry0;
    ry0 = ry1;
    ry1 = t;
  }

  // skinny horizontal rectangle
  if (ry1 - ry0 < rx1 - rx0) {
    if (ry1 - ry0 < maxUnderlineWidth) {
      ry0 = 0.5 * (ry0 + ry1);
      text->addUnderline(rx0, ry0, rx1, ry0);
    }

  // skinny vertical rectangle
  } else {
    if (rx1 - rx0 < maxUnderlineWidth) {
      rx0 = 0.5 * (rx0 + rx1);
      text->addUnderline(rx0, ry0, rx0, ry1);
    }
  }
}

void TextOutputDev::eoFill(GfxState *state) {
  if (!doHTML) {
    return;
  }
  fill(state);
}

void TextOutputDev::processLink(AnnotLink *link) {
  double x1, y1, x2, y2;
  int xMin, yMin, xMax, yMax, x, y;

  if (!doHTML) {
    return;
  }
  link->getRect(&x1, &y1, &x2, &y2);
  cvtUserToDev(x1, y1, &x, &y);
  xMin = xMax = x;
  yMin = yMax = y;
  cvtUserToDev(x1, y2, &x, &y);
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  cvtUserToDev(x2, y1, &x, &y);
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  cvtUserToDev(x2, y2, &x, &y);
  if (x < xMin) {
    xMin = x;
  } else if (x > xMax) {
    xMax = x;
  }
  if (y < yMin) {
    yMin = y;
  } else if (y > yMax) {
    yMax = y;
  }
  text->addLink(xMin, yMin, xMax, yMax, link);
}

GBool TextOutputDev::findText(Unicode *s, int len,
			      GBool startAtTop, GBool stopAtBottom,
			      GBool startAtLast, GBool stopAtLast,
			      GBool caseSensitive, GBool backward,
			      GBool wholeWord,
			      double *xMin, double *yMin,
			      double *xMax, double *yMax) {
  return text->findText(s, len, startAtTop, stopAtBottom,
			startAtLast, stopAtLast,
			caseSensitive, backward, wholeWord,
			xMin, yMin, xMax, yMax);
}

GooString *TextOutputDev::getText(double xMin, double yMin,
				double xMax, double yMax) {
  return text->getText(xMin, yMin, xMax, yMax);
}

void TextOutputDev::drawSelection(OutputDev *out,
				  double scale,
				  int rotation,
				  PDFRectangle *selection,
				  SelectionStyle style,
				  GfxColor *glyph_color, GfxColor *box_color) {
  text->drawSelection(out, scale, rotation, selection, style, glyph_color, box_color);
}

GooList *TextOutputDev::getSelectionRegion(PDFRectangle *selection,
					   SelectionStyle style,
					   double scale) {
  return text->getSelectionRegion(selection, style, scale);
}

GooString *TextOutputDev::getSelectionText(PDFRectangle *selection,
					   SelectionStyle style)
{
  return text->getSelectionText(selection, style);
}

GBool TextOutputDev::findCharRange(int pos, int length,
				   double *xMin, double *yMin,
				   double *xMax, double *yMax) {
  return text->findCharRange(pos, length, xMin, yMin, xMax, yMax);
}

void TextOutputDev::setMergeCombining(GBool merge) {
  text->setMergeCombining(merge);
}

#ifdef TEXTOUT_WORD_LIST
TextWordList *TextOutputDev::makeWordList() {
  return text->makeWordList(physLayout);
}
#endif

TextPage *TextOutputDev::takeText() {
  TextPage *ret;

  ret = text;
  text = new TextPage(rawOrder);
  return ret;
}
