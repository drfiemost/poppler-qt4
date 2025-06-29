//========================================================================
//
// Lexer.cc
//
// Copyright 1996-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2006-2010, 2012-2014, 2017, 2018 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2006 Krzysztof Kowalczyk <kkowalczyk@gmail.com>
// Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2012, 2013 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include "Lexer.h"
#include "Error.h"
#include "XRef.h"

//------------------------------------------------------------------------

// A '1' in this array means the character is white space.  A '1' or
// '2' means the character ends a name or command.
static const char specialChars[256] = {
  1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,   // 0x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 1x
  1, 0, 0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 2,   // 2x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0,   // 3x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 4x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 0,   // 5x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 6x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 0,   // 7x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 8x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 9x
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // ax
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // bx
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // cx
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // dx
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // ex
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0    // fx
};

static const int IntegerSafeLimit = (INT_MAX - 9) / 10;
static const long long LongLongSafeLimit = (LLONG_MAX - 9) / 10;

//------------------------------------------------------------------------
// Lexer
//------------------------------------------------------------------------

Lexer::Lexer(XRef *xrefA, Stream *str) {
  lookCharLastValueCached = LOOK_VALUE_NOT_CACHED;
  xref = xrefA;

  curStr = Object(str);
  streams = new Array(xref);
  streams->add(curStr.copy());
  strPtr = 0;
  freeArray = gTrue;
  curStr.streamReset();
}

Lexer::Lexer(XRef *xrefA, Object *obj) {
  lookCharLastValueCached = LOOK_VALUE_NOT_CACHED;
  xref = xrefA;

  if (obj->isStream()) {
    streams = new Array(xref);
    freeArray = gTrue;
    streams->add(obj->copy());
  } else {
    streams = obj->getArray();
    freeArray = gFalse;
  }
  strPtr = 0;
  if (streams->getLength() > 0) {
    curStr = streams->get(strPtr);
    if (curStr.isStream()) {
      curStr.streamReset();
    }
  }
}

Lexer::~Lexer() {
  if (curStr.isStream()) {
    curStr.streamClose();
  }
  if (freeArray) {
    delete streams;
  }
}

int Lexer::getChar(GBool comesFromLook) {
  int c;

  if (LOOK_VALUE_NOT_CACHED != lookCharLastValueCached) {
    c = lookCharLastValueCached;
    lookCharLastValueCached = LOOK_VALUE_NOT_CACHED;
    return c;
  }

  c = EOF;
  while (curStr.isStream() && (c = curStr.streamGetChar()) == EOF) {
    if (comesFromLook == gTrue) {
      return EOF;
    } else {
      curStr.streamClose();
      curStr = Object();
      ++strPtr;
      if (strPtr < streams->getLength()) {
        curStr = streams->get(strPtr);
        if (curStr.isStream()) {
            curStr.streamReset();
        }
      }
    }
  }
  return c;
}

int Lexer::lookChar() {
  
  if (LOOK_VALUE_NOT_CACHED != lookCharLastValueCached) {
    return lookCharLastValueCached;
  }
  lookCharLastValueCached = getChar(gTrue);
  if (lookCharLastValueCached == EOF) {
    lookCharLastValueCached = LOOK_VALUE_NOT_CACHED;
    return EOF;
  } else {
    return lookCharLastValueCached;
  }
}

