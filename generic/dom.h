/*---------------------------------------------------------------------------
|   Copyright (C) 1999  Jochen C. Loewer (loewerj@hotmail.com)
+----------------------------------------------------------------------------
|
|   A DOM interface upon the expat XML parser for the C language
|   according to the W3C recommendation REC-DOM-Level-1-19981001
|
|
|   The contents of this file are subject to the Mozilla Public License
|   Version 2.0 (the "License"); you may not use this file except in
|   compliance with the License. You may obtain a copy of the License at
|   http://www.mozilla.org/MPL/
|
|   Software distributed under the License is distributed on an "AS IS"
|   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
|   License for the specific language governing rights and limitations
|   under the License.
|
|   The Original Code is tDOM.
|
|   The Initial Developer of the Original Code is Jochen Loewer
|   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|
|
|   written by Jochen Loewer
|   April 5, 1999
|
\--------------------------------------------------------------------------*/

#ifndef __DOM_H__
#define __DOM_H__

#include <tcl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <expat.h>
#include <domalloc.h>
#include <limits.h>

/*
 * tDOM provides it's own memory allocator which is optimized for
 * low heap usage. It uses the native Tcl allocator underneath,
 * though, but it is not very MT-friendly. Therefore, you might
 * use the (normal) Tcl allocator with USE_NORMAL_ALLOCATOR
 * defined during compile time. Actually, the symbols name is 
 * a misnomer. It should have benn called "USE_TCL_ALLOCATOR"
 * but I did not want to break any backward compatibility. 
 */

#ifndef USE_NORMAL_ALLOCATOR
# define MALLOC             malloc
# define FREE               free
# define REALLOC            realloc
# define tdomstrdup         strdup
#else
# define domAllocInit()
# define domAlloc           MALLOC 
# define domFree            FREE
# if defined(TCL_MEM_DEBUG) || defined(NS_AOLSERVER) 
#  define MALLOC            Tcl_Alloc
#  define FREE(a)           Tcl_Free((char*)(a))
#  define REALLOC           Tcl_Realloc
#  define tdomstrdup(s)     (char*)strcpy(MALLOC(strlen((s))+1),(char*)s)
# else    
#  define MALLOC            malloc
#  define FREE              free
#  define REALLOC           realloc
#  define tdomstrdup        strdup
# endif /* TCL_MEM_DEBUG */
#endif /* USE_NORMAL_ALLOCATOR */
#define TMALLOC(t) (t*)MALLOC(sizeof(t))

#if defined(TCL_MEM_DEBUG) || defined(NS_AOLSERVER) 
   static void* my_malloc(size_t size){return Tcl_Alloc(size);}
   static void  my_free(void *ptr){Tcl_Free((char*)ptr);}
   static void* my_realloc(void *ptr,size_t size){return Tcl_Realloc(ptr,size);}
   static XML_Memory_Handling_Suite memsuite = {
       my_malloc, my_realloc, my_free
   };
#  define MEM_SUITE &memsuite
#else
#  define MEM_SUITE NULL
#endif

/*
 * Beginning with 8.6, interp->errorLine isn't public visible anymore
 * (TIP 330)
 */
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION < 6)
# define Tcl_GetErrorLine(interp) (interp)->errorLine
#endif

/* Beginning with Tcl 9.0 the string representation of a Tcl_Obj may
 * be bigger thant 2 GByte. Therefore some API functions differ. */
#if TCL_MAJOR_VERSION > 8
#  ifdef _WIN32
#    define TCL_THREADS 1
#  endif
#  define domLength Tcl_Size
#  define Tcl_SetDomLengthObj Tcl_SetWideIntObj
#  define domLengthConversion "%" TCL_SIZE_MODIFIER "d"
#else
#  define domLength int
#  define Tcl_SetDomLengthObj Tcl_SetIntObj
#  define domLengthConversion "%d"
#  define TCL_SIZE_MODIFIER ""
#  define Tcl_GetSizeIntFromObj Tcl_GetIntFromObj
#endif

