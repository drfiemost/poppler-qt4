//========================================================================
//
// Dict.cc
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
// Copyright (C) 2005 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2006 Krzysztof Kowalczyk <kkowalczyk@gmail.com>
// Copyright (C) 2007-2008 Julien Rebetez <julienr@svn.gnome.org>
// Copyright (C) 2008, 2010, 2013, 2014, 2017 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2010 Paweł Wiejacha <pawel.wiejacha@gmail.com>
// Copyright (C) 2012 Fabio D'Urso <fabiodurso@hotmail.it>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2014 Scott West <scott.gregory.west@gmail.com>
// Copyright (C) 2017 Adrian Johnson <ajohnson@redneon.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <algorithm>
#include <stddef.h>
#include <string.h>
#include "goo/gmem.h"
#include "Object.h"
#include "XRef.h"
#include "Dict.h"

#ifdef MULTITHREADED
#  define dictLocker()   MutexLocker locker(&mutex)
#else
#  define dictLocker()
#endif
//------------------------------------------------------------------------
// Dict
//------------------------------------------------------------------------

static const int SORT_LENGTH_LOWER_LIMIT = 32;

static inline bool cmpDictEntries(const DictEntry &e1, const DictEntry &e2)
{
  return strcmp(e1.key, e2.key) < 0;
}

static int binarySearch(const char *key, DictEntry *entries, int length)
{
  int first = 0;
  int end = length - 1;
  while (first <= end) {
    const int middle = (first + end) / 2;
    const int res = strcmp(key, entries[middle].key);
    if (res == 0) {
      return middle;
    } else if (res < 0) {
      end = middle - 1;
    } else {
      first = middle + 1;
    }
  }
  return -1;
}

Dict::Dict(XRef *xrefA) {
  xref = xrefA;
  entries = NULL;
  size = length = 0;
  ref = 1;
  sorted = gFalse;
#ifdef MULTITHREADED
  gInitMutex(&mutex);
#endif
}

Dict::Dict(Dict* dictA) {
  xref = dictA->xref;
  size = length = dictA->length;
  ref = 1;
#ifdef MULTITHREADED
  gInitMutex(&mutex);
#endif

  sorted = dictA->sorted;
  entries = (DictEntry *)gmallocn(size, sizeof(DictEntry));
  for (int i=0; i<length; i++) {
    entries[i].key = copyString(dictA->entries[i].key);
    entries[i].val.initNullAfterMalloc();
    entries[i].val = dictA->entries[i].val.copy();
  }
}

Dict *Dict::copy(XRef *xrefA) {
  dictLocker();
  Dict *dictA = new Dict(this);
  dictA->xref = xrefA;
  for (int i=0; i<length; i++) {
    if (dictA->entries[i].val.getType() == objDict) {
       Dict *copy = dictA->entries[i].val.getDict()->copy(xrefA);
       dictA->entries[i].val = Object(copy);
    }
  }
  return dictA;
}

Dict::~Dict() {
  int i;

  for (i = 0; i < length; ++i) {
    gfree(entries[i].key);
    entries[i].val.free();
  }
  gfree(entries);
#ifdef MULTITHREADED
  gDestroyMutex(&mutex);
#endif
}

int Dict::incRef() {
  dictLocker();
  ++ref;
  return ref;
}

int Dict::decRef() {
  dictLocker();
  --ref;
  return ref;
}

void Dict::add(char *key, Object &&val) {
  dictLocker();
  if (sorted) {
    // We use add on very few occasions so
    // virtually this will never be hit
    sorted = gFalse;
  }

  if (length == size) {
    if (length == 0) {
      size = 8;
    } else {
      size *= 2;
    }
    entries = (DictEntry *)greallocn(entries, size, sizeof(DictEntry));
  }
  entries[length].key = key;
  entries[length].val.initNullAfterMalloc();
  entries[length].val = std::move(val);
  ++length;
}

inline DictEntry *Dict::find(const char *key) const {
  if (!sorted && length >= SORT_LENGTH_LOWER_LIMIT)
  {
      dictLocker();
      sorted = gTrue;
      std::sort(entries, entries+length, cmpDictEntries);
  }

  if (sorted) {
    const int pos = binarySearch(key, entries, length);
    if (pos != -1) {
      return &entries[pos];
    }
  } else {
    int i;

    for (i = length - 1; i >=0; --i) {
      if (!strcmp(key, entries[i].key))
        return &entries[i];
    }
  }
  return NULL;
}

GBool Dict::hasKey(const char *key) const {
  return find(key) != NULL;
}

void Dict::remove(const char *key) {
  dictLocker();
  if (sorted) {
    const int pos = binarySearch(key, entries, length);
    if (pos != -1) {
      length -= 1;
      gfree(entries[pos].key);
      entries[pos].val.free();
      if (pos != length) {
        memmove(static_cast<void*>(&entries[pos]), &entries[pos + 1], (length - pos) * sizeof(DictEntry));
      }
    }
  } else {
    int i; 
    bool found = false;
    if(length == 0) {
      return;
    }

    for(i=0; i<length; i++) {
      if (!strcmp(key, entries[i].key)) {
        found = true;
        break;
      }
    }
    if(!found) {
      return;
    }
    //replace the deleted entry with the last entry
    gfree(entries[i].key);
    entries[i].val.free();
    length -= 1;
    if (i!=length) {
      //don't copy the last entry if it is deleted
      entries[i].key = entries[length].key;
      entries[i].val = std::move(entries[length].val);
    }
  }
}

void Dict::set(const char *key, Object &&val) {
  DictEntry *e;
  if (val.isNull()) {
    remove(key);
    return;
  }
  e = find (key);
  if (e) {
    dictLocker();
    e->val = std::move(val);
  } else {
    add (copyString(key), std::move(val));
  }
}


GBool Dict::is(const char *type) const {
  DictEntry *e;

  return (e = find("Type")) && e->val.isName(type);
}

Object Dict::lookup(const char *key, int recursion) const {
  DictEntry *e;

  return (e = find(key)) ? e->val.fetch(xref, recursion) : Object(objNull);
}

Object Dict::lookupNF(const char *key) const {
  DictEntry *e;

  return (e = find(key)) ? e->val.copy() : Object(objNull);
}

GBool Dict::lookupInt(const char *key, const char *alt_key, int *value) const
{
  GBool success = gFalse;
  Object obj1 = lookup ((char *) key);
  if (obj1.isNull () && alt_key != NULL) {
    obj1.free ();
    obj1 = lookup ((char *) alt_key);
  }
  if (obj1.isInt ()) {
    *value = obj1.getInt ();
    success = gTrue;
  }

  obj1.free ();

  return success;
}

char *Dict::getKey(int i) const {
  return entries[i].key;
}

Object Dict::getVal(int i) const {
  return entries[i].val.fetch(xref);
}

Object Dict::getValNF(int i) const {
  return entries[i].val.copy();
}