Object Lexer::getObj(int objNum) {
  char *p;
  int c, c2;
  GBool comment, neg, done, overflownInteger, overflownLongLong;
  int numParen;
  int xi;
  long long xll = 0;
  double xf = 0, scale;
  GooString *s;
  int n, m;

  // skip whitespace and comments
  comment = gFalse;
  while (1) {
    if ((c = getChar()) == EOF) {
      return Object(objEOF);
    }
    if (comment) {
      if (c == '\r' || c == '\n')
	comment = gFalse;
    } else if (c == '%') {
      comment = gTrue;
    } else if (specialChars[c] != 1) {
      break;
    }
  }

  // start reading token
  switch (c) {

  // number
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
  case '+': case '-': case '.':
    overflownInteger = gFalse;
    overflownLongLong = gFalse;
    neg = gFalse;
    xi = 0;
    if (c == '-') {
      neg = gTrue;
    } else if (c == '.') {
      goto doReal;
    } else if (c != '+') {
      xi = c - '0';
    }
    while (1) {
      c = lookChar();
      if (isdigit(c)) {
	getChar();
	if (unlikely(overflownLongLong)) {
	  xf = xf * 10.0 + (c - '0');
	} else if (unlikely (overflownInteger)) {
	  if (unlikely(xll > LongLongSafeLimit) &&
	      (xll > (LLONG_MAX - (c - '0')) / 10.0)) {
	    overflownLongLong = gTrue;
	    xf = xll * 10.0 + (c - '0');
	  } else {
	    xll = xll * 10 + (c - '0');
	  }
	} else {
	  if (unlikely(xi > IntegerSafeLimit) &&
	      (xi > (INT_MAX - (c - '0')) / 10.0)) {
	    overflownInteger = gTrue;
	    xll = xi * 10LL + (c - '0');
	  } else {
	    xi = xi * 10 + (c - '0');
	  }
	}
      } else if (c == '.') {
	getChar();
	goto doReal;
      } else {
	break;
      }
    }
    if (neg) {
      xi = -xi;
      xll = -xll;
      xf = -xf;
    }
    if (unlikely(overflownInteger)) {
      if (overflownLongLong) {
        return Object(xf);
      } else {
        if (unlikely(xll == INT_MIN)) {
          return Object(static_cast<int>(INT_MIN));
        } else {
          return Object(xll);
        }
      }
    } else {
      return Object(xi);
    }
    break;
  doReal:
    if (likely(!overflownInteger)) {
      xf = xi;
    } else if (!overflownLongLong) {
      xf = xll;
    }
    scale = 0.1;
    while (1) {
      c = lookChar();
      if (c == '-') {
	// ignore minus signs in the middle of numbers to match
	// Adobe's behavior
	error(errSyntaxWarning, getPos(), "Badly formatted number");
	getChar();
	continue;
      }
      if (!isdigit(c)) {
	break;
      }
      getChar();
      xf = xf + scale * (c - '0');
      scale *= 0.1;
    }
    if (neg) {
      xf = -xf;
    }
    return Object(xf);
    break;

  // string
  case '(':
    p = tokBuf;
    n = 0;
    numParen = 1;
    done = gFalse;
    s = nullptr;
    do {
      c2 = EOF;
      switch (c = getChar()) {

      case EOF:
#if 0
      // This breaks some PDF files, e.g., ones from Photoshop.
      case '\r':
      case '\n':
#endif
	error(errSyntaxError, getPos(), "Unterminated string");
	done = gTrue;
	break;

      case '(':
	++numParen;
	c2 = c;
	break;

      case ')':
	if (--numParen == 0) {
	  done = gTrue;
	} else {
	  c2 = c;
	}
	break;

      case '\\':
	switch (c = getChar()) {
	case 'n':
	  c2 = '\n';
	  break;
	case 'r':
	  c2 = '\r';
	  break;
	case 't':
	  c2 = '\t';
	  break;
	case 'b':
	  c2 = '\b';
	  break;
	case 'f':
	  c2 = '\f';
	  break;
	case '\\':
	case '(':
	case ')':
	  c2 = c;
	  break;
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
	  c2 = c - '0';
	  c = lookChar();
	  if (c >= '0' && c <= '7') {
	    getChar();
	    c2 = (c2 << 3) + (c - '0');
	    c = lookChar();
	    if (c >= '0' && c <= '7') {
	      getChar();
	      c2 = (c2 << 3) + (c - '0');
	    }
	  }
	  break;
	case '\r':
	  c = lookChar();
	  if (c == '\n') {
	    getChar();
	  }
	  break;
	case '\n':
	  break;
	case EOF:
	  error(errSyntaxError, getPos(), "Unterminated string");
	  done = gTrue;
	  break;
	default:
	  c2 = c;
	  break;
	}
	break;

      default:
	c2 = c;
	break;
      }

      if (c2 != EOF) {
	if (n == tokBufSize) {
	  if (!s)
	    s = new GooString(tokBuf, tokBufSize);
	  else
	    s->append(tokBuf, tokBufSize);
	  p = tokBuf;
	  n = 0;
	  
	  // we are growing see if the document is not malformed and we are growing too much
	  if (objNum > 0 && xref != nullptr)
	  {
	    int newObjNum = xref->getNumEntry(curStr.streamGetPos());
	    if (newObjNum != objNum)
	    {
	      error(errSyntaxError, getPos(), "Unterminated string");
	      done = gTrue;
	      delete s;
	      n = -2;
	    }
	  }
	}
	*p++ = (char)c2;
	++n;
      }
    } while (!done);
    if (n >= 0) {
      if (!s)
        s = new GooString(tokBuf, n);
      else
        s->append(tokBuf, n);
      return Object(s);
    } else {
      return Object(objEOF);
    }
    break;

  // name
  case '/':
    p = tokBuf;
    n = 0;
    s = nullptr;
    while ((c = lookChar()) != EOF && !specialChars[c]) {
      getChar();
      if (c == '#') {
	c2 = lookChar();
	if (c2 >= '0' && c2 <= '9') {
	  c = c2 - '0';
	} else if (c2 >= 'A' && c2 <= 'F') {
	  c = c2 - 'A' + 10;
	} else if (c2 >= 'a' && c2 <= 'f') {
	  c = c2 - 'a' + 10;
	} else {
	  goto notEscChar;
	}
	getChar();
	c <<= 4;
	c2 = getChar();
	if (c2 >= '0' && c2 <= '9') {
	  c += c2 - '0';
	} else if (c2 >= 'A' && c2 <= 'F') {
	  c += c2 - 'A' + 10;
	} else if (c2 >= 'a' && c2 <= 'f') {
	  c += c2 - 'a' + 10;
	} else {
	  error(errSyntaxError, getPos(), "Illegal digit in hex char in name");
	}
      }
     notEscChar:
      // the PDF spec claims that names are limited to 127 chars, but
      // Distiller 8 will produce longer names, and Acrobat 8 will
      // accept longer names
      ++n;
      if (n < tokBufSize) {
	*p++ = c;
      } else if (n == tokBufSize) {
	error(errSyntaxError, getPos(), "Warning: name token is longer than what the specification says it can be");
	*p = c;
	s = new GooString(tokBuf, n);
      } else {
	s->append((char)c);
      }
    }
    if (n < tokBufSize) {
      *p = '\0';
      return Object(objName, tokBuf);
    } else {
      Object obj(objName, s->getCString());
      delete s;
      return obj;
    }
    break;

  // array punctuation
  case '[':
  case ']':
    tokBuf[0] = c;
    tokBuf[1] = '\0';
    return Object(objCmd, tokBuf);
    break;

  // hex string or dict punctuation
  case '<':
    c = lookChar();

    // dict punctuation
    if (c == '<') {
      getChar();
      tokBuf[0] = tokBuf[1] = '<';
      tokBuf[2] = '\0';
      return Object(objCmd, tokBuf);

    // hex string
    } else {
      p = tokBuf;
      m = n = 0;
      c2 = 0;
      s = nullptr;
      while (1) {
	c = getChar();
	if (c == '>') {
	  break;
	} else if (c == EOF) {
	  error(errSyntaxError, getPos(), "Unterminated hex string");
	  break;
	} else if (specialChars[c] != 1) {
	  c2 = c2 << 4;
	  if (c >= '0' && c <= '9')
	    c2 += c - '0';
	  else if (c >= 'A' && c <= 'F')
	    c2 += c - 'A' + 10;
	  else if (c >= 'a' && c <= 'f')
	    c2 += c - 'a' + 10;
	  else
	    error(errSyntaxError, getPos(), "Illegal character <{0:02x}> in hex string", c);
	  if (++m == 2) {
	    if (n == tokBufSize) {
	      if (!s)
		s = new GooString(tokBuf, tokBufSize);
	      else
		s->append(tokBuf, tokBufSize);
	      p = tokBuf;
	      n = 0;
	    }
	    *p++ = (char)c2;
	    ++n;
	    c2 = 0;
	    m = 0;
	  }
	}
      }
      if (!s)
	s = new GooString(tokBuf, n);
      else
	s->append(tokBuf, n);
      if (m == 1)
	s->append((char)(c2 << 4));
      return Object(s);
    }
    break;

  // dict punctuation
  case '>':
    c = lookChar();
    if (c == '>') {
      getChar();
      tokBuf[0] = tokBuf[1] = '>';
      tokBuf[2] = '\0';
      return Object(objCmd, tokBuf);
    } else {
      error(errSyntaxError, getPos(), "Illegal character '>'");
      return Object(objError);
    }
    break;

  // error
  case ')':
  case '{':
  case '}':
    error(errSyntaxError, getPos(), "Illegal character '{0:c}'", c);
    return Object(objError);
    break;

  // command
  default:
    p = tokBuf;
    *p++ = c;
    n = 1;
    while ((c = lookChar()) != EOF && !specialChars[c]) {
      getChar();
      if (++n == tokBufSize) {
	error(errSyntaxError, getPos(), "Command token too long");
	break;
      }
      *p++ = c;
    }
    *p = '\0';
    if (tokBuf[0] == 't' && !strcmp(tokBuf, "true")) {
      return Object(gTrue);
    } else if (tokBuf[0] == 'f' && !strcmp(tokBuf, "false")) {
      return Object(gFalse);
    } else if (tokBuf[0] == 'n' && !strcmp(tokBuf, "null")) {
      return Object(objNull);
    } else {
      return Object(objCmd, tokBuf);
    }
    break;
  }

  return Object();
}