#ifndef TDOM_LS_MODIFIER
#  ifdef XML_LARGE_SIZE
#    ifdef _WIN32
#      define TDOM_LS_MODIFIER "I64"
#    else
#      define TDOM_LS_MODIFIER "ll"
#    endif
#  else
#    define TDOM_LS_MODIFIER "l"
#  endif
#endif

#ifndef TDOM_INLINE
#ifdef _MSC_VER
# define TDOM_INLINE __inline
#else
# define TDOM_INLINE inline
#endif
#endif

/* Since the len argument of XML_Parse() is of type int, parsing of
 * strings has to be done in chunks anyway for Tcl 9 with its strings
 * potentially longer than 2 GByte. Because of internal changes in
 * exapt a chunk size of INT_MAX led to out of memory errors.
 * (TDOM_PCS = TDOM_PARSE_CHUNK_SIZE) */

#ifndef TDOM_PCS
# define TDOM_PCS INT_MAX / 2
#endif

/* The following is the machinery to have an UNUSED macro which
 * signals that a function parameter is known to be not used. This
 * works for some open source compiler. */

/*
 * Define __GNUC_PREREQ for determine available features of gcc and clang
 */
#undef  __GNUC_PREREQ
#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define __GNUC_PREREQ(maj, min) (0)
#endif

/*
 * UNUSED uses the gcc __attribute__ unused and mangles the name, so the
 * attribute could not be used, if declared as unused.
 */
#ifdef UNUSED
#elif __GNUC_PREREQ(2, 7)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ (x)
#else
# define UNUSED(x) (x)
#endif

/* UNUSED stuff end */

#define domPanic(msg) Tcl_Panic((msg));

/*
 * If compiled against threaded Tcl core, we must take
 * some extra care about process-wide globals and the
 * way we name Tcl object accessor commands.
 */
#ifndef TCL_THREADS
  extern unsigned long domUniqueNodeNr;
  extern unsigned long domUniqueDocNr;
  extern Tcl_HashTable tdom_tagNames;
  extern Tcl_HashTable tdom_attrNames;
# define TDomNotThreaded(x) x
# define TDomThreaded(x)
# define HASHTAB(doc,tab)   tab
# define NODE_NO(doc)       ++domUniqueNodeNr
# define DOC_NO(doc)        ++domUniqueDocNr
#else
# define TDomNotThreaded(x)
# define TDomThreaded(x)    x
# define HASHTAB(doc,tab)   (doc)->tab
# define NODE_NO(doc)       ((doc)->nodeCounter)++
# define DOC_NO(doc)         (uintptr_t)(doc)
#endif /* TCL_THREADS */

#define DOC_CMD(s,doc)      sprintf((s), "domDoc%p", (void *)(doc))
#define NODE_CMD(s,node)    sprintf((s), "domNode%p", (void *)(node))
#define XSLT_CMD(s,doc)     sprintf((s), "XSLTcmd%p", (void *)(doc))

#define XML_NAMESPACE "http://www.w3.org/XML/1998/namespace"
#define XMLNS_NAMESPACE "http://www.w3.org/2000/xmlns"

#define UTF8_1BYTE_CHAR(c) ( 0    == ((c) & 0x80))
#define UTF8_2BYTE_CHAR(c) ( 0xC0 == ((c) & 0xE0))
#define UTF8_3BYTE_CHAR(c) ( 0xE0 == ((c) & 0xF0))
#define UTF8_4BYTE_CHAR(c) ( 0xF0 == ((c) & 0xF8))

#define UTF8_CHAR_LEN(c) \
  UTF8_1BYTE_CHAR((c)) ? 1 : \
   (UTF8_2BYTE_CHAR((c)) ? 2 : \
     (UTF8_3BYTE_CHAR((c)) ? 3 : \
       (UTF8_4BYTE_CHAR((c)) ? 4 : 0)))

/* The following 2 defines are out of the expat code */

/* A 2 byte UTF-8 representation splits the characters 11 bits
between the bottom 5 and 6 bits of the bytes.
We need 8 bits to index into pages, 3 bits to add to that index and
5 bits to generate the mask. */
#define UTF8_GET_NAMING2(pages, byte) \
    (namingBitmap[((pages)[(((byte)[0]) >> 2) & 7] << 3) \
                      + ((((byte)[0]) & 3) << 1) \
                      + ((((byte)[1]) >> 5) & 1)] \
         & (1u << (((byte)[1]) & 0x1F)))

/* A 3 byte UTF-8 representation splits the characters 16 bits
between the bottom 4, 6 and 6 bits of the bytes.
We need 8 bits to index into pages, 3 bits to add to that index and
5 bits to generate the mask. */
#define UTF8_GET_NAMING3(pages, byte) \
  (namingBitmap[((pages)[((((byte)[0]) & 0xF) << 4) \
                             + ((((byte)[1]) >> 2) & 0xF)] \
                       << 3) \
                      + ((((byte)[1]) & 3) << 1) \
                      + ((((byte)[2]) >> 5) & 1)] \
         & (1u << (((byte)[2]) & 0x1F)))

#define UTF8_GET_NAMING_NMTOKEN(p, n) \
  ((n) == 1 \
  ? nameChar7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(namePages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(namePages, (const unsigned char *)(p)) \
      : 0)))

#define UTF8_GET_NAMING_NCNMTOKEN(p, n) \
  ((n) == 1 \
  ? NCnameChar7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(namePages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(namePages, (const unsigned char *)(p)) \
      : 0)))

#define UTF8_GET_NAME_START(p, n) \
  ((n) == 1 \
  ? nameStart7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(nmstrtPages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(nmstrtPages, (const unsigned char *)(p)) \
      : 0)))

#define UTF8_GET_NCNAME_START(p, n) \
  ((n) == 1 \
  ? NCnameStart7Bit[(int)(*(p))] \
  : ((n) == 2 \
    ? UTF8_GET_NAMING2(nmstrtPages, (const unsigned char *)(p)) \
    : ((n) == 3 \
      ? UTF8_GET_NAMING3(nmstrtPages, (const unsigned char *)(p)) \
      : 0)))

#define UTF8_XMLCHAR3(p) \
  (*(p) == 0xED  \
   ? ((p)[1] < 0xA0 ? 1 : 0) \
   : (*(p) == 0xEF \
      ? ((p)[1] == 0xBF \
         ? ((p)[2] == 0xBE || (p)[2] == 0xBF ? 0 : 1) \
         : 1) \
      : 1)) \
    
/* This definition is lax in the sense, that it accepts every 4 byte
 * utf-8 character beyond #xFFFF as valid, no matter, if Unicode has
 * (so far) defined a character for that encoding point. Additionally,
 * this define does not care about the discouraged characters beyond
 * #xFFFF (but after all, they are only discouraged, not
 * forbidden). */
#define UTF8_XMLCHAR(p, n) \
  ((n) == 1 \
  ? CharBit[(int)(*(p))] \
  : ((n) == 2 \
    ? 1 \
    : ((n) == 3 \
      ? (UTF8_XMLCHAR3(p)) \
      : ((n) == 4 \
        ? 1 : 0))))

#include "../expat/nametab.h"

static const unsigned char nameChar7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
/* 0x30 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x38 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char NCnameChar7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
/* 0x30 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x38 */    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};


static const unsigned char nameStart7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x30 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x38 */    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};


static const unsigned char NCnameStart7Bit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x30 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x38 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
/* 0x60 */    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char CharBit[] = {
/* 0x00 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */    0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
/* 0x10 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x28 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x30 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x38 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x40 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x48 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x50 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x58 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x60 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x68 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x70 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
/* 0x78 */    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};