Object Lexer::getObj(const char *cmdA, int objNum) {
  char *p;
  int c;
  GBool comment;
  int n;

  // skip whitespace and comments
  comment = gFalse;
  const char *cmd1 = tokBuf;
  *tokBuf = 0;
  while (strcmp(cmdA, cmd1) && (objNum < 0 || (xref && xref->getNumEntry(getPos()) == objNum))) {
    while (1) {
      if ((c = getChar()) == EOF) {
        return Object(objEOF);
      }
      if (comment) {
        if (c == '\r' || c == '\n') {
          comment = gFalse;
        }
      } else if (c == '%') {
        comment = gTrue;
      } else if (specialChars[c] != 1) {
        break;
      }
    }
    p = tokBuf;
    *p++ = c;
    n = 1;
    while ((c = lookChar()) != EOF && specialChars[c] == 0) {
      getChar();
      if (++n == tokBufSize) {
        break;
      }
      *p++ = c;
    }
    *p = '\0';
  }

  return Object(objCmd, tokBuf);
}

void Lexer::skipToNextLine() {
  int c;

  while (1) {
    c = getChar();
    if (c == EOF || c == '\n') {
      return;
    }
    if (c == '\r') {
      if ((c = lookChar()) == '\n') {
	getChar();
      }
      return;
    }
  }
}

GBool Lexer::isSpace(int c) {
  return c >= 0 && c <= 0xff && specialChars[c] == 1;
}