#define isNameStart(x)   UTF8_GET_NAME_START((x),UTF8_CHAR_LEN(*(x)))
#define isNCNameStart(x) UTF8_GET_NCNAME_START((x),UTF8_CHAR_LEN(*(x)))
#define isNameChar(x)    UTF8_GET_NAMING_NMTOKEN((x),UTF8_CHAR_LEN(*(x)))
#define isNCNameChar(x)  UTF8_GET_NAMING_NCNMTOKEN((x),UTF8_CHAR_LEN(*(x)))

#define IS_XML_WHITESPACE(c)  ((c)==' ' || (c)=='\n' || (c)=='\r' || (c)=='\t')
#define SPACE(c) IS_XML_WHITESPACE ((c))

/*--------------------------------------------------------------------------
|   DOMString
|
\-------------------------------------------------------------------------*/
typedef char* domString;   


/*--------------------------------------------------------------------------
|   domNodeType
|
\-------------------------------------------------------------------------*/
#if defined(_AIX) 
#    define    ELEMENT_NODE                 1
#    define    ATTRIBUTE_NODE               2
#    define    TEXT_NODE                    3
#    define    CDATA_SECTION_NODE           4
#    define    ENTITY_REFERENCE_NODE        5
#    define    ENTITY_NODE                  6
#    define    PROCESSING_INSTRUCTION_NODE  7
#    define    COMMENT_NODE                 8
#    define    DOCUMENT_NODE                9
#    define    DOCUMENT_TYPE_NODE           10
#    define    DOCUMENT_FRAGMENT_NODE       11
#    define    NOTATION_NODE                12
#    define    ALL_NODES                    100

#    define    domNodeType                  int

#else 

typedef enum {

    ELEMENT_NODE                = 1,
    ATTRIBUTE_NODE              = 2,
    TEXT_NODE                   = 3,
    CDATA_SECTION_NODE          = 4,
    ENTITY_REFERENCE_NODE       = 5,
    ENTITY_NODE                 = 6,
    PROCESSING_INSTRUCTION_NODE = 7,
    COMMENT_NODE                = 8,
    DOCUMENT_NODE               = 9,
    DOCUMENT_TYPE_NODE          = 10,
    DOCUMENT_FRAGMENT_NODE      = 11,
    NOTATION_NODE               = 12,
    ALL_NODES                   = 100
} domNodeType;

#endif

/* The following defines used for NodeObjCmd */
#define PARSER_NODE 9999 /* Hack so that we can invoke XML parser */
/* More hacked domNodeTypes - used to signal, that we want to check
   name/data of the node to create. */
#define ELEMENT_NODE_ANAME_CHK 10000
#define ELEMENT_NODE_AVALUE_CHK 10001
#define ELEMENT_NODE_CHK 10002
#define TEXT_NODE_CHK 10003
#define COMMENT_NODE_CHK 10004
#define CDATA_SECTION_NODE_CHK 10005
#define PROCESSING_INSTRUCTION_NODE_NAME_CHK 10006
#define PROCESSING_INSTRUCTION_NODE_VALUE_CHK 10007
#define PROCESSING_INSTRUCTION_NODE_CHK 10008


/*--------------------------------------------------------------------------
|   flags   -  indicating some internal features about nodes
|
\-------------------------------------------------------------------------*/
typedef unsigned int domNodeFlags;

#define HAS_LINE_COLUMN           1
#define VISIBLE_IN_TCL            2
#define IS_DELETED                4
#define HAS_BASEURI               8
#define DISABLE_OUTPUT_ESCAPING  16

typedef unsigned int domAttrFlags;

#define IS_ID_ATTRIBUTE           1
#define IS_NS_NODE                2

typedef unsigned int domDocFlags;

#define OUTPUT_DEFAULT_INDENT      1
#define NEEDS_RENUMBERING          2
#define DONT_FREE                  4
#define IGNORE_XMLNS               8
#define DOCUMENT_CMD              16
#define VAR_TRACE                 32
#define INSIDE_FROM_SCRIPT        64
#define DELETE_AFTER_FROM_SCRIPT 128

/*--------------------------------------------------------------------------
|   an index to the namespace records
|
\-------------------------------------------------------------------------*/
typedef unsigned int domNameSpaceIndex;



/*--------------------------------------------------------------------------
|   domException
|
\-------------------------------------------------------------------------*/
typedef enum {

    OK                          = 0,
    INDEX_SIZE_ERR              = 1,
    DOMSTRING_SIZE_ERR          = 2,
    HIERARCHY_REQUEST_ERR       = 3,
    WRONG_DOCUMENT_ERR          = 4,
    INVALID_CHARACTER_ERR       = 5,
    NO_DATA_ALLOWED_ERR         = 6,
    NO_MODIFICATION_ALLOWED_ERR = 7,
    NOT_FOUND_ERR               = 8,
    NOT_SUPPORTED_ERR           = 9,
    INUSE_ATTRIBUTE_ERR         = 10

} domException;

/*--------------------------------------------------------------------------
|   domDocInfo
|
\-------------------------------------------------------------------------*/
typedef struct domDocInfo {
    
    /* 'name' is always the name of the documentElement, no struct element
       needed for this */
    domString      publicId;
    domString      systemId;
    domString      internalSubset;
    /* Currently missing, according to DOM 2: 'entities' and 'notations'. */
    /* The following struct elements describes additional 'requested'
       facets of the document, following the xslt rec, section 16 */
    float          version;
    char          *encoding;
    int            omitXMLDeclaration;
    int            standalone;
    Tcl_HashTable *cdataSectionElements;
    domString      method;
    domString      mediaType;
    
} domDocInfo;

/*--------------------------------------------------------------------------
|   domDocument
|
\-------------------------------------------------------------------------*/
typedef struct domDocument {

    domNodeType       nodeType  : 8;
    domDocFlags       nodeFlags : 8;
    domNameSpaceIndex dummy     : 16;
    uintptr_t         documentNumber;
    struct domNode   *documentElement;
    struct domNode   *fragments;
#ifdef TCL_THREADS
    struct domNode   *deletedNodes;
#endif
    struct domNS    **namespaces;
    int               nsptr;
    int               nslen;
    char            **prefixNSMappings; /* Stores doc global prefix ns
                                           mappings for resolving of
                                           prefixes in seletNodes expr */
#ifdef TCL_THREADS
    unsigned int      nodeCounter;
#endif
    struct domNode   *rootNode;
    Tcl_HashTable    *ids;
    Tcl_HashTable    *unparsedEntities;
    Tcl_HashTable    *baseURIs;
    Tcl_HashTable    *xpathCache;
    char             *extResolver;
    domDocInfo       *doctype;
    TDomThreaded (
        Tcl_HashTable tdom_tagNames;   /* Names of tags found in doc */
        Tcl_HashTable tdom_attrNames;  /* Names of tag attributes */
        unsigned int  refCount;        /* # of object commands attached */
        struct _domlock *lock;          /* Lock for this document */
    )
} domDocument;

/*--------------------------------------------------------------------------
|  domLock
|
\-------------------------------------------------------------------------*/

#ifdef TCL_THREADS
typedef struct _domlock {
    domDocument* doc;           /* The DOM document to be locked */
    int numrd;	                /* # of readers waiting for lock */
    int numwr;                  /* # of writers waiting for lock */
    int lrcnt;                  /* Lock ref count, > 0: # of shared
                                 * readers, -1: exclusive writer */
    Tcl_Mutex mutex;            /* Mutex for serializing access */
    Tcl_Condition rcond;        /* Condition var for reader locks */
    Tcl_Condition wcond;        /* Condition var for writer locks */
    struct _domlock *next;       /* Next doc lock in global list */
} domlock;

#define LOCK_READ  0
#define LOCK_WRITE 1

#endif


/*--------------------------------------------------------------------------
|   namespace
|
\-------------------------------------------------------------------------*/
typedef struct domNS {

   char   *uri;
   char   *prefix;
   int     index;

} domNS;


#ifndef MAX_PREFIX_LEN
# define MAX_PREFIX_LEN   80
#endif

/*---------------------------------------------------------------------------
|   type domActiveNS
|
\--------------------------------------------------------------------------*/
typedef struct _domActiveNS {

    int    depth;
    domNS *namespace;

} domActiveNS;


/*--------------------------------------------------------------------------
|   domLineColumn
|
\-------------------------------------------------------------------------*/
typedef struct domLineColumn {

    XML_Size  line;
    XML_Size  column;
    XML_Index  byteIndex;

} domLineColumn;

typedef struct {
    int  errorCode;
    XML_Size errorLine;
    XML_Size errorColumn;
    XML_Index byteIndex;
} domParseForestErrorData;


/*--------------------------------------------------------------------------
|   domNode
|
\-------------------------------------------------------------------------*/
typedef struct domNode {

    domNodeType         nodeType  : 8;
    domNodeFlags        nodeFlags : 8;
#ifdef TDOM_LESS_NS
    domNameSpaceIndex   namespace : 8;
    unsigned int        info      : 8;
#else
    unsigned int        dummy     : 8;
    unsigned int        info      : 8;    
#endif
    unsigned int        nodeNumber;
    domDocument        *ownerDocument;
    struct domNode     *parentNode;
    struct domNode     *previousSibling;
    struct domNode     *nextSibling;

    domString           nodeName;  /* now the element node specific fields */
#ifndef TDOM_LESS_NS
    domNameSpaceIndex   namespace;
#endif
    struct domNode     *firstChild;
    struct domNode     *lastChild;
    struct domAttrNode *firstAttr;

} domNode;

/*--------------------------------------------------------------------------
|   domDeleteInfo
|
\-------------------------------------------------------------------------*/

typedef struct domDeleteInfo {
    domDocument * document;
    domNode     * node;
    Tcl_Interp  * interp;
    char        * traceVarName;
} domDeleteInfo;


/*--------------------------------------------------------------------------
|   domTextNode
|
\-------------------------------------------------------------------------*/
typedef struct domTextNode {

    domNodeType         nodeType  : 8;
    domNodeFlags        nodeFlags : 8;
#ifdef TDOM_LESS_NS
    domNameSpaceIndex   namespace : 8;
    unsigned int        info      : 8;
#else
    unsigned int        dummy     : 8;
    unsigned int        info      : 8;    
#endif
    unsigned int        nodeNumber;
    domDocument        *ownerDocument;
    struct domNode     *parentNode;
    struct domNode     *previousSibling;
    struct domNode     *nextSibling;

    domString           nodeValue;   /* now the text node specific fields */
    domLength           valueLength;

} domTextNode;


/*--------------------------------------------------------------------------
|   domProcessingInstructionNode
|
\-------------------------------------------------------------------------*/
typedef struct domProcessingInstructionNode {

    domNodeType         nodeType  : 8;
    domNodeFlags        nodeFlags : 8;
#ifdef TDOM_LESS_NS
    domNameSpaceIndex   namespace : 8;
    unsigned int        info      : 8;
#else
    unsigned int        dummy     : 8;
    unsigned int        info      : 8;    
#endif
    unsigned int        nodeNumber;
    domDocument        *ownerDocument;
    struct domNode     *parentNode;
    struct domNode     *previousSibling;
    struct domNode     *nextSibling;

    domString           targetValue;   /* now the pi specific fields */
    domLength           targetLength;
#ifndef TDOM_LESS_NS
    domNameSpaceIndex   namespace;
#endif
    domString           dataValue;
    domLength           dataLength;

} domProcessingInstructionNode;


/*--------------------------------------------------------------------------
|   domAttrNode
|
\-------------------------------------------------------------------------*/
typedef struct domAttrNode {

    domNodeType         nodeType  : 8;
    domAttrFlags        nodeFlags : 8;
#ifdef TDOM_LESS_NS
    domNameSpaceIndex   namespace : 8;
    unsigned int        info      : 8;
#else
    unsigned int        dummy     : 8;
    unsigned int        info      : 8;    
    domNameSpaceIndex   namespace;
#endif
    domString           nodeName;
    domString           nodeValue;
    domLength           valueLength;
    struct domNode     *parentNode;
    struct domAttrNode *nextSibling;

} domAttrNode;

/*--------------------------------------------------------------------------
|   domAddCallback
|
\-------------------------------------------------------------------------*/
typedef int  (*domAddCallback)  (domNode * node, void * clientData);
typedef void (*domFreeCallback) (domNode * node, void * clientData);

#include <schema.h>

/*--------------------------------------------------------------------------
|   Function prototypes
|
\-------------------------------------------------------------------------*/
const char *   domException2String (domException exception);


void           domModuleInitialize (void);
domDocument *  domCreateDoc (const char *baseURI, int storeLineColumn);
domDocument *  domCreateDocument (const char *uri,
                                  char *documentElementTagName);
void           domSetDocumentElement (domDocument *doc);

domDocument *  domReadDocument   (XML_Parser parser,
                                  char *xml,
                                  domLength  length,
                                  int   ignoreWhiteSpaces,
                                  int   keepCDATA,
                                  int   storeLineColumn,
                                  int   ignoreXMLNS,
                                  int   feedbackAfter,
                                  Tcl_Obj *feedbackCmd,
                                  Tcl_Channel channel,
                                  const char *baseurl,
                                  Tcl_Obj *extResolver,
                                  int   useForeignDTD,
                                  int   forest,
                                  int   paramEntityParsing,
#ifndef TDOM_NO_SCHEMA
                                  SchemaData *sdata,
#endif
                                  Tcl_Interp *interp,
                                  domParseForestErrorData *forestError,
                                  int  *status);

void           domFreeDocument   (domDocument *doc, 
                                  domFreeCallback freeCB, 
                                  void * clientData);

void           domFreeNode       (domNode *node, 
                                  domFreeCallback freeCB, 
                                  void *clientData, 
                                  int dontfree);

domTextNode *  domNewTextNode    (domDocument *doc,
                                  const char  *value,
                                  domLength    length,
                                  domNodeType  nodeType);

domNode *      domNewElementNode (domDocument *doc,
                                  const char  *tagName);
		
domNode *      domNewElementNodeNS (domDocument *doc,
                                    const char  *tagName,
                                    const char  *uri);

domProcessingInstructionNode * domNewProcessingInstructionNode (
                                  domDocument *doc,
                                  const char  *targetValue,
                                  domLength    targetLength,
                                  const char  *dataValue,
                                  domLength    dataLength);

domAttrNode *  domSetAttribute (domNode *node, const char *attributeName,
                                               const char *attributeValue);

domAttrNode *  domSetAttributeNS (domNode *node, const char *attributeName,
                                                 const char *attributeValue,
                                                 const char *uri,
                                                 int   createNSIfNeeded);
domAttrNode *  domGetAttributeNodeNS (domNode *node, const char *uri, 
                                                     const char *localname);

int            domRemoveAttribute (domNode *node, const char *attributeName);
int            domRemoveAttributeNS (domNode *node, const char *uri,
                                     const char *localName);
domNode *      domPreviousSibling (domNode *attr);
domException   domDeleteNode   (domNode *node, domFreeCallback freeCB, void *clientData);
domException   domRemoveChild  (domNode *node, domNode *childToRemove);
domException   domAppendChild  (domNode *node, domNode *childToAppend);
domException   domInsertBefore (domNode *node, domNode *childToInsert, domNode *refChild);
domException   domReplaceChild (domNode *node, domNode *newChild, domNode *oldChild);
domException   domSetNodeValue (domNode *node, const char *nodeValue,
                                domLength valueLen);
domNode *      domCloneNode (domNode *node, int deep);

domTextNode *  domAppendNewTextNode (domNode *parent, char *value, domLength length, domNodeType nodeType, int disableOutputEscaping);
domNode *      domAppendNewElementNode (domNode *parent, const char *tagName,
                                        const char *uri);
domNode *      domAppendLiteralNode (domNode *parent, domNode *node);
domNS *        domAddNSToNode (domNode *node, domNS *nsToAdd);
const char *   domNamespacePrefix (domNode *node);
const char *   domNamespaceURI    (domNode *node);
const char *   domGetLocalName    (const char *nodeName);
int            domSplitQName (const char *name, char *prefix,
                              const char **localName);
domNS *        domLookupNamespace (domDocument *doc, const char *prefix, 
                                   const char *namespaceURI);
domNS *        domLookupPrefix  (domNode *node, const char *prefix);
int            domIsNamespaceInScope (domActiveNS *NSstack, int NSstackPos,
                                      const char *prefix, const char *namespaceURI);
const char *   domLookupPrefixWithMappings (domNode *node, const char *prefix,
                                            char **prefixMappings);
domNS *        domLookupURI     (domNode *node, char *uri);
domNS *        domGetNamespaceByIndex (domDocument *doc, unsigned int nsIndex);
domNS *        domNewNamespace (domDocument *doc, const char *prefix,
                                const char *namespaceURI);
int            domGetLineColumn (domNode *node, XML_Size *line,
                                 XML_Size *column, XML_Index *byteIndex);

int            domXPointerChild (domNode * node, int all, int instance, domNodeType type,
                                 char *element, char *attrName, char *attrValue,
                                 domLength attrLen, domAddCallback addCallback,
                                 void * clientData);

int            domXPointerDescendant (domNode * node, int all, int instance,
                                      int * i, domNodeType type, char *element,
                                      char *attrName, char *attrValue, domLength attrLen,
                                      domAddCallback addCallback, void * clientData);

int            domXPointerAncestor (domNode * node, int all, int instance,
                                    int * i, domNodeType type, char *element,
                                    char *attrName, char *attrValue, domLength attrLen,
                                    domAddCallback addCallback, void * clientData);

int            domXPointerXSibling (domNode * node, int forward_mode, int all, int instance,
                                    domNodeType type, char *element, char *attrName,
                                    char *attrValue, domLength attrLen,
                                    domAddCallback addCallback, void * clientData);

const char *   findBaseURI (domNode *node);

void           tcldom_tolower (const char *str, char *str_out, int  len);
int            domIsNAME (const char *name);
int            domIsPINAME (const char *name);
int            domIsQNAME (const char *name);
int            domIsNCNAME (const char *name);
int            domIsChar (const char *str);
void           domClearString (char *str, char *replacement, domLength repllen,
                               Tcl_DString *clearedstr, int *changed);
int            domIsBMPChar (const char *str);
int            domIsComment (const char *str);
int            domIsCDATA (const char *str);
int            domIsPIValue (const char *str);
int            domIsHTML5CustomName (const char *str);
void           domCopyTo (domNode *node, domNode *parent, int copyNS);
void           domCopyNS (domNode *from, domNode *to);
domAttrNode *  domCreateXMLNamespaceNode (domNode *parent);
void           domRenumberTree (domNode *node);
int            domPrecedes (domNode *node, domNode *other);
void           domNormalize (domNode *node, int forXPath, 
                             domFreeCallback freeCB, void *clientData);
domException   domAppendData (domTextNode *node, char *value, domLength length, 
                              int disableOutputEscaping);

#ifdef TCL_THREADS
void           domLocksLock(domlock *dl, int how);
void           domLocksUnlock(domlock *dl);
void           domLocksAttach(domDocument *doc);
void           domLocksDetach(domDocument *doc);
void           domLocksFinalize(ClientData dummy);
#endif

/*---------------------------------------------------------------------------
|   coercion routines for calling from C++
|
\--------------------------------------------------------------------------*/
domAttrNode                  * coerceToAttrNode( domNode *n );
domTextNode                  * coerceToTextNode( domNode *n );
domProcessingInstructionNode * coerceToProcessingInstructionNode( domNode *n );


#endif

