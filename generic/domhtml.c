/*----------------------------------------------------------------------------
|   Copyright (c) 2000  Jochen Loewer (loewerj@hotmail.com)
|-----------------------------------------------------------------------------
|
|
| !! EXPERIMENTAL / pre alpha !!
|   A simple (hopefully fast) HTML parser to build up a DOM structure
|   in memory.
|   Based on xmlsimple.c.
| !! EXPERIMENTAL / pre alpha !!
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
|   October 2000
|
|   ------------------------------------------------------------------------
|
|   Partly based on a parser for XML (for TMML by R.Hipp 1998). 
|   This source code is released into the public domain by the author.
|   on 2002, December 17.
|
\---------------------------------------------------------------------------*/



/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <tcl.h>
#include <ctype.h>
#include <string.h>
#include <dom.h>



/*----------------------------------------------------------------------------
|   Defines
|
\---------------------------------------------------------------------------*/
#define DBG(x)          
#define RetError(m,p)   *errStr = tdomstrdup(m); *pos = p; return TCL_ERROR;
#define IsLetter(c)     ( ((c)>='A' && (c)<='Z') || ((c)>='a' && (c)<='z') || ((c) >= '0' && (c) <= '9') )
#define TU(c)           toupper((unsigned char)c)



/*----------------------------------------------------------------------------
|   Begin Character Entity Translator
|
|
|   The next section of code implements routines used to translate
|   character entity references into their corresponding strings.
|
|   Examples:
|
|         &amp;          "&"
|         &lt;           "<"
|         &gt;           ">"
|         &nbsp;         " "
|
\---------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
|   Each entity reference is recorded as an instance of the following
|   tructure
\---------------------------------------------------------------------------*/
typedef struct Er Er;
struct Er {
    char *zName;     /* The name of this entity reference.  ex:  "amp" */
    char *zValue;    /* The value for this entity.          ex:  "&"   */
    Er *pNext;       /* Next entity with the same hash on zName        */
};


/*----------------------------------------------------------------------------
|   The size of the hash table.  For best results this should
|   be a prime number which is about the same size as the number of
|   character entity references known to the system.
|
\---------------------------------------------------------------------------*/
#define ER_HASH_SIZE 2129

/*----------------------------------------------------------------------------
|   The following flag is TRUE if entity reference hash table needs
|   to be initialized.
|
\---------------------------------------------------------------------------*/
static int bErNeedsInit = 1;
TDomThreaded(static Tcl_Mutex initMutex;)


/*----------------------------------------------------------------------------
|   The hash table
|
|   If the name of an entity reference hashes to the value H, then
|   apErHash[H] will point to a linked list of Er structures, one of
|   which will be the Er structure for that entity reference
|
\---------------------------------------------------------------------------*/
static Er *apErHash[ER_HASH_SIZE];



/*----------------------------------------------------------------------------
|   ErHash  --
|
|       Hash an entity reference name.  The value returned is an
|       integer between 0 and Er_HASH_SIZE-1, inclusive.
|
\---------------------------------------------------------------------------*/
static int ErHash(
    const char *zName
)
{
    int h = 0;      /* The hash value to be returned */
    char c;         /* The next character in the name being hashed */

    while( (c=*zName)!=0 ){
        h = h<<5 ^ h ^ c;
        zName++;
    }
    if( h<0 ) h = -h;
    return h % ER_HASH_SIZE;

} /* ErHash */


/*----------------------------------------------------------------------------
|   The following is a table of all entity references.  To create
|   new character entities, add entries to this table.
|
|   Note: For the decoder to work, the name of the entity reference
|   (including the leading & and closing ;) must not be shorter than
|   the value.
|
\---------------------------------------------------------------------------*/
static Er er_sequences[] = {
    { "AElig",                           "\xC3\x86"                  , 0 }, /* Æ */
    { "AMP",                             "\x26"                      , 0 }, /* & */
    { "Aacute",                          "\xC3\x81"                  , 0 }, /* Á */
    { "Abreve",                          "\xC4\x82"                  , 0 }, /* Ă */
    { "Acirc",                           "\xC3\x82"                  , 0 }, /* Â */
    { "Acy",                             "\xD0\x90"                  , 0 }, /* А */
    { "Afr",                             "\xF0\x9D\x94\x84"          , 0 }, /* 𝔄 */
    { "Agrave",                          "\xC3\x80"                  , 0 }, /* À */
    { "Alpha",                           "\xCE\x91"                  , 0 }, /* Α */
    { "Amacr",                           "\xC4\x80"                  , 0 }, /* Ā */
    { "And",                             "\xE2\xA9\x93"              , 0 }, /* ⩓ */
    { "Aogon",                           "\xC4\x84"                  , 0 }, /* Ą */
    { "Aopf",                            "\xF0\x9D\x94\xB8"          , 0 }, /* 𝔸 */
    { "ApplyFunction",                   "\xE2\x81\xA1"              , 0 }, /* ⁡ */
    { "Aring",                           "\xC3\x85"                  , 0 }, /* Å */
    { "Ascr",                            "\xF0\x9D\x92\x9C"          , 0 }, /* 𝒜 */
    { "Assign",                          "\xE2\x89\x94"              , 0 }, /* ≔ */
    { "Atilde",                          "\xC3\x83"                  , 0 }, /* Ã */
    { "Auml",                            "\xC3\x84"                  , 0 }, /* Ä */
    { "Backslash",                       "\xE2\x88\x96"              , 0 }, /* ∖ */
    { "Barv",                            "\xE2\xAB\xA7"              , 0 }, /* ⫧ */
    { "Barwed",                          "\xE2\x8C\x86"              , 0 }, /* ⌆ */
    { "Bcy",                             "\xD0\x91"                  , 0 }, /* Б */
    { "Because",                         "\xE2\x88\xB5"              , 0 }, /* ∵ */
    { "Bernoullis",                      "\xE2\x84\xAC"              , 0 }, /* ℬ */
    { "Beta",                            "\xCE\x92"                  , 0 }, /* Β */
    { "Bfr",                             "\xF0\x9D\x94\x85"          , 0 }, /* 𝔅 */
    { "Bopf",                            "\xF0\x9D\x94\xB9"          , 0 }, /* 𝔹 */
    { "Breve",                           "\xCB\x98"                  , 0 }, /* ˘ */
    { "Bscr",                            "\xE2\x84\xAC"              , 0 }, /* ℬ */
    { "Bumpeq",                          "\xE2\x89\x8E"              , 0 }, /* ≎ */
    { "CHcy",                            "\xD0\xA7"                  , 0 }, /* Ч */
    { "COPY",                            "\xC2\xA9"                  , 0 }, /* © */
    { "Cacute",                          "\xC4\x86"                  , 0 }, /* Ć */
    { "Cap",                             "\xE2\x8B\x92"              , 0 }, /* ⋒ */
    { "CapitalDifferentialD",            "\xE2\x85\x85"              , 0 }, /* ⅅ */
    { "Cayleys",                         "\xE2\x84\xAD"              , 0 }, /* ℭ */
    { "Ccaron",                          "\xC4\x8C"                  , 0 }, /* Č */
    { "Ccedil",                          "\xC3\x87"                  , 0 }, /* Ç */
    { "Ccirc",                           "\xC4\x88"                  , 0 }, /* Ĉ */
    { "Cconint",                         "\xE2\x88\xB0"              , 0 }, /* ∰ */
    { "Cdot",                            "\xC4\x8A"                  , 0 }, /* Ċ */
    { "Cedilla",                         "\xC2\xB8"                  , 0 }, /* ¸ */
    { "CenterDot",                       "\xC2\xB7"                  , 0 }, /* · */
    { "Cfr",                             "\xE2\x84\xAD"              , 0 }, /* ℭ */
    { "Chi",                             "\xCE\xA7"                  , 0 }, /* Χ */
    { "CircleDot",                       "\xE2\x8A\x99"              , 0 }, /* ⊙ */
    { "CircleMinus",                     "\xE2\x8A\x96"              , 0 }, /* ⊖ */
    { "CirclePlus",                      "\xE2\x8A\x95"              , 0 }, /* ⊕ */
    { "CircleTimes",                     "\xE2\x8A\x97"              , 0 }, /* ⊗ */
    { "ClockwiseContourIntegral",        "\xE2\x88\xB2"              , 0 }, /* ∲ */
    { "CloseCurlyDoubleQuote",           "\xE2\x80\x9D"              , 0 }, /* ” */
    { "CloseCurlyQuote",                 "\xE2\x80\x99"              , 0 }, /* ’ */
    { "Colon",                           "\xE2\x88\xB7"              , 0 }, /* ∷ */
    { "Colone",                          "\xE2\xA9\xB4"              , 0 }, /* ⩴ */
    { "Congruent",                       "\xE2\x89\xA1"              , 0 }, /* ≡ */
    { "Conint",                          "\xE2\x88\xAF"              , 0 }, /* ∯ */
    { "ContourIntegral",                 "\xE2\x88\xAE"              , 0 }, /* ∮ */
    { "Copf",                            "\xE2\x84\x82"              , 0 }, /* ℂ */
    { "Coproduct",                       "\xE2\x88\x90"              , 0 }, /* ∐ */
    { "CounterClockwiseContourIntegral", "\xE2\x88\xB3"              , 0 }, /* ∳ */
    { "Cross",                           "\xE2\xA8\xAF"              , 0 }, /* ⨯ */
    { "Cscr",                            "\xF0\x9D\x92\x9E"          , 0 }, /* 𝒞 */
    { "Cup",                             "\xE2\x8B\x93"              , 0 }, /* ⋓ */
    { "CupCap",                          "\xE2\x89\x8D"              , 0 }, /* ≍ */
    { "DD",                              "\xE2\x85\x85"              , 0 }, /* ⅅ */
    { "DDotrahd",                        "\xE2\xA4\x91"              , 0 }, /* ⤑ */
    { "DJcy",                            "\xD0\x82"                  , 0 }, /* Ђ */
    { "DScy",                            "\xD0\x85"                  , 0 }, /* Ѕ */
    { "DZcy",                            "\xD0\x8F"                  , 0 }, /* Џ */
    { "Dagger",                          "\xE2\x80\xA1"              , 0 }, /* ‡ */
    { "Darr",                            "\xE2\x86\xA1"              , 0 }, /* ↡ */
    { "Dashv",                           "\xE2\xAB\xA4"              , 0 }, /* ⫤ */
    { "Dcaron",                          "\xC4\x8E"                  , 0 }, /* Ď */
    { "Dcy",                             "\xD0\x94"                  , 0 }, /* Д */
    { "Del",                             "\xE2\x88\x87"              , 0 }, /* ∇ */
    { "Delta",                           "\xCE\x94"                  , 0 }, /* Δ */
    { "Dfr",                             "\xF0\x9D\x94\x87"          , 0 }, /* 𝔇 */
    { "DiacriticalAcute",                "\xC2\xB4"                  , 0 }, /* ´ */
    { "DiacriticalDot",                  "\xCB\x99"                  , 0 }, /* ˙ */
    { "DiacriticalDoubleAcute",          "\xCB\x9D"                  , 0 }, /* ˝ */
    { "DiacriticalGrave",                "\x60"                      , 0 }, /* ` */
    { "DiacriticalTilde",                "\xCB\x9C"                  , 0 }, /* ˜ */
    { "Diamond",                         "\xE2\x8B\x84"              , 0 }, /* ⋄ */
    { "DifferentialD",                   "\xE2\x85\x86"              , 0 }, /* ⅆ */
    { "Dopf",                            "\xF0\x9D\x94\xBB"          , 0 }, /* 𝔻 */
    { "Dot",                             "\xC2\xA8"                  , 0 }, /* ¨ */
    { "DotDot",                          "\x20\xE2\x83\x9C"          , 0 }, /*  ⃜ */
    { "DotEqual",                        "\xE2\x89\x90"              , 0 }, /* ≐ */
    { "DoubleContourIntegral",           "\xE2\x88\xAF"              , 0 }, /* ∯ */
    { "DoubleDot",                       "\xC2\xA8"                  , 0 }, /* ¨ */
    { "DoubleDownArrow",                 "\xE2\x87\x93"              , 0 }, /* ⇓ */
    { "DoubleLeftArrow",                 "\xE2\x87\x90"              , 0 }, /* ⇐ */
    { "DoubleLeftRightArrow",            "\xE2\x87\x94"              , 0 }, /* ⇔ */
    { "DoubleLeftTee",                   "\xE2\xAB\xA4"              , 0 }, /* ⫤ */
    { "DoubleLongLeftArrow",             "\xE2\x9F\xB8"              , 0 }, /* ⟸ */
    { "DoubleLongLeftRightArrow",        "\xE2\x9F\xBA"              , 0 }, /* ⟺ */
    { "DoubleLongRightArrow",            "\xE2\x9F\xB9"              , 0 }, /* ⟹ */
    { "DoubleRightArrow",                "\xE2\x87\x92"              , 0 }, /* ⇒ */
    { "DoubleRightTee",                  "\xE2\x8A\xA8"              , 0 }, /* ⊨ */
    { "DoubleUpArrow",                   "\xE2\x87\x91"              , 0 }, /* ⇑ */
    { "DoubleUpDownArrow",               "\xE2\x87\x95"              , 0 }, /* ⇕ */
    { "DoubleVerticalBar",               "\xE2\x88\xA5"              , 0 }, /* ∥ */
    { "DownArrow",                       "\xE2\x86\x93"              , 0 }, /* ↓ */
    { "DownArrowBar",                    "\xE2\xA4\x93"              , 0 }, /* ⤓ */
    { "DownArrowUpArrow",                "\xE2\x87\xB5"              , 0 }, /* ⇵ */
    { "DownBreve",                       "\x20\xCC\x91"              , 0 }, /*  ̑ */
    { "DownLeftRightVector",             "\xE2\xA5\x90"              , 0 }, /* ⥐ */
    { "DownLeftTeeVector",               "\xE2\xA5\x9E"              , 0 }, /* ⥞ */
    { "DownLeftVector",                  "\xE2\x86\xBD"              , 0 }, /* ↽ */
    { "DownLeftVectorBar",               "\xE2\xA5\x96"              , 0 }, /* ⥖ */
    { "DownRightTeeVector",              "\xE2\xA5\x9F"              , 0 }, /* ⥟ */
    { "DownRightVector",                 "\xE2\x87\x81"              , 0 }, /* ⇁ */
    { "DownRightVectorBar",              "\xE2\xA5\x97"              , 0 }, /* ⥗ */
    { "DownTee",                         "\xE2\x8A\xA4"              , 0 }, /* ⊤ */
    { "DownTeeArrow",                    "\xE2\x86\xA7"              , 0 }, /* ↧ */
    { "Downarrow",                       "\xE2\x87\x93"              , 0 }, /* ⇓ */
    { "Dscr",                            "\xF0\x9D\x92\x9F"          , 0 }, /* 𝒟 */
    { "Dstrok",                          "\xC4\x90"                  , 0 }, /* Đ */
    { "ENG",                             "\xC5\x8A"                  , 0 }, /* Ŋ */
    { "ETH",                             "\xC3\x90"                  , 0 }, /* Ð */
    { "Eacute",                          "\xC3\x89"                  , 0 }, /* É */
    { "Ecaron",                          "\xC4\x9A"                  , 0 }, /* Ě */
    { "Ecirc",                           "\xC3\x8A"                  , 0 }, /* Ê */
    { "Ecy",                             "\xD0\xAD"                  , 0 }, /* Э */
    { "Edot",                            "\xC4\x96"                  , 0 }, /* Ė */
    { "Efr",                             "\xF0\x9D\x94\x88"          , 0 }, /* 𝔈 */
    { "Egrave",                          "\xC3\x88"                  , 0 }, /* È */
    { "Element",                         "\xE2\x88\x88"              , 0 }, /* ∈ */
    { "Emacr",                           "\xC4\x92"                  , 0 }, /* Ē */
    { "EmptySmallSquare",                "\xE2\x97\xBB"              , 0 }, /* ◻ */
    { "EmptyVerySmallSquare",            "\xE2\x96\xAB"              , 0 }, /* ▫ */
    { "Eogon",                           "\xC4\x98"                  , 0 }, /* Ę */
    { "Eopf",                            "\xF0\x9D\x94\xBC"          , 0 }, /* 𝔼 */
    { "Epsilon",                         "\xCE\x95"                  , 0 }, /* Ε */
    { "Equal",                           "\xE2\xA9\xB5"              , 0 }, /* ⩵ */
    { "EqualTilde",                      "\xE2\x89\x82"              , 0 }, /* ≂ */
    { "Equilibrium",                     "\xE2\x87\x8C"              , 0 }, /* ⇌ */
    { "Escr",                            "\xE2\x84\xB0"              , 0 }, /* ℰ */
    { "Esim",                            "\xE2\xA9\xB3"              , 0 }, /* ⩳ */
    { "Eta",                             "\xCE\x97"                  , 0 }, /* Η */
    { "Euml",                            "\xC3\x8B"                  , 0 }, /* Ë */
    { "Exists",                          "\xE2\x88\x83"              , 0 }, /* ∃ */
    { "ExponentialE",                    "\xE2\x85\x87"              , 0 }, /* ⅇ */
    { "Fcy",                             "\xD0\xA4"                  , 0 }, /* Ф */
    { "Ffr",                             "\xF0\x9D\x94\x89"          , 0 }, /* 𝔉 */
    { "FilledSmallSquare",               "\xE2\x97\xBC"              , 0 }, /* ◼ */
    { "FilledVerySmallSquare",           "\xE2\x96\xAA"              , 0 }, /* ▪ */
    { "Fopf",                            "\xF0\x9D\x94\xBD"          , 0 }, /* 𝔽 */
    { "ForAll",                          "\xE2\x88\x80"              , 0 }, /* ∀ */
    { "Fouriertrf",                      "\xE2\x84\xB1"              , 0 }, /* ℱ */
    { "Fscr",                            "\xE2\x84\xB1"              , 0 }, /* ℱ */
    { "GJcy",                            "\xD0\x83"                  , 0 }, /* Ѓ */
    { "GT",                              "\x3E"                      , 0 }, /* > */
    { "Gamma",                           "\xCE\x93"                  , 0 }, /* Γ */
    { "Gammad",                          "\xCF\x9C"                  , 0 }, /* Ϝ */
    { "Gbreve",                          "\xC4\x9E"                  , 0 }, /* Ğ */
    { "Gcedil",                          "\xC4\xA2"                  , 0 }, /* Ģ */
    { "Gcirc",                           "\xC4\x9C"                  , 0 }, /* Ĝ */
    { "Gcy",                             "\xD0\x93"                  , 0 }, /* Г */
    { "Gdot",                            "\xC4\xA0"                  , 0 }, /* Ġ */
    { "Gfr",                             "\xF0\x9D\x94\x8A"          , 0 }, /* 𝔊 */
    { "Gg",                              "\xE2\x8B\x99"              , 0 }, /* ⋙ */
    { "Gopf",                            "\xF0\x9D\x94\xBE"          , 0 }, /* 𝔾 */
    { "GreaterEqual",                    "\xE2\x89\xA5"              , 0 }, /* ≥ */
    { "GreaterEqualLess",                "\xE2\x8B\x9B"              , 0 }, /* ⋛ */
    { "GreaterFullEqual",                "\xE2\x89\xA7"              , 0 }, /* ≧ */
    { "GreaterGreater",                  "\xE2\xAA\xA2"              , 0 }, /* ⪢ */
    { "GreaterLess",                     "\xE2\x89\xB7"              , 0 }, /* ≷ */
    { "GreaterSlantEqual",               "\xE2\xA9\xBE"              , 0 }, /* ⩾ */
    { "GreaterTilde",                    "\xE2\x89\xB3"              , 0 }, /* ≳ */
    { "Gscr",                            "\xF0\x9D\x92\xA2"          , 0 }, /* 𝒢 */
    { "Gt",                              "\xE2\x89\xAB"              , 0 }, /* ≫ */
    { "HARDcy",                          "\xD0\xAA"                  , 0 }, /* Ъ */
    { "Hacek",                           "\xCB\x87"                  , 0 }, /* ˇ */
    { "Hat",                             "\x5E"                      , 0 }, /* ^ */
    { "Hcirc",                           "\xC4\xA4"                  , 0 }, /* Ĥ */
    { "Hfr",                             "\xE2\x84\x8C"              , 0 }, /* ℌ */
    { "HilbertSpace",                    "\xE2\x84\x8B"              , 0 }, /* ℋ */
    { "Hopf",                            "\xE2\x84\x8D"              , 0 }, /* ℍ */
    { "HorizontalLine",                  "\xE2\x94\x80"              , 0 }, /* ─ */
    { "Hscr",                            "\xE2\x84\x8B"              , 0 }, /* ℋ */
    { "Hstrok",                          "\xC4\xA6"                  , 0 }, /* Ħ */
    { "HumpDownHump",                    "\xE2\x89\x8E"              , 0 }, /* ≎ */
    { "HumpEqual",                       "\xE2\x89\x8F"              , 0 }, /* ≏ */
    { "IEcy",                            "\xD0\x95"                  , 0 }, /* Е */
    { "IJlig",                           "\xC4\xB2"                  , 0 }, /* Ĳ */
    { "IOcy",                            "\xD0\x81"                  , 0 }, /* Ё */
    { "Iacute",                          "\xC3\x8D"                  , 0 }, /* Í */
    { "Icirc",                           "\xC3\x8E"                  , 0 }, /* Î */
    { "Icy",                             "\xD0\x98"                  , 0 }, /* И */
    { "Idot",                            "\xC4\xB0"                  , 0 }, /* İ */
    { "Ifr",                             "\xE2\x84\x91"              , 0 }, /* ℑ */
    { "Igrave",                          "\xC3\x8C"                  , 0 }, /* Ì */
    { "Im",                              "\xE2\x84\x91"              , 0 }, /* ℑ */
    { "Imacr",                           "\xC4\xAA"                  , 0 }, /* Ī */
    { "ImaginaryI",                      "\xE2\x85\x88"              , 0 }, /* ⅈ */
    { "Implies",                         "\xE2\x87\x92"              , 0 }, /* ⇒ */
    { "Int",                             "\xE2\x88\xAC"              , 0 }, /* ∬ */
    { "Integral",                        "\xE2\x88\xAB"              , 0 }, /* ∫ */
    { "Intersection",                    "\xE2\x8B\x82"              , 0 }, /* ⋂ */
    { "InvisibleComma",                  "\xE2\x81\xA3"              , 0 }, /* ⁣ */
    { "InvisibleTimes",                  "\xE2\x81\xA2"              , 0 }, /* ⁢ */
    { "Iogon",                           "\xC4\xAE"                  , 0 }, /* Į */
    { "Iopf",                            "\xF0\x9D\x95\x80"          , 0 }, /* 𝕀 */
    { "Iota",                            "\xCE\x99"                  , 0 }, /* Ι */
    { "Iscr",                            "\xE2\x84\x90"              , 0 }, /* ℐ */
    { "Itilde",                          "\xC4\xA8"                  , 0 }, /* Ĩ */
    { "Iukcy",                           "\xD0\x86"                  , 0 }, /* І */
    { "Iuml",                            "\xC3\x8F"                  , 0 }, /* Ï */
    { "Jcirc",                           "\xC4\xB4"                  , 0 }, /* Ĵ */
    { "Jcy",                             "\xD0\x99"                  , 0 }, /* Й */
    { "Jfr",                             "\xF0\x9D\x94\x8D"          , 0 }, /* 𝔍 */
    { "Jopf",                            "\xF0\x9D\x95\x81"          , 0 }, /* 𝕁 */
    { "Jscr",                            "\xF0\x9D\x92\xA5"          , 0 }, /* 𝒥 */
    { "Jsercy",                          "\xD0\x88"                  , 0 }, /* Ј */
    { "Jukcy",                           "\xD0\x84"                  , 0 }, /* Є */
    { "KHcy",                            "\xD0\xA5"                  , 0 }, /* Х */
    { "KJcy",                            "\xD0\x8C"                  , 0 }, /* Ќ */
    { "Kappa",                           "\xCE\x9A"                  , 0 }, /* Κ */
    { "Kcedil",                          "\xC4\xB6"                  , 0 }, /* Ķ */
    { "Kcy",                             "\xD0\x9A"                  , 0 }, /* К */
    { "Kfr",                             "\xF0\x9D\x94\x8E"          , 0 }, /* 𝔎 */
    { "Kopf",                            "\xF0\x9D\x95\x82"          , 0 }, /* 𝕂 */
    { "Kscr",                            "\xF0\x9D\x92\xA6"          , 0 }, /* 𝒦 */
    { "LJcy",                            "\xD0\x89"                  , 0 }, /* Љ */
    { "LT",                              "\x3C"                      , 0 }, /* < */
    { "Lacute",                          "\xC4\xB9"                  , 0 }, /* Ĺ */
    { "Lambda",                          "\xCE\x9B"                  , 0 }, /* Λ */
    { "Lang",                            "\xE2\x9F\xAA"              , 0 }, /* ⟪ */
    { "Laplacetrf",                      "\xE2\x84\x92"              , 0 }, /* ℒ */
    { "Larr",                            "\xE2\x86\x9E"              , 0 }, /* ↞ */
    { "Lcaron",                          "\xC4\xBD"                  , 0 }, /* Ľ */
    { "Lcedil",                          "\xC4\xBB"                  , 0 }, /* Ļ */
    { "Lcy",                             "\xD0\x9B"                  , 0 }, /* Л */
    { "LeftAngleBracket",                "\xE2\x9F\xA8"              , 0 }, /* ⟨ */
    { "LeftArrow",                       "\xE2\x86\x90"              , 0 }, /* ← */
    { "LeftArrowBar",                    "\xE2\x87\xA4"              , 0 }, /* ⇤ */
    { "LeftArrowRightArrow",             "\xE2\x87\x86"              , 0 }, /* ⇆ */
    { "LeftCeiling",                     "\xE2\x8C\x88"              , 0 }, /* ⌈ */
    { "LeftDoubleBracket",               "\xE2\x9F\xA6"              , 0 }, /* ⟦ */
    { "LeftDownTeeVector",               "\xE2\xA5\xA1"              , 0 }, /* ⥡ */
    { "LeftDownVector",                  "\xE2\x87\x83"              , 0 }, /* ⇃ */
    { "LeftDownVectorBar",               "\xE2\xA5\x99"              , 0 }, /* ⥙ */
    { "LeftFloor",                       "\xE2\x8C\x8A"              , 0 }, /* ⌊ */
    { "LeftRightArrow",                  "\xE2\x86\x94"              , 0 }, /* ↔ */
    { "LeftRightVector",                 "\xE2\xA5\x8E"              , 0 }, /* ⥎ */
    { "LeftTee",                         "\xE2\x8A\xA3"              , 0 }, /* ⊣ */
    { "LeftTeeArrow",                    "\xE2\x86\xA4"              , 0 }, /* ↤ */
    { "LeftTeeVector",                   "\xE2\xA5\x9A"              , 0 }, /* ⥚ */
    { "LeftTriangle",                    "\xE2\x8A\xB2"              , 0 }, /* ⊲ */
    { "LeftTriangleBar",                 "\xE2\xA7\x8F"              , 0 }, /* ⧏ */
    { "LeftTriangleEqual",               "\xE2\x8A\xB4"              , 0 }, /* ⊴ */
    { "LeftUpDownVector",                "\xE2\xA5\x91"              , 0 }, /* ⥑ */
    { "LeftUpTeeVector",                 "\xE2\xA5\xA0"              , 0 }, /* ⥠ */
    { "LeftUpVector",                    "\xE2\x86\xBF"              , 0 }, /* ↿ */
    { "LeftUpVectorBar",                 "\xE2\xA5\x98"              , 0 }, /* ⥘ */
    { "LeftVector",                      "\xE2\x86\xBC"              , 0 }, /* ↼ */
    { "LeftVectorBar",                   "\xE2\xA5\x92"              , 0 }, /* ⥒ */
    { "Leftarrow",                       "\xE2\x87\x90"              , 0 }, /* ⇐ */
    { "Leftrightarrow",                  "\xE2\x87\x94"              , 0 }, /* ⇔ */
    { "LessEqualGreater",                "\xE2\x8B\x9A"              , 0 }, /* ⋚ */
    { "LessFullEqual",                   "\xE2\x89\xA6"              , 0 }, /* ≦ */
    { "LessGreater",                     "\xE2\x89\xB6"              , 0 }, /* ≶ */
    { "LessLess",                        "\xE2\xAA\xA1"              , 0 }, /* ⪡ */
    { "LessSlantEqual",                  "\xE2\xA9\xBD"              , 0 }, /* ⩽ */
    { "LessTilde",                       "\xE2\x89\xB2"              , 0 }, /* ≲ */
    { "Lfr",                             "\xF0\x9D\x94\x8F"          , 0 }, /* 𝔏 */
    { "Ll",                              "\xE2\x8B\x98"              , 0 }, /* ⋘ */
    { "Lleftarrow",                      "\xE2\x87\x9A"              , 0 }, /* ⇚ */
    { "Lmidot",                          "\xC4\xBF"                  , 0 }, /* Ŀ */
    { "LongLeftArrow",                   "\xE2\x9F\xB5"              , 0 }, /* ⟵ */
    { "LongLeftRightArrow",              "\xE2\x9F\xB7"              , 0 }, /* ⟷ */
    { "LongRightArrow",                  "\xE2\x9F\xB6"              , 0 }, /* ⟶ */
    { "Longleftarrow",                   "\xE2\x9F\xB8"              , 0 }, /* ⟸ */
    { "Longleftrightarrow",              "\xE2\x9F\xBA"              , 0 }, /* ⟺ */
    { "Longrightarrow",                  "\xE2\x9F\xB9"              , 0 }, /* ⟹ */
    { "Lopf",                            "\xF0\x9D\x95\x83"          , 0 }, /* 𝕃 */
    { "LowerLeftArrow",                  "\xE2\x86\x99"              , 0 }, /* ↙ */
    { "LowerRightArrow",                 "\xE2\x86\x98"              , 0 }, /* ↘ */
    { "Lscr",                            "\xE2\x84\x92"              , 0 }, /* ℒ */
    { "Lsh",                             "\xE2\x86\xB0"              , 0 }, /* ↰ */
    { "Lstrok",                          "\xC5\x81"                  , 0 }, /* Ł */
    { "Lt",                              "\xE2\x89\xAA"              , 0 }, /* ≪ */
    { "Map",                             "\xE2\xA4\x85"              , 0 }, /* ⤅ */
    { "Mcy",                             "\xD0\x9C"                  , 0 }, /* М */
    { "MediumSpace",                     "\xE2\x81\x9F"              , 0 }, /*   */
    { "Mellintrf",                       "\xE2\x84\xB3"              , 0 }, /* ℳ */
    { "Mfr",                             "\xF0\x9D\x94\x90"          , 0 }, /* 𝔐 */
    { "MinusPlus",                       "\xE2\x88\x93"              , 0 }, /* ∓ */
    { "Mopf",                            "\xF0\x9D\x95\x84"          , 0 }, /* 𝕄 */
    { "Mscr",                            "\xE2\x84\xB3"              , 0 }, /* ℳ */
    { "Mu",                              "\xCE\x9C"                  , 0 }, /* Μ */
    { "NJcy",                            "\xD0\x8A"                  , 0 }, /* Њ */
    { "Nacute",                          "\xC5\x83"                  , 0 }, /* Ń */
    { "Ncaron",                          "\xC5\x87"                  , 0 }, /* Ň */
    { "Ncedil",                          "\xC5\x85"                  , 0 }, /* Ņ */
    { "Ncy",                             "\xD0\x9D"                  , 0 }, /* Н */
    { "NegativeMediumSpace",             "\xE2\x80\x8B"              , 0 }, /* ​ */
    { "NegativeThickSpace",              "\xE2\x80\x8B"              , 0 }, /* ​ */
    { "NegativeThinSpace",               "\xE2\x80\x8B"              , 0 }, /* ​ */
    { "NegativeVeryThinSpace",           "\xE2\x80\x8B"              , 0 }, /* ​ */
    { "NestedGreaterGreater",            "\xE2\x89\xAB"              , 0 }, /* ≫ */
    { "NestedLessLess",                  "\xE2\x89\xAA"              , 0 }, /* ≪ */
    { "NewLine",                         "\x0A"                      , 0 }, /* 
                                                                             */
    { "Nfr",                             "\xF0\x9D\x94\x91"          , 0 }, /* 𝔑 */
    { "NoBreak",                         "\xE2\x81\xA0"              , 0 }, /* ⁠ */
    { "NonBreakingSpace",                "\xC2\xA0"                  , 0 }, /*   */
    { "Nopf",                            "\xE2\x84\x95"              , 0 }, /* ℕ */
    { "Not",                             "\xE2\xAB\xAC"              , 0 }, /* ⫬ */
    { "NotCongruent",                    "\xE2\x89\xA2"              , 0 }, /* ≢ */
    { "NotCupCap",                       "\xE2\x89\xAD"              , 0 }, /* ≭ */
    { "NotDoubleVerticalBar",            "\xE2\x88\xA6"              , 0 }, /* ∦ */
    { "NotElement",                      "\xE2\x88\x89"              , 0 }, /* ∉ */
    { "NotEqual",                        "\xE2\x89\xA0"              , 0 }, /* ≠ */
    { "NotEqualTilde",                   "\xE2\x89\x82\xCC\xB8"      , 0 }, /* ≂̸ */
    { "NotExists",                       "\xE2\x88\x84"              , 0 }, /* ∄ */
    { "NotGreater",                      "\xE2\x89\xAF"              , 0 }, /* ≯ */
    { "NotGreaterEqual",                 "\xE2\x89\xB1"              , 0 }, /* ≱ */
    { "NotGreaterFullEqual",             "\xE2\x89\xA7\xCC\xB8"      , 0 }, /* ≧̸ */
    { "NotGreaterGreater",               "\xE2\x89\xAB\xCC\xB8"      , 0 }, /* ≫̸ */
    { "NotGreaterLess",                  "\xE2\x89\xB9"              , 0 }, /* ≹ */
    { "NotGreaterSlantEqual",            "\xE2\xA9\xBE\xCC\xB8"      , 0 }, /* ⩾̸ */
    { "NotGreaterTilde",                 "\xE2\x89\xB5"              , 0 }, /* ≵ */
    { "NotHumpDownHump",                 "\xE2\x89\x8E\xCC\xB8"      , 0 }, /* ≎̸ */
    { "NotHumpEqual",                    "\xE2\x89\x8F\xCC\xB8"      , 0 }, /* ≏̸ */
    { "NotLeftTriangle",                 "\xE2\x8B\xAA"              , 0 }, /* ⋪ */
    { "NotLeftTriangleBar",              "\xE2\xA7\x8F\xCC\xB8"      , 0 }, /* ⧏̸ */
    { "NotLeftTriangleEqual",            "\xE2\x8B\xAC"              , 0 }, /* ⋬ */
    { "NotLess",                         "\xE2\x89\xAE"              , 0 }, /* ≮ */
    { "NotLessEqual",                    "\xE2\x89\xB0"              , 0 }, /* ≰ */
    { "NotLessGreater",                  "\xE2\x89\xB8"              , 0 }, /* ≸ */
    { "NotLessLess",                     "\xE2\x89\xAA\xCC\xB8"      , 0 }, /* ≪̸ */
    { "NotLessSlantEqual",               "\xE2\xA9\xBD\xCC\xB8"      , 0 }, /* ⩽̸ */
    { "NotLessTilde",                    "\xE2\x89\xB4"              , 0 }, /* ≴ */
    { "NotNestedGreaterGreater",         "\xE2\xAA\xA2\xCC\xB8"      , 0 }, /* ⪢̸ */
    { "NotNestedLessLess",               "\xE2\xAA\xA1\xCC\xB8"      , 0 }, /* ⪡̸ */
    { "NotPrecedes",                     "\xE2\x8A\x80"              , 0 }, /* ⊀ */
    { "NotPrecedesEqual",                "\xE2\xAA\xAF\xCC\xB8"      , 0 }, /* ⪯̸ */
    { "NotPrecedesSlantEqual",           "\xE2\x8B\xA0"              , 0 }, /* ⋠ */
    { "NotReverseElement",               "\xE2\x88\x8C"              , 0 }, /* ∌ */
    { "NotRightTriangle",                "\xE2\x8B\xAB"              , 0 }, /* ⋫ */
    { "NotRightTriangleBar",             "\xE2\xA7\x90\xCC\xB8"      , 0 }, /* ⧐̸ */
    { "NotRightTriangleEqual",           "\xE2\x8B\xAD"              , 0 }, /* ⋭ */
    { "NotSquareSubset",                 "\xE2\x8A\x8F\xCC\xB8"      , 0 }, /* ⊏̸ */
    { "NotSquareSubsetEqual",            "\xE2\x8B\xA2"              , 0 }, /* ⋢ */
    { "NotSquareSuperset",               "\xE2\x8A\x90\xCC\xB8"      , 0 }, /* ⊐̸ */
    { "NotSquareSupersetEqual",          "\xE2\x8B\xA3"              , 0 }, /* ⋣ */
    { "NotSubset",                       "\xE2\x8A\x82\xE2\x83\x92"  , 0 }, /* ⊂⃒ */
    { "NotSubsetEqual",                  "\xE2\x8A\x88"              , 0 }, /* ⊈ */
    { "NotSucceeds",                     "\xE2\x8A\x81"              , 0 }, /* ⊁ */
    { "NotSucceedsEqual",                "\xE2\xAA\xB0\xCC\xB8"      , 0 }, /* ⪰̸ */
    { "NotSucceedsSlantEqual",           "\xE2\x8B\xA1"              , 0 }, /* ⋡ */
    { "NotSucceedsTilde",                "\xE2\x89\xBF\xCC\xB8"      , 0 }, /* ≿̸ */
    { "NotSuperset",                     "\xE2\x8A\x83\xE2\x83\x92"  , 0 }, /* ⊃⃒ */
    { "NotSupersetEqual",                "\xE2\x8A\x89"              , 0 }, /* ⊉ */
    { "NotTilde",                        "\xE2\x89\x81"              , 0 }, /* ≁ */
    { "NotTildeEqual",                   "\xE2\x89\x84"              , 0 }, /* ≄ */
    { "NotTildeFullEqual",               "\xE2\x89\x87"              , 0 }, /* ≇ */
    { "NotTildeTilde",                   "\xE2\x89\x89"              , 0 }, /* ≉ */
    { "NotVerticalBar",                  "\xE2\x88\xA4"              , 0 }, /* ∤ */
    { "Nscr",                            "\xF0\x9D\x92\xA9"          , 0 }, /* 𝒩 */
    { "Ntilde",                          "\xC3\x91"                  , 0 }, /* Ñ */
    { "Nu",                              "\xCE\x9D"                  , 0 }, /* Ν */
    { "OElig",                           "\xC5\x92"                  , 0 }, /* Œ */
    { "Oacute",                          "\xC3\x93"                  , 0 }, /* Ó */
    { "Ocirc",                           "\xC3\x94"                  , 0 }, /* Ô */
    { "Ocy",                             "\xD0\x9E"                  , 0 }, /* О */
    { "Odblac",                          "\xC5\x90"                  , 0 }, /* Ő */
    { "Ofr",                             "\xF0\x9D\x94\x92"          , 0 }, /* 𝔒 */
    { "Ograve",                          "\xC3\x92"                  , 0 }, /* Ò */
    { "Omacr",                           "\xC5\x8C"                  , 0 }, /* Ō */
    { "Omega",                           "\xCE\xA9"                  , 0 }, /* Ω */
    { "Omicron",                         "\xCE\x9F"                  , 0 }, /* Ο */
    { "Oopf",                            "\xF0\x9D\x95\x86"          , 0 }, /* 𝕆 */
    { "OpenCurlyDoubleQuote",            "\xE2\x80\x9C"              , 0 }, /* “ */
    { "OpenCurlyQuote",                  "\xE2\x80\x98"              , 0 }, /* ‘ */
    { "Or",                              "\xE2\xA9\x94"              , 0 }, /* ⩔ */
    { "Oscr",                            "\xF0\x9D\x92\xAA"          , 0 }, /* 𝒪 */
    { "Oslash",                          "\xC3\x98"                  , 0 }, /* Ø */
    { "Otilde",                          "\xC3\x95"                  , 0 }, /* Õ */
    { "Otimes",                          "\xE2\xA8\xB7"              , 0 }, /* ⨷ */
    { "Ouml",                            "\xC3\x96"                  , 0 }, /* Ö */
    { "OverBar",                         "\xE2\x80\xBE"              , 0 }, /* ‾ */
    { "OverBrace",                       "\xE2\x8F\x9E"              , 0 }, /* ⏞ */
    { "OverBracket",                     "\xE2\x8E\xB4"              , 0 }, /* ⎴ */
    { "OverParenthesis",                 "\xE2\x8F\x9C"              , 0 }, /* ⏜ */
    { "PartialD",                        "\xE2\x88\x82"              , 0 }, /* ∂ */
    { "Pcy",                             "\xD0\x9F"                  , 0 }, /* П */
    { "Pfr",                             "\xF0\x9D\x94\x93"          , 0 }, /* 𝔓 */
    { "Phi",                             "\xCE\xA6"                  , 0 }, /* Φ */
    { "Pi",                              "\xCE\xA0"                  , 0 }, /* Π */
    { "PlusMinus",                       "\xC2\xB1"                  , 0 }, /* ± */
    { "Poincareplane",                   "\xE2\x84\x8C"              , 0 }, /* ℌ */
    { "Popf",                            "\xE2\x84\x99"              , 0 }, /* ℙ */
    { "Pr",                              "\xE2\xAA\xBB"              , 0 }, /* ⪻ */
    { "Precedes",                        "\xE2\x89\xBA"              , 0 }, /* ≺ */
    { "PrecedesEqual",                   "\xE2\xAA\xAF"              , 0 }, /* ⪯ */
    { "PrecedesSlantEqual",              "\xE2\x89\xBC"              , 0 }, /* ≼ */
    { "PrecedesTilde",                   "\xE2\x89\xBE"              , 0 }, /* ≾ */
    { "Prime",                           "\xE2\x80\xB3"              , 0 }, /* ″ */
    { "Product",                         "\xE2\x88\x8F"              , 0 }, /* ∏ */
    { "Proportion",                      "\xE2\x88\xB7"              , 0 }, /* ∷ */
    { "Proportional",                    "\xE2\x88\x9D"              , 0 }, /* ∝ */
    { "Pscr",                            "\xF0\x9D\x92\xAB"          , 0 }, /* 𝒫 */
    { "Psi",                             "\xCE\xA8"                  , 0 }, /* Ψ */
    { "QUOT",                            "\x22"                      , 0 }, /* " */
    { "Qfr",                             "\xF0\x9D\x94\x94"          , 0 }, /* 𝔔 */
    { "Qopf",                            "\xE2\x84\x9A"              , 0 }, /* ℚ */
    { "Qscr",                            "\xF0\x9D\x92\xAC"          , 0 }, /* 𝒬 */
    { "RBarr",                           "\xE2\xA4\x90"              , 0 }, /* ⤐ */
    { "REG",                             "\xC2\xAE"                  , 0 }, /* ® */
    { "Racute",                          "\xC5\x94"                  , 0 }, /* Ŕ */
    { "Rang",                            "\xE2\x9F\xAB"              , 0 }, /* ⟫ */
    { "Rarr",                            "\xE2\x86\xA0"              , 0 }, /* ↠ */
    { "Rarrtl",                          "\xE2\xA4\x96"              , 0 }, /* ⤖ */
    { "Rcaron",                          "\xC5\x98"                  , 0 }, /* Ř */
    { "Rcedil",                          "\xC5\x96"                  , 0 }, /* Ŗ */
    { "Rcy",                             "\xD0\xA0"                  , 0 }, /* Р */
    { "Re",                              "\xE2\x84\x9C"              , 0 }, /* ℜ */
    { "ReverseElement",                  "\xE2\x88\x8B"              , 0 }, /* ∋ */
    { "ReverseEquilibrium",              "\xE2\x87\x8B"              , 0 }, /* ⇋ */
    { "ReverseUpEquilibrium",            "\xE2\xA5\xAF"              , 0 }, /* ⥯ */
    { "Rfr",                             "\xE2\x84\x9C"              , 0 }, /* ℜ */
    { "Rho",                             "\xCE\xA1"                  , 0 }, /* Ρ */
    { "RightAngleBracket",               "\xE2\x9F\xA9"              , 0 }, /* ⟩ */
    { "RightArrow",                      "\xE2\x86\x92"              , 0 }, /* → */
    { "RightArrowBar",                   "\xE2\x87\xA5"              , 0 }, /* ⇥ */
    { "RightArrowLeftArrow",             "\xE2\x87\x84"              , 0 }, /* ⇄ */
    { "RightCeiling",                    "\xE2\x8C\x89"              , 0 }, /* ⌉ */
    { "RightDoubleBracket",              "\xE2\x9F\xA7"              , 0 }, /* ⟧ */
    { "RightDownTeeVector",              "\xE2\xA5\x9D"              , 0 }, /* ⥝ */
    { "RightDownVector",                 "\xE2\x87\x82"              , 0 }, /* ⇂ */
    { "RightDownVectorBar",              "\xE2\xA5\x95"              , 0 }, /* ⥕ */
    { "RightFloor",                      "\xE2\x8C\x8B"              , 0 }, /* ⌋ */
    { "RightTee",                        "\xE2\x8A\xA2"              , 0 }, /* ⊢ */
    { "RightTeeArrow",                   "\xE2\x86\xA6"              , 0 }, /* ↦ */
    { "RightTeeVector",                  "\xE2\xA5\x9B"              , 0 }, /* ⥛ */
    { "RightTriangle",                   "\xE2\x8A\xB3"              , 0 }, /* ⊳ */
    { "RightTriangleBar",                "\xE2\xA7\x90"              , 0 }, /* ⧐ */
    { "RightTriangleEqual",              "\xE2\x8A\xB5"              , 0 }, /* ⊵ */
    { "RightUpDownVector",               "\xE2\xA5\x8F"              , 0 }, /* ⥏ */
    { "RightUpTeeVector",                "\xE2\xA5\x9C"              , 0 }, /* ⥜ */
    { "RightUpVector",                   "\xE2\x86\xBE"              , 0 }, /* ↾ */
    { "RightUpVectorBar",                "\xE2\xA5\x94"              , 0 }, /* ⥔ */
    { "RightVector",                     "\xE2\x87\x80"              , 0 }, /* ⇀ */
    { "RightVectorBar",                  "\xE2\xA5\x93"              , 0 }, /* ⥓ */
    { "Rightarrow",                      "\xE2\x87\x92"              , 0 }, /* ⇒ */
    { "Ropf",                            "\xE2\x84\x9D"              , 0 }, /* ℝ */
    { "RoundImplies",                    "\xE2\xA5\xB0"              , 0 }, /* ⥰ */
    { "Rrightarrow",                     "\xE2\x87\x9B"              , 0 }, /* ⇛ */
    { "Rscr",                            "\xE2\x84\x9B"              , 0 }, /* ℛ */
    { "Rsh",                             "\xE2\x86\xB1"              , 0 }, /* ↱ */
    { "RuleDelayed",                     "\xE2\xA7\xB4"              , 0 }, /* ⧴ */
    { "SHCHcy",                          "\xD0\xA9"                  , 0 }, /* Щ */
    { "SHcy",                            "\xD0\xA8"                  , 0 }, /* Ш */
    { "SOFTcy",                          "\xD0\xAC"                  , 0 }, /* Ь */
    { "Sacute",                          "\xC5\x9A"                  , 0 }, /* Ś */
    { "Sc",                              "\xE2\xAA\xBC"              , 0 }, /* ⪼ */
    { "Scaron",                          "\xC5\xA0"                  , 0 }, /* Š */
    { "Scedil",                          "\xC5\x9E"                  , 0 }, /* Ş */
    { "Scirc",                           "\xC5\x9C"                  , 0 }, /* Ŝ */
    { "Scy",                             "\xD0\xA1"                  , 0 }, /* С */
    { "Sfr",                             "\xF0\x9D\x94\x96"          , 0 }, /* 𝔖 */
    { "ShortDownArrow",                  "\xE2\x86\x93"              , 0 }, /* ↓ */
    { "ShortLeftArrow",                  "\xE2\x86\x90"              , 0 }, /* ← */
    { "ShortRightArrow",                 "\xE2\x86\x92"              , 0 }, /* → */
    { "ShortUpArrow",                    "\xE2\x86\x91"              , 0 }, /* ↑ */
    { "Sigma",                           "\xCE\xA3"                  , 0 }, /* Σ */
    { "SmallCircle",                     "\xE2\x88\x98"              , 0 }, /* ∘ */
    { "Sopf",                            "\xF0\x9D\x95\x8A"          , 0 }, /* 𝕊 */
    { "Sqrt",                            "\xE2\x88\x9A"              , 0 }, /* √ */
    { "Square",                          "\xE2\x96\xA1"              , 0 }, /* □ */
    { "SquareIntersection",              "\xE2\x8A\x93"              , 0 }, /* ⊓ */
    { "SquareSubset",                    "\xE2\x8A\x8F"              , 0 }, /* ⊏ */
    { "SquareSubsetEqual",               "\xE2\x8A\x91"              , 0 }, /* ⊑ */
    { "SquareSuperset",                  "\xE2\x8A\x90"              , 0 }, /* ⊐ */
    { "SquareSupersetEqual",             "\xE2\x8A\x92"              , 0 }, /* ⊒ */
    { "SquareUnion",                     "\xE2\x8A\x94"              , 0 }, /* ⊔ */
    { "Sscr",                            "\xF0\x9D\x92\xAE"          , 0 }, /* 𝒮 */
    { "Star",                            "\xE2\x8B\x86"              , 0 }, /* ⋆ */
    { "Sub",                             "\xE2\x8B\x90"              , 0 }, /* ⋐ */
    { "Subset",                          "\xE2\x8B\x90"              , 0 }, /* ⋐ */
    { "SubsetEqual",                     "\xE2\x8A\x86"              , 0 }, /* ⊆ */
    { "Succeeds",                        "\xE2\x89\xBB"              , 0 }, /* ≻ */
    { "SucceedsEqual",                   "\xE2\xAA\xB0"              , 0 }, /* ⪰ */
    { "SucceedsSlantEqual",              "\xE2\x89\xBD"              , 0 }, /* ≽ */
    { "SucceedsTilde",                   "\xE2\x89\xBF"              , 0 }, /* ≿ */
    { "SuchThat",                        "\xE2\x88\x8B"              , 0 }, /* ∋ */
    { "Sum",                             "\xE2\x88\x91"              , 0 }, /* ∑ */
    { "Sup",                             "\xE2\x8B\x91"              , 0 }, /* ⋑ */
    { "Superset",                        "\xE2\x8A\x83"              , 0 }, /* ⊃ */
    { "SupersetEqual",                   "\xE2\x8A\x87"              , 0 }, /* ⊇ */
    { "Supset",                          "\xE2\x8B\x91"              , 0 }, /* ⋑ */
    { "THORN",                           "\xC3\x9E"                  , 0 }, /* Þ */
    { "TRADE",                           "\xE2\x84\xA2"              , 0 }, /* ™ */
    { "TSHcy",                           "\xD0\x8B"                  , 0 }, /* Ћ */
    { "TScy",                            "\xD0\xA6"                  , 0 }, /* Ц */
    { "Tab",                             "\x09"                      , 0 }, /* 	 */
    { "Tau",                             "\xCE\xA4"                  , 0 }, /* Τ */
    { "Tcaron",                          "\xC5\xA4"                  , 0 }, /* Ť */
    { "Tcedil",                          "\xC5\xA2"                  , 0 }, /* Ţ */
    { "Tcy",                             "\xD0\xA2"                  , 0 }, /* Т */
    { "Tfr",                             "\xF0\x9D\x94\x97"          , 0 }, /* 𝔗 */
    { "Therefore",                       "\xE2\x88\xB4"              , 0 }, /* ∴ */
    { "Theta",                           "\xCE\x98"                  , 0 }, /* Θ */
    { "ThickSpace",                      "\xE2\x81\x9F\xE2\x80\x8A"  , 0 }, /*    */
    { "ThinSpace",                       "\xE2\x80\x89"              , 0 }, /*   */
    { "Tilde",                           "\xE2\x88\xBC"              , 0 }, /* ∼ */
    { "TildeEqual",                      "\xE2\x89\x83"              , 0 }, /* ≃ */
    { "TildeFullEqual",                  "\xE2\x89\x85"              , 0 }, /* ≅ */
    { "TildeTilde",                      "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "Topf",                            "\xF0\x9D\x95\x8B"          , 0 }, /* 𝕋 */
    { "TripleDot",                       "\x20\xE2\x83\x9B"          , 0 }, /*  ⃛ */
    { "Tscr",                            "\xF0\x9D\x92\xAF"          , 0 }, /* 𝒯 */
    { "Tstrok",                          "\xC5\xA6"                  , 0 }, /* Ŧ */
    { "Uacute",                          "\xC3\x9A"                  , 0 }, /* Ú */
    { "Uarr",                            "\xE2\x86\x9F"              , 0 }, /* ↟ */
    { "Uarrocir",                        "\xE2\xA5\x89"              , 0 }, /* ⥉ */
    { "Ubrcy",                           "\xD0\x8E"                  , 0 }, /* Ў */
    { "Ubreve",                          "\xC5\xAC"                  , 0 }, /* Ŭ */
    { "Ucirc",                           "\xC3\x9B"                  , 0 }, /* Û */
    { "Ucy",                             "\xD0\xA3"                  , 0 }, /* У */
    { "Udblac",                          "\xC5\xB0"                  , 0 }, /* Ű */
    { "Ufr",                             "\xF0\x9D\x94\x98"          , 0 }, /* 𝔘 */
    { "Ugrave",                          "\xC3\x99"                  , 0 }, /* Ù */
    { "Umacr",                           "\xC5\xAA"                  , 0 }, /* Ū */
    { "UnderBar",                        "\x5F"                      , 0 }, /* _ */
    { "UnderBrace",                      "\xE2\x8F\x9F"              , 0 }, /* ⏟ */
    { "UnderBracket",                    "\xE2\x8E\xB5"              , 0 }, /* ⎵ */
    { "UnderParenthesis",                "\xE2\x8F\x9D"              , 0 }, /* ⏝ */
    { "Union",                           "\xE2\x8B\x83"              , 0 }, /* ⋃ */
    { "UnionPlus",                       "\xE2\x8A\x8E"              , 0 }, /* ⊎ */
    { "Uogon",                           "\xC5\xB2"                  , 0 }, /* Ų */
    { "Uopf",                            "\xF0\x9D\x95\x8C"          , 0 }, /* 𝕌 */
    { "UpArrow",                         "\xE2\x86\x91"              , 0 }, /* ↑ */
    { "UpArrowBar",                      "\xE2\xA4\x92"              , 0 }, /* ⤒ */
    { "UpArrowDownArrow",                "\xE2\x87\x85"              , 0 }, /* ⇅ */
    { "UpDownArrow",                     "\xE2\x86\x95"              , 0 }, /* ↕ */
    { "UpEquilibrium",                   "\xE2\xA5\xAE"              , 0 }, /* ⥮ */
    { "UpTee",                           "\xE2\x8A\xA5"              , 0 }, /* ⊥ */
    { "UpTeeArrow",                      "\xE2\x86\xA5"              , 0 }, /* ↥ */
    { "Uparrow",                         "\xE2\x87\x91"              , 0 }, /* ⇑ */
    { "Updownarrow",                     "\xE2\x87\x95"              , 0 }, /* ⇕ */
    { "UpperLeftArrow",                  "\xE2\x86\x96"              , 0 }, /* ↖ */
    { "UpperRightArrow",                 "\xE2\x86\x97"              , 0 }, /* ↗ */
    { "Upsi",                            "\xCF\x92"                  , 0 }, /* ϒ */
    { "Upsilon",                         "\xCE\xA5"                  , 0 }, /* Υ */
    { "Uring",                           "\xC5\xAE"                  , 0 }, /* Ů */
    { "Uscr",                            "\xF0\x9D\x92\xB0"          , 0 }, /* 𝒰 */
    { "Utilde",                          "\xC5\xA8"                  , 0 }, /* Ũ */
    { "Uuml",                            "\xC3\x9C"                  , 0 }, /* Ü */
    { "VDash",                           "\xE2\x8A\xAB"              , 0 }, /* ⊫ */
    { "Vbar",                            "\xE2\xAB\xAB"              , 0 }, /* ⫫ */
    { "Vcy",                             "\xD0\x92"                  , 0 }, /* В */
    { "Vdash",                           "\xE2\x8A\xA9"              , 0 }, /* ⊩ */
    { "Vdashl",                          "\xE2\xAB\xA6"              , 0 }, /* ⫦ */
    { "Vee",                             "\xE2\x8B\x81"              , 0 }, /* ⋁ */
    { "Verbar",                          "\xE2\x80\x96"              , 0 }, /* ‖ */
    { "Vert",                            "\xE2\x80\x96"              , 0 }, /* ‖ */
    { "VerticalBar",                     "\xE2\x88\xA3"              , 0 }, /* ∣ */
    { "VerticalLine",                    "\x7C"                      , 0 }, /* | */
    { "VerticalSeparator",               "\xE2\x9D\x98"              , 0 }, /* ❘ */
    { "VerticalTilde",                   "\xE2\x89\x80"              , 0 }, /* ≀ */
    { "VeryThinSpace",                   "\xE2\x80\x8A"              , 0 }, /*   */
    { "Vfr",                             "\xF0\x9D\x94\x99"          , 0 }, /* 𝔙 */
    { "Vopf",                            "\xF0\x9D\x95\x8D"          , 0 }, /* 𝕍 */
    { "Vscr",                            "\xF0\x9D\x92\xB1"          , 0 }, /* 𝒱 */
    { "Vvdash",                          "\xE2\x8A\xAA"              , 0 }, /* ⊪ */
    { "Wcirc",                           "\xC5\xB4"                  , 0 }, /* Ŵ */
    { "Wedge",                           "\xE2\x8B\x80"              , 0 }, /* ⋀ */
    { "Wfr",                             "\xF0\x9D\x94\x9A"          , 0 }, /* 𝔚 */
    { "Wopf",                            "\xF0\x9D\x95\x8E"          , 0 }, /* 𝕎 */
    { "Wscr",                            "\xF0\x9D\x92\xB2"          , 0 }, /* 𝒲 */
    { "Xfr",                             "\xF0\x9D\x94\x9B"          , 0 }, /* 𝔛 */
    { "Xi",                              "\xCE\x9E"                  , 0 }, /* Ξ */
    { "Xopf",                            "\xF0\x9D\x95\x8F"          , 0 }, /* 𝕏 */
    { "Xscr",                            "\xF0\x9D\x92\xB3"          , 0 }, /* 𝒳 */
    { "YAcy",                            "\xD0\xAF"                  , 0 }, /* Я */
    { "YIcy",                            "\xD0\x87"                  , 0 }, /* Ї */
    { "YUcy",                            "\xD0\xAE"                  , 0 }, /* Ю */
    { "Yacute",                          "\xC3\x9D"                  , 0 }, /* Ý */
    { "Ycirc",                           "\xC5\xB6"                  , 0 }, /* Ŷ */
    { "Ycy",                             "\xD0\xAB"                  , 0 }, /* Ы */
    { "Yfr",                             "\xF0\x9D\x94\x9C"          , 0 }, /* 𝔜 */
    { "Yopf",                            "\xF0\x9D\x95\x90"          , 0 }, /* 𝕐 */
    { "Yscr",                            "\xF0\x9D\x92\xB4"          , 0 }, /* 𝒴 */
    { "Yuml",                            "\xC5\xB8"                  , 0 }, /* Ÿ */
    { "ZHcy",                            "\xD0\x96"                  , 0 }, /* Ж */
    { "Zacute",                          "\xC5\xB9"                  , 0 }, /* Ź */
    { "Zcaron",                          "\xC5\xBD"                  , 0 }, /* Ž */
    { "Zcy",                             "\xD0\x97"                  , 0 }, /* З */
    { "Zdot",                            "\xC5\xBB"                  , 0 }, /* Ż */
    { "ZeroWidthSpace",                  "\xE2\x80\x8B"              , 0 }, /* ​ */
    { "Zeta",                            "\xCE\x96"                  , 0 }, /* Ζ */
    { "Zfr",                             "\xE2\x84\xA8"              , 0 }, /* ℨ */
    { "Zopf",                            "\xE2\x84\xA4"              , 0 }, /* ℤ */
    { "Zscr",                            "\xF0\x9D\x92\xB5"          , 0 }, /* 𝒵 */
    { "aacute",                          "\xC3\xA1"                  , 0 }, /* á */
    { "abreve",                          "\xC4\x83"                  , 0 }, /* ă */
    { "ac",                              "\xE2\x88\xBE"              , 0 }, /* ∾ */
    { "acE",                             "\xE2\x88\xBE\xCC\xB3"      , 0 }, /* ∾̳ */
    { "acd",                             "\xE2\x88\xBF"              , 0 }, /* ∿ */
    { "acirc",                           "\xC3\xA2"                  , 0 }, /* â */
    { "acute",                           "\xC2\xB4"                  , 0 }, /* ´ */
    { "acy",                             "\xD0\xB0"                  , 0 }, /* а */
    { "aelig",                           "\xC3\xA6"                  , 0 }, /* æ */
    { "af",                              "\xE2\x81\xA1"              , 0 }, /* ⁡ */
    { "afr",                             "\xF0\x9D\x94\x9E"          , 0 }, /* 𝔞 */
    { "agrave",                          "\xC3\xA0"                  , 0 }, /* à */
    { "alefsym",                         "\xE2\x84\xB5"              , 0 }, /* ℵ */
    { "aleph",                           "\xE2\x84\xB5"              , 0 }, /* ℵ */
    { "alpha",                           "\xCE\xB1"                  , 0 }, /* α */
    { "amacr",                           "\xC4\x81"                  , 0 }, /* ā */
    { "amalg",                           "\xE2\xA8\xBF"              , 0 }, /* ⨿ */
    { "amp",                             "\x26"                      , 0 }, /* & */
    { "and",                             "\xE2\x88\xA7"              , 0 }, /* ∧ */
    { "andand",                          "\xE2\xA9\x95"              , 0 }, /* ⩕ */
    { "andd",                            "\xE2\xA9\x9C"              , 0 }, /* ⩜ */
    { "andslope",                        "\xE2\xA9\x98"              , 0 }, /* ⩘ */
    { "andv",                            "\xE2\xA9\x9A"              , 0 }, /* ⩚ */
    { "ang",                             "\xE2\x88\xA0"              , 0 }, /* ∠ */
    { "ange",                            "\xE2\xA6\xA4"              , 0 }, /* ⦤ */
    { "angle",                           "\xE2\x88\xA0"              , 0 }, /* ∠ */
    { "angmsd",                          "\xE2\x88\xA1"              , 0 }, /* ∡ */
    { "angmsdaa",                        "\xE2\xA6\xA8"              , 0 }, /* ⦨ */
    { "angmsdab",                        "\xE2\xA6\xA9"              , 0 }, /* ⦩ */
    { "angmsdac",                        "\xE2\xA6\xAA"              , 0 }, /* ⦪ */
    { "angmsdad",                        "\xE2\xA6\xAB"              , 0 }, /* ⦫ */
    { "angmsdae",                        "\xE2\xA6\xAC"              , 0 }, /* ⦬ */
    { "angmsdaf",                        "\xE2\xA6\xAD"              , 0 }, /* ⦭ */
    { "angmsdag",                        "\xE2\xA6\xAE"              , 0 }, /* ⦮ */
    { "angmsdah",                        "\xE2\xA6\xAF"              , 0 }, /* ⦯ */
    { "angrt",                           "\xE2\x88\x9F"              , 0 }, /* ∟ */
    { "angrtvb",                         "\xE2\x8A\xBE"              , 0 }, /* ⊾ */
    { "angrtvbd",                        "\xE2\xA6\x9D"              , 0 }, /* ⦝ */
    { "angsph",                          "\xE2\x88\xA2"              , 0 }, /* ∢ */
    { "angst",                           "\xC3\x85"                  , 0 }, /* Å */
    { "angzarr",                         "\xE2\x8D\xBC"              , 0 }, /* ⍼ */
    { "aogon",                           "\xC4\x85"                  , 0 }, /* ą */
    { "aopf",                            "\xF0\x9D\x95\x92"          , 0 }, /* 𝕒 */
    { "ap",                              "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "apE",                             "\xE2\xA9\xB0"              , 0 }, /* ⩰ */
    { "apacir",                          "\xE2\xA9\xAF"              , 0 }, /* ⩯ */
    { "ape",                             "\xE2\x89\x8A"              , 0 }, /* ≊ */
    { "apid",                            "\xE2\x89\x8B"              , 0 }, /* ≋ */
    { "apos",                            "\x27"                      , 0 }, /* ' */
    { "approx",                          "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "approxeq",                        "\xE2\x89\x8A"              , 0 }, /* ≊ */
    { "aring",                           "\xC3\xA5"                  , 0 }, /* å */
    { "ascr",                            "\xF0\x9D\x92\xB6"          , 0 }, /* 𝒶 */
    { "ast",                             "\x2A"                      , 0 }, /* * */
    { "asymp",                           "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "asympeq",                         "\xE2\x89\x8D"              , 0 }, /* ≍ */
    { "atilde",                          "\xC3\xA3"                  , 0 }, /* ã */
    { "auml",                            "\xC3\xA4"                  , 0 }, /* ä */
    { "awconint",                        "\xE2\x88\xB3"              , 0 }, /* ∳ */
    { "awint",                           "\xE2\xA8\x91"              , 0 }, /* ⨑ */
    { "bNot",                            "\xE2\xAB\xAD"              , 0 }, /* ⫭ */
    { "backcong",                        "\xE2\x89\x8C"              , 0 }, /* ≌ */
    { "backepsilon",                     "\xCF\xB6"                  , 0 }, /* ϶ */
    { "backprime",                       "\xE2\x80\xB5"              , 0 }, /* ‵ */
    { "backsim",                         "\xE2\x88\xBD"              , 0 }, /* ∽ */
    { "backsimeq",                       "\xE2\x8B\x8D"              , 0 }, /* ⋍ */
    { "barvee",                          "\xE2\x8A\xBD"              , 0 }, /* ⊽ */
    { "barwed",                          "\xE2\x8C\x85"              , 0 }, /* ⌅ */
    { "barwedge",                        "\xE2\x8C\x85"              , 0 }, /* ⌅ */
    { "bbrk",                            "\xE2\x8E\xB5"              , 0 }, /* ⎵ */
    { "bbrktbrk",                        "\xE2\x8E\xB6"              , 0 }, /* ⎶ */
    { "bcong",                           "\xE2\x89\x8C"              , 0 }, /* ≌ */
    { "bcy",                             "\xD0\xB1"                  , 0 }, /* б */
    { "bdquo",                           "\xE2\x80\x9E"              , 0 }, /* „ */
    { "becaus",                          "\xE2\x88\xB5"              , 0 }, /* ∵ */
    { "because",                         "\xE2\x88\xB5"              , 0 }, /* ∵ */
    { "bemptyv",                         "\xE2\xA6\xB0"              , 0 }, /* ⦰ */
    { "bepsi",                           "\xCF\xB6"                  , 0 }, /* ϶ */
    { "bernou",                          "\xE2\x84\xAC"              , 0 }, /* ℬ */
    { "beta",                            "\xCE\xB2"                  , 0 }, /* β */
    { "beth",                            "\xE2\x84\xB6"              , 0 }, /* ℶ */
    { "between",                         "\xE2\x89\xAC"              , 0 }, /* ≬ */
    { "bfr",                             "\xF0\x9D\x94\x9F"          , 0 }, /* 𝔟 */
    { "bigcap",                          "\xE2\x8B\x82"              , 0 }, /* ⋂ */
    { "bigcirc",                         "\xE2\x97\xAF"              , 0 }, /* ◯ */
    { "bigcup",                          "\xE2\x8B\x83"              , 0 }, /* ⋃ */
    { "bigodot",                         "\xE2\xA8\x80"              , 0 }, /* ⨀ */
    { "bigoplus",                        "\xE2\xA8\x81"              , 0 }, /* ⨁ */
    { "bigotimes",                       "\xE2\xA8\x82"              , 0 }, /* ⨂ */
    { "bigsqcup",                        "\xE2\xA8\x86"              , 0 }, /* ⨆ */
    { "bigstar",                         "\xE2\x98\x85"              , 0 }, /* ★ */
    { "bigtriangledown",                 "\xE2\x96\xBD"              , 0 }, /* ▽ */
    { "bigtriangleup",                   "\xE2\x96\xB3"              , 0 }, /* △ */
    { "biguplus",                        "\xE2\xA8\x84"              , 0 }, /* ⨄ */
    { "bigvee",                          "\xE2\x8B\x81"              , 0 }, /* ⋁ */
    { "bigwedge",                        "\xE2\x8B\x80"              , 0 }, /* ⋀ */
    { "bkarow",                          "\xE2\xA4\x8D"              , 0 }, /* ⤍ */
    { "blacklozenge",                    "\xE2\xA7\xAB"              , 0 }, /* ⧫ */
    { "blacksquare",                     "\xE2\x96\xAA"              , 0 }, /* ▪ */
    { "blacktriangle",                   "\xE2\x96\xB4"              , 0 }, /* ▴ */
    { "blacktriangledown",               "\xE2\x96\xBE"              , 0 }, /* ▾ */
    { "blacktriangleleft",               "\xE2\x97\x82"              , 0 }, /* ◂ */
    { "blacktriangleright",              "\xE2\x96\xB8"              , 0 }, /* ▸ */
    { "blank",                           "\xE2\x90\xA3"              , 0 }, /* ␣ */
    { "blk12",                           "\xE2\x96\x92"              , 0 }, /* ▒ */
    { "blk14",                           "\xE2\x96\x91"              , 0 }, /* ░ */
    { "blk34",                           "\xE2\x96\x93"              , 0 }, /* ▓ */
    { "block",                           "\xE2\x96\x88"              , 0 }, /* █ */
    { "bne",                             "\x3D\xE2\x83\xA5"          , 0 }, /* =⃥ */
    { "bnequiv",                         "\xE2\x89\xA1\xE2\x83\xA5"  , 0 }, /* ≡⃥ */
    { "bnot",                            "\xE2\x8C\x90"              , 0 }, /* ⌐ */
    { "bopf",                            "\xF0\x9D\x95\x93"          , 0 }, /* 𝕓 */
    { "bot",                             "\xE2\x8A\xA5"              , 0 }, /* ⊥ */
    { "bottom",                          "\xE2\x8A\xA5"              , 0 }, /* ⊥ */
    { "bowtie",                          "\xE2\x8B\x88"              , 0 }, /* ⋈ */
    { "boxDL",                           "\xE2\x95\x97"              , 0 }, /* ╗ */
    { "boxDR",                           "\xE2\x95\x94"              , 0 }, /* ╔ */
    { "boxDl",                           "\xE2\x95\x96"              , 0 }, /* ╖ */
    { "boxDr",                           "\xE2\x95\x93"              , 0 }, /* ╓ */
    { "boxH",                            "\xE2\x95\x90"              , 0 }, /* ═ */
    { "boxHD",                           "\xE2\x95\xA6"              , 0 }, /* ╦ */
    { "boxHU",                           "\xE2\x95\xA9"              , 0 }, /* ╩ */
    { "boxHd",                           "\xE2\x95\xA4"              , 0 }, /* ╤ */
    { "boxHu",                           "\xE2\x95\xA7"              , 0 }, /* ╧ */
    { "boxUL",                           "\xE2\x95\x9D"              , 0 }, /* ╝ */
    { "boxUR",                           "\xE2\x95\x9A"              , 0 }, /* ╚ */
    { "boxUl",                           "\xE2\x95\x9C"              , 0 }, /* ╜ */
    { "boxUr",                           "\xE2\x95\x99"              , 0 }, /* ╙ */
    { "boxV",                            "\xE2\x95\x91"              , 0 }, /* ║ */
    { "boxVH",                           "\xE2\x95\xAC"              , 0 }, /* ╬ */
    { "boxVL",                           "\xE2\x95\xA3"              , 0 }, /* ╣ */
    { "boxVR",                           "\xE2\x95\xA0"              , 0 }, /* ╠ */
    { "boxVh",                           "\xE2\x95\xAB"              , 0 }, /* ╫ */
    { "boxVl",                           "\xE2\x95\xA2"              , 0 }, /* ╢ */
    { "boxVr",                           "\xE2\x95\x9F"              , 0 }, /* ╟ */
    { "boxbox",                          "\xE2\xA7\x89"              , 0 }, /* ⧉ */
    { "boxdL",                           "\xE2\x95\x95"              , 0 }, /* ╕ */
    { "boxdR",                           "\xE2\x95\x92"              , 0 }, /* ╒ */
    { "boxdl",                           "\xE2\x94\x90"              , 0 }, /* ┐ */
    { "boxdr",                           "\xE2\x94\x8C"              , 0 }, /* ┌ */
    { "boxh",                            "\xE2\x94\x80"              , 0 }, /* ─ */
    { "boxhD",                           "\xE2\x95\xA5"              , 0 }, /* ╥ */
    { "boxhU",                           "\xE2\x95\xA8"              , 0 }, /* ╨ */
    { "boxhd",                           "\xE2\x94\xAC"              , 0 }, /* ┬ */
    { "boxhu",                           "\xE2\x94\xB4"              , 0 }, /* ┴ */
    { "boxminus",                        "\xE2\x8A\x9F"              , 0 }, /* ⊟ */
    { "boxplus",                         "\xE2\x8A\x9E"              , 0 }, /* ⊞ */
    { "boxtimes",                        "\xE2\x8A\xA0"              , 0 }, /* ⊠ */
    { "boxuL",                           "\xE2\x95\x9B"              , 0 }, /* ╛ */
    { "boxuR",                           "\xE2\x95\x98"              , 0 }, /* ╘ */
    { "boxul",                           "\xE2\x94\x98"              , 0 }, /* ┘ */
    { "boxur",                           "\xE2\x94\x94"              , 0 }, /* └ */
    { "boxv",                            "\xE2\x94\x82"              , 0 }, /* │ */
    { "boxvH",                           "\xE2\x95\xAA"              , 0 }, /* ╪ */
    { "boxvL",                           "\xE2\x95\xA1"              , 0 }, /* ╡ */
    { "boxvR",                           "\xE2\x95\x9E"              , 0 }, /* ╞ */
    { "boxvh",                           "\xE2\x94\xBC"              , 0 }, /* ┼ */
    { "boxvl",                           "\xE2\x94\xA4"              , 0 }, /* ┤ */
    { "boxvr",                           "\xE2\x94\x9C"              , 0 }, /* ├ */
    { "bprime",                          "\xE2\x80\xB5"              , 0 }, /* ‵ */
    { "breve",                           "\xCB\x98"                  , 0 }, /* ˘ */
    { "brvbar",                          "\xC2\xA6"                  , 0 }, /* ¦ */
    { "bscr",                            "\xF0\x9D\x92\xB7"          , 0 }, /* 𝒷 */
    { "bsemi",                           "\xE2\x81\x8F"              , 0 }, /* ⁏ */
    { "bsim",                            "\xE2\x88\xBD"              , 0 }, /* ∽ */
    { "bsime",                           "\xE2\x8B\x8D"              , 0 }, /* ⋍ */
    { "bsol",                            "\x5C"                      , 0 }, /* \ */
    { "bsolb",                           "\xE2\xA7\x85"              , 0 }, /* ⧅ */
    { "bsolhsub",                        "\xE2\x9F\x88"              , 0 }, /* ⟈ */
    { "bull",                            "\xE2\x80\xA2"              , 0 }, /* • */
    { "bullet",                          "\xE2\x80\xA2"              , 0 }, /* • */
    { "bump",                            "\xE2\x89\x8E"              , 0 }, /* ≎ */
    { "bumpE",                           "\xE2\xAA\xAE"              , 0 }, /* ⪮ */
    { "bumpe",                           "\xE2\x89\x8F"              , 0 }, /* ≏ */
    { "bumpeq",                          "\xE2\x89\x8F"              , 0 }, /* ≏ */
    { "cacute",                          "\xC4\x87"                  , 0 }, /* ć */
    { "cap",                             "\xE2\x88\xA9"              , 0 }, /* ∩ */
    { "capand",                          "\xE2\xA9\x84"              , 0 }, /* ⩄ */
    { "capbrcup",                        "\xE2\xA9\x89"              , 0 }, /* ⩉ */
    { "capcap",                          "\xE2\xA9\x8B"              , 0 }, /* ⩋ */
    { "capcup",                          "\xE2\xA9\x87"              , 0 }, /* ⩇ */
    { "capdot",                          "\xE2\xA9\x80"              , 0 }, /* ⩀ */
    { "caps",                            "\xE2\x88\xA9\xEF\xB8\x80"  , 0 }, /* ∩︀ */
    { "caret",                           "\xE2\x81\x81"              , 0 }, /* ⁁ */
    { "caron",                           "\xCB\x87"                  , 0 }, /* ˇ */
    { "ccaps",                           "\xE2\xA9\x8D"              , 0 }, /* ⩍ */
    { "ccaron",                          "\xC4\x8D"                  , 0 }, /* č */
    { "ccedil",                          "\xC3\xA7"                  , 0 }, /* ç */
    { "ccirc",                           "\xC4\x89"                  , 0 }, /* ĉ */
    { "ccups",                           "\xE2\xA9\x8C"              , 0 }, /* ⩌ */
    { "ccupssm",                         "\xE2\xA9\x90"              , 0 }, /* ⩐ */
    { "cdot",                            "\xC4\x8B"                  , 0 }, /* ċ */
    { "cedil",                           "\xC2\xB8"                  , 0 }, /* ¸ */
    { "cemptyv",                         "\xE2\xA6\xB2"              , 0 }, /* ⦲ */
    { "cent",                            "\xC2\xA2"                  , 0 }, /* ¢ */
    { "centerdot",                       "\xC2\xB7"                  , 0 }, /* · */
    { "cfr",                             "\xF0\x9D\x94\xA0"          , 0 }, /* 𝔠 */
    { "chcy",                            "\xD1\x87"                  , 0 }, /* ч */
    { "check",                           "\xE2\x9C\x93"              , 0 }, /* ✓ */
    { "checkmark",                       "\xE2\x9C\x93"              , 0 }, /* ✓ */
    { "chi",                             "\xCF\x87"                  , 0 }, /* χ */
    { "cir",                             "\xE2\x97\x8B"              , 0 }, /* ○ */
    { "cirE",                            "\xE2\xA7\x83"              , 0 }, /* ⧃ */
    { "circ",                            "\xCB\x86"                  , 0 }, /* ˆ */
    { "circeq",                          "\xE2\x89\x97"              , 0 }, /* ≗ */
    { "circlearrowleft",                 "\xE2\x86\xBA"              , 0 }, /* ↺ */
    { "circlearrowright",                "\xE2\x86\xBB"              , 0 }, /* ↻ */
    { "circledR",                        "\xC2\xAE"                  , 0 }, /* ® */
    { "circledS",                        "\xE2\x93\x88"              , 0 }, /* Ⓢ */
    { "circledast",                      "\xE2\x8A\x9B"              , 0 }, /* ⊛ */
    { "circledcirc",                     "\xE2\x8A\x9A"              , 0 }, /* ⊚ */
    { "circleddash",                     "\xE2\x8A\x9D"              , 0 }, /* ⊝ */
    { "cire",                            "\xE2\x89\x97"              , 0 }, /* ≗ */
    { "cirfnint",                        "\xE2\xA8\x90"              , 0 }, /* ⨐ */
    { "cirmid",                          "\xE2\xAB\xAF"              , 0 }, /* ⫯ */
    { "cirscir",                         "\xE2\xA7\x82"              , 0 }, /* ⧂ */
    { "clubs",                           "\xE2\x99\xA3"              , 0 }, /* ♣ */
    { "clubsuit",                        "\xE2\x99\xA3"              , 0 }, /* ♣ */
    { "colon",                           "\x3A"                      , 0 }, /* : */
    { "colone",                          "\xE2\x89\x94"              , 0 }, /* ≔ */
    { "coloneq",                         "\xE2\x89\x94"              , 0 }, /* ≔ */
    { "comma",                           "\x2C"                      , 0 }, /* , */
    { "commat",                          "\x40"                      , 0 }, /* @ */
    { "comp",                            "\xE2\x88\x81"              , 0 }, /* ∁ */
    { "compfn",                          "\xE2\x88\x98"              , 0 }, /* ∘ */
    { "complement",                      "\xE2\x88\x81"              , 0 }, /* ∁ */
    { "complexes",                       "\xE2\x84\x82"              , 0 }, /* ℂ */
    { "cong",                            "\xE2\x89\x85"              , 0 }, /* ≅ */
    { "congdot",                         "\xE2\xA9\xAD"              , 0 }, /* ⩭ */
    { "conint",                          "\xE2\x88\xAE"              , 0 }, /* ∮ */
    { "copf",                            "\xF0\x9D\x95\x94"          , 0 }, /* 𝕔 */
    { "coprod",                          "\xE2\x88\x90"              , 0 }, /* ∐ */
    { "copy",                            "\xC2\xA9"                  , 0 }, /* © */
    { "copysr",                          "\xE2\x84\x97"              , 0 }, /* ℗ */
    { "crarr",                           "\xE2\x86\xB5"              , 0 }, /* ↵ */
    { "cross",                           "\xE2\x9C\x97"              , 0 }, /* ✗ */
    { "cscr",                            "\xF0\x9D\x92\xB8"          , 0 }, /* 𝒸 */
    { "csub",                            "\xE2\xAB\x8F"              , 0 }, /* ⫏ */
    { "csube",                           "\xE2\xAB\x91"              , 0 }, /* ⫑ */
    { "csup",                            "\xE2\xAB\x90"              , 0 }, /* ⫐ */
    { "csupe",                           "\xE2\xAB\x92"              , 0 }, /* ⫒ */
    { "ctdot",                           "\xE2\x8B\xAF"              , 0 }, /* ⋯ */
    { "cudarrl",                         "\xE2\xA4\xB8"              , 0 }, /* ⤸ */
    { "cudarrr",                         "\xE2\xA4\xB5"              , 0 }, /* ⤵ */
    { "cuepr",                           "\xE2\x8B\x9E"              , 0 }, /* ⋞ */
    { "cuesc",                           "\xE2\x8B\x9F"              , 0 }, /* ⋟ */
    { "cularr",                          "\xE2\x86\xB6"              , 0 }, /* ↶ */
    { "cularrp",                         "\xE2\xA4\xBD"              , 0 }, /* ⤽ */
    { "cup",                             "\xE2\x88\xAA"              , 0 }, /* ∪ */
    { "cupbrcap",                        "\xE2\xA9\x88"              , 0 }, /* ⩈ */
    { "cupcap",                          "\xE2\xA9\x86"              , 0 }, /* ⩆ */
    { "cupcup",                          "\xE2\xA9\x8A"              , 0 }, /* ⩊ */
    { "cupdot",                          "\xE2\x8A\x8D"              , 0 }, /* ⊍ */
    { "cupor",                           "\xE2\xA9\x85"              , 0 }, /* ⩅ */
    { "cups",                            "\xE2\x88\xAA\xEF\xB8\x80"  , 0 }, /* ∪︀ */
    { "curarr",                          "\xE2\x86\xB7"              , 0 }, /* ↷ */
    { "curarrm",                         "\xE2\xA4\xBC"              , 0 }, /* ⤼ */
    { "curlyeqprec",                     "\xE2\x8B\x9E"              , 0 }, /* ⋞ */
    { "curlyeqsucc",                     "\xE2\x8B\x9F"              , 0 }, /* ⋟ */
    { "curlyvee",                        "\xE2\x8B\x8E"              , 0 }, /* ⋎ */
    { "curlywedge",                      "\xE2\x8B\x8F"              , 0 }, /* ⋏ */
    { "curren",                          "\xC2\xA4"                  , 0 }, /* ¤ */
    { "curvearrowleft",                  "\xE2\x86\xB6"              , 0 }, /* ↶ */
    { "curvearrowright",                 "\xE2\x86\xB7"              , 0 }, /* ↷ */
    { "cuvee",                           "\xE2\x8B\x8E"              , 0 }, /* ⋎ */
    { "cuwed",                           "\xE2\x8B\x8F"              , 0 }, /* ⋏ */
    { "cwconint",                        "\xE2\x88\xB2"              , 0 }, /* ∲ */
    { "cwint",                           "\xE2\x88\xB1"              , 0 }, /* ∱ */
    { "cylcty",                          "\xE2\x8C\xAD"              , 0 }, /* ⌭ */
    { "dArr",                            "\xE2\x87\x93"              , 0 }, /* ⇓ */
    { "dHar",                            "\xE2\xA5\xA5"              , 0 }, /* ⥥ */
    { "dagger",                          "\xE2\x80\xA0"              , 0 }, /* † */
    { "daleth",                          "\xE2\x84\xB8"              , 0 }, /* ℸ */
    { "darr",                            "\xE2\x86\x93"              , 0 }, /* ↓ */
    { "dash",                            "\xE2\x80\x90"              , 0 }, /* ‐ */
    { "dashv",                           "\xE2\x8A\xA3"              , 0 }, /* ⊣ */
    { "dbkarow",                         "\xE2\xA4\x8F"              , 0 }, /* ⤏ */
    { "dblac",                           "\xCB\x9D"                  , 0 }, /* ˝ */
    { "dcaron",                          "\xC4\x8F"                  , 0 }, /* ď */
    { "dcy",                             "\xD0\xB4"                  , 0 }, /* д */
    { "dd",                              "\xE2\x85\x86"              , 0 }, /* ⅆ */
    { "ddagger",                         "\xE2\x80\xA1"              , 0 }, /* ‡ */
    { "ddarr",                           "\xE2\x87\x8A"              , 0 }, /* ⇊ */
    { "ddotseq",                         "\xE2\xA9\xB7"              , 0 }, /* ⩷ */
    { "deg",                             "\xC2\xB0"                  , 0 }, /* ° */
    { "delta",                           "\xCE\xB4"                  , 0 }, /* δ */
    { "demptyv",                         "\xE2\xA6\xB1"              , 0 }, /* ⦱ */
    { "dfisht",                          "\xE2\xA5\xBF"              , 0 }, /* ⥿ */
    { "dfr",                             "\xF0\x9D\x94\xA1"          , 0 }, /* 𝔡 */
    { "dharl",                           "\xE2\x87\x83"              , 0 }, /* ⇃ */
    { "dharr",                           "\xE2\x87\x82"              , 0 }, /* ⇂ */
    { "diam",                            "\xE2\x8B\x84"              , 0 }, /* ⋄ */
    { "diamond",                         "\xE2\x8B\x84"              , 0 }, /* ⋄ */
    { "diamondsuit",                     "\xE2\x99\xA6"              , 0 }, /* ♦ */
    { "diams",                           "\xE2\x99\xA6"              , 0 }, /* ♦ */
    { "die",                             "\xC2\xA8"                  , 0 }, /* ¨ */
    { "digamma",                         "\xCF\x9D"                  , 0 }, /* ϝ */
    { "disin",                           "\xE2\x8B\xB2"              , 0 }, /* ⋲ */
    { "div",                             "\xC3\xB7"                  , 0 }, /* ÷ */
    { "divide",                          "\xC3\xB7"                  , 0 }, /* ÷ */
    { "divideontimes",                   "\xE2\x8B\x87"              , 0 }, /* ⋇ */
    { "divonx",                          "\xE2\x8B\x87"              , 0 }, /* ⋇ */
    { "djcy",                            "\xD1\x92"                  , 0 }, /* ђ */
    { "dlcorn",                          "\xE2\x8C\x9E"              , 0 }, /* ⌞ */
    { "dlcrop",                          "\xE2\x8C\x8D"              , 0 }, /* ⌍ */
    { "dollar",                          "\x24"                      , 0 }, /* $ */
    { "dopf",                            "\xF0\x9D\x95\x95"          , 0 }, /* 𝕕 */
    { "dot",                             "\xCB\x99"                  , 0 }, /* ˙ */
    { "doteq",                           "\xE2\x89\x90"              , 0 }, /* ≐ */
    { "doteqdot",                        "\xE2\x89\x91"              , 0 }, /* ≑ */
    { "dotminus",                        "\xE2\x88\xB8"              , 0 }, /* ∸ */
    { "dotplus",                         "\xE2\x88\x94"              , 0 }, /* ∔ */
    { "dotsquare",                       "\xE2\x8A\xA1"              , 0 }, /* ⊡ */
    { "doublebarwedge",                  "\xE2\x8C\x86"              , 0 }, /* ⌆ */
    { "downarrow",                       "\xE2\x86\x93"              , 0 }, /* ↓ */
    { "downdownarrows",                  "\xE2\x87\x8A"              , 0 }, /* ⇊ */
    { "downharpoonleft",                 "\xE2\x87\x83"              , 0 }, /* ⇃ */
    { "downharpoonright",                "\xE2\x87\x82"              , 0 }, /* ⇂ */
    { "drbkarow",                        "\xE2\xA4\x90"              , 0 }, /* ⤐ */
    { "drcorn",                          "\xE2\x8C\x9F"              , 0 }, /* ⌟ */
    { "drcrop",                          "\xE2\x8C\x8C"              , 0 }, /* ⌌ */
    { "dscr",                            "\xF0\x9D\x92\xB9"          , 0 }, /* 𝒹 */
    { "dscy",                            "\xD1\x95"                  , 0 }, /* ѕ */
    { "dsol",                            "\xE2\xA7\xB6"              , 0 }, /* ⧶ */
    { "dstrok",                          "\xC4\x91"                  , 0 }, /* đ */
    { "dtdot",                           "\xE2\x8B\xB1"              , 0 }, /* ⋱ */
    { "dtri",                            "\xE2\x96\xBF"              , 0 }, /* ▿ */
    { "dtrif",                           "\xE2\x96\xBE"              , 0 }, /* ▾ */
    { "duarr",                           "\xE2\x87\xB5"              , 0 }, /* ⇵ */
    { "duhar",                           "\xE2\xA5\xAF"              , 0 }, /* ⥯ */
    { "dwangle",                         "\xE2\xA6\xA6"              , 0 }, /* ⦦ */
    { "dzcy",                            "\xD1\x9F"                  , 0 }, /* џ */
    { "dzigrarr",                        "\xE2\x9F\xBF"              , 0 }, /* ⟿ */
    { "eDDot",                           "\xE2\xA9\xB7"              , 0 }, /* ⩷ */
    { "eDot",                            "\xE2\x89\x91"              , 0 }, /* ≑ */
    { "eacute",                          "\xC3\xA9"                  , 0 }, /* é */
    { "easter",                          "\xE2\xA9\xAE"              , 0 }, /* ⩮ */
    { "ecaron",                          "\xC4\x9B"                  , 0 }, /* ě */
    { "ecir",                            "\xE2\x89\x96"              , 0 }, /* ≖ */
    { "ecirc",                           "\xC3\xAA"                  , 0 }, /* ê */
    { "ecolon",                          "\xE2\x89\x95"              , 0 }, /* ≕ */
    { "ecy",                             "\xD1\x8D"                  , 0 }, /* э */
    { "edot",                            "\xC4\x97"                  , 0 }, /* ė */
    { "ee",                              "\xE2\x85\x87"              , 0 }, /* ⅇ */
    { "efDot",                           "\xE2\x89\x92"              , 0 }, /* ≒ */
    { "efr",                             "\xF0\x9D\x94\xA2"          , 0 }, /* 𝔢 */
    { "eg",                              "\xE2\xAA\x9A"              , 0 }, /* ⪚ */
    { "egrave",                          "\xC3\xA8"                  , 0 }, /* è */
    { "egs",                             "\xE2\xAA\x96"              , 0 }, /* ⪖ */
    { "egsdot",                          "\xE2\xAA\x98"              , 0 }, /* ⪘ */
    { "el",                              "\xE2\xAA\x99"              , 0 }, /* ⪙ */
    { "elinters",                        "\xE2\x8F\xA7"              , 0 }, /* ⏧ */
    { "ell",                             "\xE2\x84\x93"              , 0 }, /* ℓ */
    { "els",                             "\xE2\xAA\x95"              , 0 }, /* ⪕ */
    { "elsdot",                          "\xE2\xAA\x97"              , 0 }, /* ⪗ */
    { "emacr",                           "\xC4\x93"                  , 0 }, /* ē */
    { "empty",                           "\xE2\x88\x85"              , 0 }, /* ∅ */
    { "emptyset",                        "\xE2\x88\x85"              , 0 }, /* ∅ */
    { "emptyv",                          "\xE2\x88\x85"              , 0 }, /* ∅ */
    { "emsp",                            "\xE2\x80\x83"              , 0 }, /*   */
    { "emsp13",                          "\xE2\x80\x84"              , 0 }, /*   */
    { "emsp14",                          "\xE2\x80\x85"              , 0 }, /*   */
    { "eng",                             "\xC5\x8B"                  , 0 }, /* ŋ */
    { "ensp",                            "\xE2\x80\x82"              , 0 }, /*   */
    { "eogon",                           "\xC4\x99"                  , 0 }, /* ę */
    { "eopf",                            "\xF0\x9D\x95\x96"          , 0 }, /* 𝕖 */
    { "epar",                            "\xE2\x8B\x95"              , 0 }, /* ⋕ */
    { "eparsl",                          "\xE2\xA7\xA3"              , 0 }, /* ⧣ */
    { "eplus",                           "\xE2\xA9\xB1"              , 0 }, /* ⩱ */
    { "epsi",                            "\xCE\xB5"                  , 0 }, /* ε */
    { "epsilon",                         "\xCE\xB5"                  , 0 }, /* ε */
    { "epsiv",                           "\xCF\xB5"                  , 0 }, /* ϵ */
    { "eqcirc",                          "\xE2\x89\x96"              , 0 }, /* ≖ */
    { "eqcolon",                         "\xE2\x89\x95"              , 0 }, /* ≕ */
    { "eqsim",                           "\xE2\x89\x82"              , 0 }, /* ≂ */
    { "eqslantgtr",                      "\xE2\xAA\x96"              , 0 }, /* ⪖ */
    { "eqslantless",                     "\xE2\xAA\x95"              , 0 }, /* ⪕ */
    { "equals",                          "\x3D"                      , 0 }, /* = */
    { "equest",                          "\xE2\x89\x9F"              , 0 }, /* ≟ */
    { "equiv",                           "\xE2\x89\xA1"              , 0 }, /* ≡ */
    { "equivDD",                         "\xE2\xA9\xB8"              , 0 }, /* ⩸ */
    { "eqvparsl",                        "\xE2\xA7\xA5"              , 0 }, /* ⧥ */
    { "erDot",                           "\xE2\x89\x93"              , 0 }, /* ≓ */
    { "erarr",                           "\xE2\xA5\xB1"              , 0 }, /* ⥱ */
    { "escr",                            "\xE2\x84\xAF"              , 0 }, /* ℯ */
    { "esdot",                           "\xE2\x89\x90"              , 0 }, /* ≐ */
    { "esim",                            "\xE2\x89\x82"              , 0 }, /* ≂ */
    { "eta",                             "\xCE\xB7"                  , 0 }, /* η */
    { "eth",                             "\xC3\xB0"                  , 0 }, /* ð */
    { "euml",                            "\xC3\xAB"                  , 0 }, /* ë */
    { "euro",                            "\xE2\x82\xAC"              , 0 }, /* € */
    { "excl",                            "\x21"                      , 0 }, /* ! */
    { "exist",                           "\xE2\x88\x83"              , 0 }, /* ∃ */
    { "expectation",                     "\xE2\x84\xB0"              , 0 }, /* ℰ */
    { "exponentiale",                    "\xE2\x85\x87"              , 0 }, /* ⅇ */
    { "fallingdotseq",                   "\xE2\x89\x92"              , 0 }, /* ≒ */
    { "fcy",                             "\xD1\x84"                  , 0 }, /* ф */
    { "female",                          "\xE2\x99\x80"              , 0 }, /* ♀ */
    { "ffilig",                          "\xEF\xAC\x83"              , 0 }, /* ﬃ */
    { "fflig",                           "\xEF\xAC\x80"              , 0 }, /* ﬀ */
    { "ffllig",                          "\xEF\xAC\x84"              , 0 }, /* ﬄ */
    { "ffr",                             "\xF0\x9D\x94\xA3"          , 0 }, /* 𝔣 */
    { "filig",                           "\xEF\xAC\x81"              , 0 }, /* ﬁ */
    { "fjlig",                           "\x66\x6A"                  , 0 }, /* fj */
    { "flat",                            "\xE2\x99\xAD"              , 0 }, /* ♭ */
    { "fllig",                           "\xEF\xAC\x82"              , 0 }, /* ﬂ */
    { "fltns",                           "\xE2\x96\xB1"              , 0 }, /* ▱ */
    { "fnof",                            "\xC6\x92"                  , 0 }, /* ƒ */
    { "fopf",                            "\xF0\x9D\x95\x97"          , 0 }, /* 𝕗 */
    { "forall",                          "\xE2\x88\x80"              , 0 }, /* ∀ */
    { "fork",                            "\xE2\x8B\x94"              , 0 }, /* ⋔ */
    { "forkv",                           "\xE2\xAB\x99"              , 0 }, /* ⫙ */
    { "fpartint",                        "\xE2\xA8\x8D"              , 0 }, /* ⨍ */
    { "frac12",                          "\xC2\xBD"                  , 0 }, /* ½ */
    { "frac13",                          "\xE2\x85\x93"              , 0 }, /* ⅓ */
    { "frac14",                          "\xC2\xBC"                  , 0 }, /* ¼ */
    { "frac15",                          "\xE2\x85\x95"              , 0 }, /* ⅕ */
    { "frac16",                          "\xE2\x85\x99"              , 0 }, /* ⅙ */
    { "frac18",                          "\xE2\x85\x9B"              , 0 }, /* ⅛ */
    { "frac23",                          "\xE2\x85\x94"              , 0 }, /* ⅔ */
    { "frac25",                          "\xE2\x85\x96"              , 0 }, /* ⅖ */
    { "frac34",                          "\xC2\xBE"                  , 0 }, /* ¾ */
    { "frac35",                          "\xE2\x85\x97"              , 0 }, /* ⅗ */
    { "frac38",                          "\xE2\x85\x9C"              , 0 }, /* ⅜ */
    { "frac45",                          "\xE2\x85\x98"              , 0 }, /* ⅘ */
    { "frac56",                          "\xE2\x85\x9A"              , 0 }, /* ⅚ */
    { "frac58",                          "\xE2\x85\x9D"              , 0 }, /* ⅝ */
    { "frac78",                          "\xE2\x85\x9E"              , 0 }, /* ⅞ */
    { "frasl",                           "\xE2\x81\x84"              , 0 }, /* ⁄ */
    { "frown",                           "\xE2\x8C\xA2"              , 0 }, /* ⌢ */
    { "fscr",                            "\xF0\x9D\x92\xBB"          , 0 }, /* 𝒻 */
    { "gE",                              "\xE2\x89\xA7"              , 0 }, /* ≧ */
    { "gEl",                             "\xE2\xAA\x8C"              , 0 }, /* ⪌ */
    { "gacute",                          "\xC7\xB5"                  , 0 }, /* ǵ */
    { "gamma",                           "\xCE\xB3"                  , 0 }, /* γ */
    { "gammad",                          "\xCF\x9D"                  , 0 }, /* ϝ */
    { "gap",                             "\xE2\xAA\x86"              , 0 }, /* ⪆ */
    { "gbreve",                          "\xC4\x9F"                  , 0 }, /* ğ */
    { "gcirc",                           "\xC4\x9D"                  , 0 }, /* ĝ */
    { "gcy",                             "\xD0\xB3"                  , 0 }, /* г */
    { "gdot",                            "\xC4\xA1"                  , 0 }, /* ġ */
    { "ge",                              "\xE2\x89\xA5"              , 0 }, /* ≥ */
    { "gel",                             "\xE2\x8B\x9B"              , 0 }, /* ⋛ */
    { "geq",                             "\xE2\x89\xA5"              , 0 }, /* ≥ */
    { "geqq",                            "\xE2\x89\xA7"              , 0 }, /* ≧ */
    { "geqslant",                        "\xE2\xA9\xBE"              , 0 }, /* ⩾ */
    { "ges",                             "\xE2\xA9\xBE"              , 0 }, /* ⩾ */
    { "gescc",                           "\xE2\xAA\xA9"              , 0 }, /* ⪩ */
    { "gesdot",                          "\xE2\xAA\x80"              , 0 }, /* ⪀ */
    { "gesdoto",                         "\xE2\xAA\x82"              , 0 }, /* ⪂ */
    { "gesdotol",                        "\xE2\xAA\x84"              , 0 }, /* ⪄ */
    { "gesl",                            "\xE2\x8B\x9B\xEF\xB8\x80"  , 0 }, /* ⋛︀ */
    { "gesles",                          "\xE2\xAA\x94"              , 0 }, /* ⪔ */
    { "gfr",                             "\xF0\x9D\x94\xA4"          , 0 }, /* 𝔤 */
    { "gg",                              "\xE2\x89\xAB"              , 0 }, /* ≫ */
    { "ggg",                             "\xE2\x8B\x99"              , 0 }, /* ⋙ */
    { "gimel",                           "\xE2\x84\xB7"              , 0 }, /* ℷ */
    { "gjcy",                            "\xD1\x93"                  , 0 }, /* ѓ */
    { "gl",                              "\xE2\x89\xB7"              , 0 }, /* ≷ */
    { "glE",                             "\xE2\xAA\x92"              , 0 }, /* ⪒ */
    { "gla",                             "\xE2\xAA\xA5"              , 0 }, /* ⪥ */
    { "glj",                             "\xE2\xAA\xA4"              , 0 }, /* ⪤ */
    { "gnE",                             "\xE2\x89\xA9"              , 0 }, /* ≩ */
    { "gnap",                            "\xE2\xAA\x8A"              , 0 }, /* ⪊ */
    { "gnapprox",                        "\xE2\xAA\x8A"              , 0 }, /* ⪊ */
    { "gne",                             "\xE2\xAA\x88"              , 0 }, /* ⪈ */
    { "gneq",                            "\xE2\xAA\x88"              , 0 }, /* ⪈ */
    { "gneqq",                           "\xE2\x89\xA9"              , 0 }, /* ≩ */
    { "gnsim",                           "\xE2\x8B\xA7"              , 0 }, /* ⋧ */
    { "gopf",                            "\xF0\x9D\x95\x98"          , 0 }, /* 𝕘 */
    { "grave",                           "\x60"                      , 0 }, /* ` */
    { "gscr",                            "\xE2\x84\x8A"              , 0 }, /* ℊ */
    { "gsim",                            "\xE2\x89\xB3"              , 0 }, /* ≳ */
    { "gsime",                           "\xE2\xAA\x8E"              , 0 }, /* ⪎ */
    { "gsiml",                           "\xE2\xAA\x90"              , 0 }, /* ⪐ */
    { "gt",                              "\x3E"                      , 0 }, /* > */
    { "gtcc",                            "\xE2\xAA\xA7"              , 0 }, /* ⪧ */
    { "gtcir",                           "\xE2\xA9\xBA"              , 0 }, /* ⩺ */
    { "gtdot",                           "\xE2\x8B\x97"              , 0 }, /* ⋗ */
    { "gtlPar",                          "\xE2\xA6\x95"              , 0 }, /* ⦕ */
    { "gtquest",                         "\xE2\xA9\xBC"              , 0 }, /* ⩼ */
    { "gtrapprox",                       "\xE2\xAA\x86"              , 0 }, /* ⪆ */
    { "gtrarr",                          "\xE2\xA5\xB8"              , 0 }, /* ⥸ */
    { "gtrdot",                          "\xE2\x8B\x97"              , 0 }, /* ⋗ */
    { "gtreqless",                       "\xE2\x8B\x9B"              , 0 }, /* ⋛ */
    { "gtreqqless",                      "\xE2\xAA\x8C"              , 0 }, /* ⪌ */
    { "gtrless",                         "\xE2\x89\xB7"              , 0 }, /* ≷ */
    { "gtrsim",                          "\xE2\x89\xB3"              , 0 }, /* ≳ */
    { "gvertneqq",                       "\xE2\x89\xA9\xEF\xB8\x80"  , 0 }, /* ≩︀ */
    { "gvnE",                            "\xE2\x89\xA9\xEF\xB8\x80"  , 0 }, /* ≩︀ */
    { "hArr",                            "\xE2\x87\x94"              , 0 }, /* ⇔ */
    { "hairsp",                          "\xE2\x80\x8A"              , 0 }, /*   */
    { "half",                            "\xC2\xBD"                  , 0 }, /* ½ */
    { "hamilt",                          "\xE2\x84\x8B"              , 0 }, /* ℋ */
    { "hardcy",                          "\xD1\x8A"                  , 0 }, /* ъ */
    { "harr",                            "\xE2\x86\x94"              , 0 }, /* ↔ */
    { "harrcir",                         "\xE2\xA5\x88"              , 0 }, /* ⥈ */
    { "harrw",                           "\xE2\x86\xAD"              , 0 }, /* ↭ */
    { "hbar",                            "\xE2\x84\x8F"              , 0 }, /* ℏ */
    { "hcirc",                           "\xC4\xA5"                  , 0 }, /* ĥ */
    { "hearts",                          "\xE2\x99\xA5"              , 0 }, /* ♥ */
    { "heartsuit",                       "\xE2\x99\xA5"              , 0 }, /* ♥ */
    { "hellip",                          "\xE2\x80\xA6"              , 0 }, /* … */
    { "hercon",                          "\xE2\x8A\xB9"              , 0 }, /* ⊹ */
    { "hfr",                             "\xF0\x9D\x94\xA5"          , 0 }, /* 𝔥 */
    { "hksearow",                        "\xE2\xA4\xA5"              , 0 }, /* ⤥ */
    { "hkswarow",                        "\xE2\xA4\xA6"              , 0 }, /* ⤦ */
    { "hoarr",                           "\xE2\x87\xBF"              , 0 }, /* ⇿ */
    { "homtht",                          "\xE2\x88\xBB"              , 0 }, /* ∻ */
    { "hookleftarrow",                   "\xE2\x86\xA9"              , 0 }, /* ↩ */
    { "hookrightarrow",                  "\xE2\x86\xAA"              , 0 }, /* ↪ */
    { "hopf",                            "\xF0\x9D\x95\x99"          , 0 }, /* 𝕙 */
    { "horbar",                          "\xE2\x80\x95"              , 0 }, /* ― */
    { "hscr",                            "\xF0\x9D\x92\xBD"          , 0 }, /* 𝒽 */
    { "hslash",                          "\xE2\x84\x8F"              , 0 }, /* ℏ */
    { "hstrok",                          "\xC4\xA7"                  , 0 }, /* ħ */
    { "hybull",                          "\xE2\x81\x83"              , 0 }, /* ⁃ */
    { "hyphen",                          "\xE2\x80\x90"              , 0 }, /* ‐ */
    { "iacute",                          "\xC3\xAD"                  , 0 }, /* í */
    { "ic",                              "\xE2\x81\xA3"              , 0 }, /* ⁣ */
    { "icirc",                           "\xC3\xAE"                  , 0 }, /* î */
    { "icy",                             "\xD0\xB8"                  , 0 }, /* и */
    { "iecy",                            "\xD0\xB5"                  , 0 }, /* е */
    { "iexcl",                           "\xC2\xA1"                  , 0 }, /* ¡ */
    { "iff",                             "\xE2\x87\x94"              , 0 }, /* ⇔ */
    { "ifr",                             "\xF0\x9D\x94\xA6"          , 0 }, /* 𝔦 */
    { "igrave",                          "\xC3\xAC"                  , 0 }, /* ì */
    { "ii",                              "\xE2\x85\x88"              , 0 }, /* ⅈ */
    { "iiiint",                          "\xE2\xA8\x8C"              , 0 }, /* ⨌ */
    { "iiint",                           "\xE2\x88\xAD"              , 0 }, /* ∭ */
    { "iinfin",                          "\xE2\xA7\x9C"              , 0 }, /* ⧜ */
    { "iiota",                           "\xE2\x84\xA9"              , 0 }, /* ℩ */
    { "ijlig",                           "\xC4\xB3"                  , 0 }, /* ĳ */
    { "imacr",                           "\xC4\xAB"                  , 0 }, /* ī */
    { "image",                           "\xE2\x84\x91"              , 0 }, /* ℑ */
    { "imagline",                        "\xE2\x84\x90"              , 0 }, /* ℐ */
    { "imagpart",                        "\xE2\x84\x91"              , 0 }, /* ℑ */
    { "imath",                           "\xC4\xB1"                  , 0 }, /* ı */
    { "imof",                            "\xE2\x8A\xB7"              , 0 }, /* ⊷ */
    { "imped",                           "\xC6\xB5"                  , 0 }, /* Ƶ */
    { "in",                              "\xE2\x88\x88"              , 0 }, /* ∈ */
    { "incare",                          "\xE2\x84\x85"              , 0 }, /* ℅ */
    { "infin",                           "\xE2\x88\x9E"              , 0 }, /* ∞ */
    { "infintie",                        "\xE2\xA7\x9D"              , 0 }, /* ⧝ */
    { "inodot",                          "\xC4\xB1"                  , 0 }, /* ı */
    { "int",                             "\xE2\x88\xAB"              , 0 }, /* ∫ */
    { "intcal",                          "\xE2\x8A\xBA"              , 0 }, /* ⊺ */
    { "integers",                        "\xE2\x84\xA4"              , 0 }, /* ℤ */
    { "intercal",                        "\xE2\x8A\xBA"              , 0 }, /* ⊺ */
    { "intlarhk",                        "\xE2\xA8\x97"              , 0 }, /* ⨗ */
    { "intprod",                         "\xE2\xA8\xBC"              , 0 }, /* ⨼ */
    { "iocy",                            "\xD1\x91"                  , 0 }, /* ё */
    { "iogon",                           "\xC4\xAF"                  , 0 }, /* į */
    { "iopf",                            "\xF0\x9D\x95\x9A"          , 0 }, /* 𝕚 */
    { "iota",                            "\xCE\xB9"                  , 0 }, /* ι */
    { "iprod",                           "\xE2\xA8\xBC"              , 0 }, /* ⨼ */
    { "iquest",                          "\xC2\xBF"                  , 0 }, /* ¿ */
    { "iscr",                            "\xF0\x9D\x92\xBE"          , 0 }, /* 𝒾 */
    { "isin",                            "\xE2\x88\x88"              , 0 }, /* ∈ */
    { "isinE",                           "\xE2\x8B\xB9"              , 0 }, /* ⋹ */
    { "isindot",                         "\xE2\x8B\xB5"              , 0 }, /* ⋵ */
    { "isins",                           "\xE2\x8B\xB4"              , 0 }, /* ⋴ */
    { "isinsv",                          "\xE2\x8B\xB3"              , 0 }, /* ⋳ */
    { "isinv",                           "\xE2\x88\x88"              , 0 }, /* ∈ */
    { "it",                              "\xE2\x81\xA2"              , 0 }, /* ⁢ */
    { "itilde",                          "\xC4\xA9"                  , 0 }, /* ĩ */
    { "iukcy",                           "\xD1\x96"                  , 0 }, /* і */
    { "iuml",                            "\xC3\xAF"                  , 0 }, /* ï */
    { "jcirc",                           "\xC4\xB5"                  , 0 }, /* ĵ */
    { "jcy",                             "\xD0\xB9"                  , 0 }, /* й */
    { "jfr",                             "\xF0\x9D\x94\xA7"          , 0 }, /* 𝔧 */
    { "jmath",                           "\xC8\xB7"                  , 0 }, /* ȷ */
    { "jopf",                            "\xF0\x9D\x95\x9B"          , 0 }, /* 𝕛 */
    { "jscr",                            "\xF0\x9D\x92\xBF"          , 0 }, /* 𝒿 */
    { "jsercy",                          "\xD1\x98"                  , 0 }, /* ј */
    { "jukcy",                           "\xD1\x94"                  , 0 }, /* є */
    { "kappa",                           "\xCE\xBA"                  , 0 }, /* κ */
    { "kappav",                          "\xCF\xB0"                  , 0 }, /* ϰ */
    { "kcedil",                          "\xC4\xB7"                  , 0 }, /* ķ */
    { "kcy",                             "\xD0\xBA"                  , 0 }, /* к */
    { "kfr",                             "\xF0\x9D\x94\xA8"          , 0 }, /* 𝔨 */
    { "kgreen",                          "\xC4\xB8"                  , 0 }, /* ĸ */
    { "khcy",                            "\xD1\x85"                  , 0 }, /* х */
    { "kjcy",                            "\xD1\x9C"                  , 0 }, /* ќ */
    { "kopf",                            "\xF0\x9D\x95\x9C"          , 0 }, /* 𝕜 */
    { "kscr",                            "\xF0\x9D\x93\x80"          , 0 }, /* 𝓀 */
    { "lAarr",                           "\xE2\x87\x9A"              , 0 }, /* ⇚ */
    { "lArr",                            "\xE2\x87\x90"              , 0 }, /* ⇐ */
    { "lAtail",                          "\xE2\xA4\x9B"              , 0 }, /* ⤛ */
    { "lBarr",                           "\xE2\xA4\x8E"              , 0 }, /* ⤎ */
    { "lE",                              "\xE2\x89\xA6"              , 0 }, /* ≦ */
    { "lEg",                             "\xE2\xAA\x8B"              , 0 }, /* ⪋ */
    { "lHar",                            "\xE2\xA5\xA2"              , 0 }, /* ⥢ */
    { "lacute",                          "\xC4\xBA"                  , 0 }, /* ĺ */
    { "laemptyv",                        "\xE2\xA6\xB4"              , 0 }, /* ⦴ */
    { "lagran",                          "\xE2\x84\x92"              , 0 }, /* ℒ */
    { "lambda",                          "\xCE\xBB"                  , 0 }, /* λ */
    { "lang",                            "\xE2\x9F\xA8"              , 0 }, /* ⟨ */
    { "langd",                           "\xE2\xA6\x91"              , 0 }, /* ⦑ */
    { "langle",                          "\xE2\x9F\xA8"              , 0 }, /* ⟨ */
    { "lap",                             "\xE2\xAA\x85"              , 0 }, /* ⪅ */
    { "laquo",                           "\xC2\xAB"                  , 0 }, /* « */
    { "larr",                            "\xE2\x86\x90"              , 0 }, /* ← */
    { "larrb",                           "\xE2\x87\xA4"              , 0 }, /* ⇤ */
    { "larrbfs",                         "\xE2\xA4\x9F"              , 0 }, /* ⤟ */
    { "larrfs",                          "\xE2\xA4\x9D"              , 0 }, /* ⤝ */
    { "larrhk",                          "\xE2\x86\xA9"              , 0 }, /* ↩ */
    { "larrlp",                          "\xE2\x86\xAB"              , 0 }, /* ↫ */
    { "larrpl",                          "\xE2\xA4\xB9"              , 0 }, /* ⤹ */
    { "larrsim",                         "\xE2\xA5\xB3"              , 0 }, /* ⥳ */
    { "larrtl",                          "\xE2\x86\xA2"              , 0 }, /* ↢ */
    { "lat",                             "\xE2\xAA\xAB"              , 0 }, /* ⪫ */
    { "latail",                          "\xE2\xA4\x99"              , 0 }, /* ⤙ */
    { "late",                            "\xE2\xAA\xAD"              , 0 }, /* ⪭ */
    { "lates",                           "\xE2\xAA\xAD\xEF\xB8\x80"  , 0 }, /* ⪭︀ */
    { "lbarr",                           "\xE2\xA4\x8C"              , 0 }, /* ⤌ */
    { "lbbrk",                           "\xE2\x9D\xB2"              , 0 }, /* ❲ */
    { "lbrace",                          "\x7B"                      , 0 }, /* { */
    { "lbrack",                          "\x5B"                      , 0 }, /* [ */
    { "lbrke",                           "\xE2\xA6\x8B"              , 0 }, /* ⦋ */
    { "lbrksld",                         "\xE2\xA6\x8F"              , 0 }, /* ⦏ */
    { "lbrkslu",                         "\xE2\xA6\x8D"              , 0 }, /* ⦍ */
    { "lcaron",                          "\xC4\xBE"                  , 0 }, /* ľ */
    { "lcedil",                          "\xC4\xBC"                  , 0 }, /* ļ */
    { "lceil",                           "\xE2\x8C\x88"              , 0 }, /* ⌈ */
    { "lcub",                            "\x7B"                      , 0 }, /* { */
    { "lcy",                             "\xD0\xBB"                  , 0 }, /* л */
    { "ldca",                            "\xE2\xA4\xB6"              , 0 }, /* ⤶ */
    { "ldquo",                           "\xE2\x80\x9C"              , 0 }, /* “ */
    { "ldquor",                          "\xE2\x80\x9E"              , 0 }, /* „ */
    { "ldrdhar",                         "\xE2\xA5\xA7"              , 0 }, /* ⥧ */
    { "ldrushar",                        "\xE2\xA5\x8B"              , 0 }, /* ⥋ */
    { "ldsh",                            "\xE2\x86\xB2"              , 0 }, /* ↲ */
    { "le",                              "\xE2\x89\xA4"              , 0 }, /* ≤ */
    { "leftarrow",                       "\xE2\x86\x90"              , 0 }, /* ← */
    { "leftarrowtail",                   "\xE2\x86\xA2"              , 0 }, /* ↢ */
    { "leftharpoondown",                 "\xE2\x86\xBD"              , 0 }, /* ↽ */
    { "leftharpoonup",                   "\xE2\x86\xBC"              , 0 }, /* ↼ */
    { "leftleftarrows",                  "\xE2\x87\x87"              , 0 }, /* ⇇ */
    { "leftrightarrow",                  "\xE2\x86\x94"              , 0 }, /* ↔ */
    { "leftrightarrows",                 "\xE2\x87\x86"              , 0 }, /* ⇆ */
    { "leftrightharpoons",               "\xE2\x87\x8B"              , 0 }, /* ⇋ */
    { "leftrightsquigarrow",             "\xE2\x86\xAD"              , 0 }, /* ↭ */
    { "leftthreetimes",                  "\xE2\x8B\x8B"              , 0 }, /* ⋋ */
    { "leg",                             "\xE2\x8B\x9A"              , 0 }, /* ⋚ */
    { "leq",                             "\xE2\x89\xA4"              , 0 }, /* ≤ */
    { "leqq",                            "\xE2\x89\xA6"              , 0 }, /* ≦ */
    { "leqslant",                        "\xE2\xA9\xBD"              , 0 }, /* ⩽ */
    { "les",                             "\xE2\xA9\xBD"              , 0 }, /* ⩽ */
    { "lescc",                           "\xE2\xAA\xA8"              , 0 }, /* ⪨ */
    { "lesdot",                          "\xE2\xA9\xBF"              , 0 }, /* ⩿ */
    { "lesdoto",                         "\xE2\xAA\x81"              , 0 }, /* ⪁ */
    { "lesdotor",                        "\xE2\xAA\x83"              , 0 }, /* ⪃ */
    { "lesg",                            "\xE2\x8B\x9A\xEF\xB8\x80"  , 0 }, /* ⋚︀ */
    { "lesges",                          "\xE2\xAA\x93"              , 0 }, /* ⪓ */
    { "lessapprox",                      "\xE2\xAA\x85"              , 0 }, /* ⪅ */
    { "lessdot",                         "\xE2\x8B\x96"              , 0 }, /* ⋖ */
    { "lesseqgtr",                       "\xE2\x8B\x9A"              , 0 }, /* ⋚ */
    { "lesseqqgtr",                      "\xE2\xAA\x8B"              , 0 }, /* ⪋ */
    { "lessgtr",                         "\xE2\x89\xB6"              , 0 }, /* ≶ */
    { "lesssim",                         "\xE2\x89\xB2"              , 0 }, /* ≲ */
    { "lfisht",                          "\xE2\xA5\xBC"              , 0 }, /* ⥼ */
    { "lfloor",                          "\xE2\x8C\x8A"              , 0 }, /* ⌊ */
    { "lfr",                             "\xF0\x9D\x94\xA9"          , 0 }, /* 𝔩 */
    { "lg",                              "\xE2\x89\xB6"              , 0 }, /* ≶ */
    { "lgE",                             "\xE2\xAA\x91"              , 0 }, /* ⪑ */
    { "lhard",                           "\xE2\x86\xBD"              , 0 }, /* ↽ */
    { "lharu",                           "\xE2\x86\xBC"              , 0 }, /* ↼ */
    { "lharul",                          "\xE2\xA5\xAA"              , 0 }, /* ⥪ */
    { "lhblk",                           "\xE2\x96\x84"              , 0 }, /* ▄ */
    { "ljcy",                            "\xD1\x99"                  , 0 }, /* љ */
    { "ll",                              "\xE2\x89\xAA"              , 0 }, /* ≪ */
    { "llarr",                           "\xE2\x87\x87"              , 0 }, /* ⇇ */
    { "llcorner",                        "\xE2\x8C\x9E"              , 0 }, /* ⌞ */
    { "llhard",                          "\xE2\xA5\xAB"              , 0 }, /* ⥫ */
    { "lltri",                           "\xE2\x97\xBA"              , 0 }, /* ◺ */
    { "lmidot",                          "\xC5\x80"                  , 0 }, /* ŀ */
    { "lmoust",                          "\xE2\x8E\xB0"              , 0 }, /* ⎰ */
    { "lmoustache",                      "\xE2\x8E\xB0"              , 0 }, /* ⎰ */
    { "lnE",                             "\xE2\x89\xA8"              , 0 }, /* ≨ */
    { "lnap",                            "\xE2\xAA\x89"              , 0 }, /* ⪉ */
    { "lnapprox",                        "\xE2\xAA\x89"              , 0 }, /* ⪉ */
    { "lne",                             "\xE2\xAA\x87"              , 0 }, /* ⪇ */
    { "lneq",                            "\xE2\xAA\x87"              , 0 }, /* ⪇ */
    { "lneqq",                           "\xE2\x89\xA8"              , 0 }, /* ≨ */
    { "lnsim",                           "\xE2\x8B\xA6"              , 0 }, /* ⋦ */
    { "loang",                           "\xE2\x9F\xAC"              , 0 }, /* ⟬ */
    { "loarr",                           "\xE2\x87\xBD"              , 0 }, /* ⇽ */
    { "lobrk",                           "\xE2\x9F\xA6"              , 0 }, /* ⟦ */
    { "longleftarrow",                   "\xE2\x9F\xB5"              , 0 }, /* ⟵ */
    { "longleftrightarrow",              "\xE2\x9F\xB7"              , 0 }, /* ⟷ */
    { "longmapsto",                      "\xE2\x9F\xBC"              , 0 }, /* ⟼ */
    { "longrightarrow",                  "\xE2\x9F\xB6"              , 0 }, /* ⟶ */
    { "looparrowleft",                   "\xE2\x86\xAB"              , 0 }, /* ↫ */
    { "looparrowright",                  "\xE2\x86\xAC"              , 0 }, /* ↬ */
    { "lopar",                           "\xE2\xA6\x85"              , 0 }, /* ⦅ */
    { "lopf",                            "\xF0\x9D\x95\x9D"          , 0 }, /* 𝕝 */
    { "loplus",                          "\xE2\xA8\xAD"              , 0 }, /* ⨭ */
    { "lotimes",                         "\xE2\xA8\xB4"              , 0 }, /* ⨴ */
    { "lowast",                          "\xE2\x88\x97"              , 0 }, /* ∗ */
    { "lowbar",                          "\x5F"                      , 0 }, /* _ */
    { "loz",                             "\xE2\x97\x8A"              , 0 }, /* ◊ */
    { "lozenge",                         "\xE2\x97\x8A"              , 0 }, /* ◊ */
    { "lozf",                            "\xE2\xA7\xAB"              , 0 }, /* ⧫ */
    { "lpar",                            "\x28"                      , 0 }, /* ( */
    { "lparlt",                          "\xE2\xA6\x93"              , 0 }, /* ⦓ */
    { "lrarr",                           "\xE2\x87\x86"              , 0 }, /* ⇆ */
    { "lrcorner",                        "\xE2\x8C\x9F"              , 0 }, /* ⌟ */
    { "lrhar",                           "\xE2\x87\x8B"              , 0 }, /* ⇋ */
    { "lrhard",                          "\xE2\xA5\xAD"              , 0 }, /* ⥭ */
    { "lrm",                             "\xE2\x80\x8E"              , 0 }, /* ‎ */
    { "lrtri",                           "\xE2\x8A\xBF"              , 0 }, /* ⊿ */
    { "lsaquo",                          "\xE2\x80\xB9"              , 0 }, /* ‹ */
    { "lscr",                            "\xF0\x9D\x93\x81"          , 0 }, /* 𝓁 */
    { "lsh",                             "\xE2\x86\xB0"              , 0 }, /* ↰ */
    { "lsim",                            "\xE2\x89\xB2"              , 0 }, /* ≲ */
    { "lsime",                           "\xE2\xAA\x8D"              , 0 }, /* ⪍ */
    { "lsimg",                           "\xE2\xAA\x8F"              , 0 }, /* ⪏ */
    { "lsqb",                            "\x5B"                      , 0 }, /* [ */
    { "lsquo",                           "\xE2\x80\x98"              , 0 }, /* ‘ */
    { "lsquor",                          "\xE2\x80\x9A"              , 0 }, /* ‚ */
    { "lstrok",                          "\xC5\x82"                  , 0 }, /* ł */
    { "lt",                              "\x3C"                      , 0 }, /* < */
    { "ltcc",                            "\xE2\xAA\xA6"              , 0 }, /* ⪦ */
    { "ltcir",                           "\xE2\xA9\xB9"              , 0 }, /* ⩹ */
    { "ltdot",                           "\xE2\x8B\x96"              , 0 }, /* ⋖ */
    { "lthree",                          "\xE2\x8B\x8B"              , 0 }, /* ⋋ */
    { "ltimes",                          "\xE2\x8B\x89"              , 0 }, /* ⋉ */
    { "ltlarr",                          "\xE2\xA5\xB6"              , 0 }, /* ⥶ */
    { "ltquest",                         "\xE2\xA9\xBB"              , 0 }, /* ⩻ */
    { "ltrPar",                          "\xE2\xA6\x96"              , 0 }, /* ⦖ */
    { "ltri",                            "\xE2\x97\x83"              , 0 }, /* ◃ */
    { "ltrie",                           "\xE2\x8A\xB4"              , 0 }, /* ⊴ */
    { "ltrif",                           "\xE2\x97\x82"              , 0 }, /* ◂ */
    { "lurdshar",                        "\xE2\xA5\x8A"              , 0 }, /* ⥊ */
    { "luruhar",                         "\xE2\xA5\xA6"              , 0 }, /* ⥦ */
    { "lvertneqq",                       "\xE2\x89\xA8\xEF\xB8\x80"  , 0 }, /* ≨︀ */
    { "lvnE",                            "\xE2\x89\xA8\xEF\xB8\x80"  , 0 }, /* ≨︀ */
    { "mDDot",                           "\xE2\x88\xBA"              , 0 }, /* ∺ */
    { "macr",                            "\xC2\xAF"                  , 0 }, /* ¯ */
    { "male",                            "\xE2\x99\x82"              , 0 }, /* ♂ */
    { "malt",                            "\xE2\x9C\xA0"              , 0 }, /* ✠ */
    { "maltese",                         "\xE2\x9C\xA0"              , 0 }, /* ✠ */
    { "map",                             "\xE2\x86\xA6"              , 0 }, /* ↦ */
    { "mapsto",                          "\xE2\x86\xA6"              , 0 }, /* ↦ */
    { "mapstodown",                      "\xE2\x86\xA7"              , 0 }, /* ↧ */
    { "mapstoleft",                      "\xE2\x86\xA4"              , 0 }, /* ↤ */
    { "mapstoup",                        "\xE2\x86\xA5"              , 0 }, /* ↥ */
    { "marker",                          "\xE2\x96\xAE"              , 0 }, /* ▮ */
    { "mcomma",                          "\xE2\xA8\xA9"              , 0 }, /* ⨩ */
    { "mcy",                             "\xD0\xBC"                  , 0 }, /* м */
    { "mdash",                           "\xE2\x80\x94"              , 0 }, /* — */
    { "measuredangle",                   "\xE2\x88\xA1"              , 0 }, /* ∡ */
    { "mfr",                             "\xF0\x9D\x94\xAA"          , 0 }, /* 𝔪 */
    { "mho",                             "\xE2\x84\xA7"              , 0 }, /* ℧ */
    { "micro",                           "\xC2\xB5"                  , 0 }, /* µ */
    { "mid",                             "\xE2\x88\xA3"              , 0 }, /* ∣ */
    { "midast",                          "\x2A"                      , 0 }, /* * */
    { "midcir",                          "\xE2\xAB\xB0"              , 0 }, /* ⫰ */
    { "middot",                          "\xC2\xB7"                  , 0 }, /* · */
    { "minus",                           "\xE2\x88\x92"              , 0 }, /* − */
    { "minusb",                          "\xE2\x8A\x9F"              , 0 }, /* ⊟ */
    { "minusd",                          "\xE2\x88\xB8"              , 0 }, /* ∸ */
    { "minusdu",                         "\xE2\xA8\xAA"              , 0 }, /* ⨪ */
    { "mlcp",                            "\xE2\xAB\x9B"              , 0 }, /* ⫛ */
    { "mldr",                            "\xE2\x80\xA6"              , 0 }, /* … */
    { "mnplus",                          "\xE2\x88\x93"              , 0 }, /* ∓ */
    { "models",                          "\xE2\x8A\xA7"              , 0 }, /* ⊧ */
    { "mopf",                            "\xF0\x9D\x95\x9E"          , 0 }, /* 𝕞 */
    { "mp",                              "\xE2\x88\x93"              , 0 }, /* ∓ */
    { "mscr",                            "\xF0\x9D\x93\x82"          , 0 }, /* 𝓂 */
    { "mstpos",                          "\xE2\x88\xBE"              , 0 }, /* ∾ */
    { "mu",                              "\xCE\xBC"                  , 0 }, /* μ */
    { "multimap",                        "\xE2\x8A\xB8"              , 0 }, /* ⊸ */
    { "mumap",                           "\xE2\x8A\xB8"              , 0 }, /* ⊸ */
    { "nGg",                             "\xE2\x8B\x99\xCC\xB8"      , 0 }, /* ⋙̸ */
    { "nGtv",                            "\xE2\x89\xAB\xCC\xB8"      , 0 }, /* ≫̸ */
    { "nLeftarrow",                      "\xE2\x87\x8D"              , 0 }, /* ⇍ */
    { "nLeftrightarrow",                 "\xE2\x87\x8E"              , 0 }, /* ⇎ */
    { "nLl",                             "\xE2\x8B\x98\xCC\xB8"      , 0 }, /* ⋘̸ */
    { "nLtv",                            "\xE2\x89\xAA\xCC\xB8"      , 0 }, /* ≪̸ */
    { "nRightarrow",                     "\xE2\x87\x8F"              , 0 }, /* ⇏ */
    { "nVDash",                          "\xE2\x8A\xAF"              , 0 }, /* ⊯ */
    { "nVdash",                          "\xE2\x8A\xAE"              , 0 }, /* ⊮ */
    { "nabla",                           "\xE2\x88\x87"              , 0 }, /* ∇ */
    { "nacute",                          "\xC5\x84"                  , 0 }, /* ń */
    { "nang",                            "\xE2\x88\xA0\xE2\x83\x92"  , 0 }, /* ∠⃒ */
    { "nap",                             "\xE2\x89\x89"              , 0 }, /* ≉ */
    { "napE",                            "\xE2\xA9\xB0\xCC\xB8"      , 0 }, /* ⩰̸ */
    { "napid",                           "\xE2\x89\x8B\xCC\xB8"      , 0 }, /* ≋̸ */
    { "napos",                           "\xC5\x89"                  , 0 }, /* ŉ */
    { "napprox",                         "\xE2\x89\x89"              , 0 }, /* ≉ */
    { "natur",                           "\xE2\x99\xAE"              , 0 }, /* ♮ */
    { "natural",                         "\xE2\x99\xAE"              , 0 }, /* ♮ */
    { "naturals",                        "\xE2\x84\x95"              , 0 }, /* ℕ */
    { "nbsp",                            "\xC2\xA0"                  , 0 }, /*   */
    { "nbump",                           "\xE2\x89\x8E\xCC\xB8"      , 0 }, /* ≎̸ */
    { "nbumpe",                          "\xE2\x89\x8F\xCC\xB8"      , 0 }, /* ≏̸ */
    { "ncap",                            "\xE2\xA9\x83"              , 0 }, /* ⩃ */
    { "ncaron",                          "\xC5\x88"                  , 0 }, /* ň */
    { "ncedil",                          "\xC5\x86"                  , 0 }, /* ņ */
    { "ncong",                           "\xE2\x89\x87"              , 0 }, /* ≇ */
    { "ncongdot",                        "\xE2\xA9\xAD\xCC\xB8"      , 0 }, /* ⩭̸ */
    { "ncup",                            "\xE2\xA9\x82"              , 0 }, /* ⩂ */
    { "ncy",                             "\xD0\xBD"                  , 0 }, /* н */
    { "ndash",                           "\xE2\x80\x93"              , 0 }, /* – */
    { "ne",                              "\xE2\x89\xA0"              , 0 }, /* ≠ */
    { "neArr",                           "\xE2\x87\x97"              , 0 }, /* ⇗ */
    { "nearhk",                          "\xE2\xA4\xA4"              , 0 }, /* ⤤ */
    { "nearr",                           "\xE2\x86\x97"              , 0 }, /* ↗ */
    { "nearrow",                         "\xE2\x86\x97"              , 0 }, /* ↗ */
    { "nedot",                           "\xE2\x89\x90\xCC\xB8"      , 0 }, /* ≐̸ */
    { "nequiv",                          "\xE2\x89\xA2"              , 0 }, /* ≢ */
    { "nesear",                          "\xE2\xA4\xA8"              , 0 }, /* ⤨ */
    { "nesim",                           "\xE2\x89\x82\xCC\xB8"      , 0 }, /* ≂̸ */
    { "nexist",                          "\xE2\x88\x84"              , 0 }, /* ∄ */
    { "nexists",                         "\xE2\x88\x84"              , 0 }, /* ∄ */
    { "nfr",                             "\xF0\x9D\x94\xAB"          , 0 }, /* 𝔫 */
    { "ngE",                             "\xE2\x89\xA7\xCC\xB8"      , 0 }, /* ≧̸ */
    { "nge",                             "\xE2\x89\xB1"              , 0 }, /* ≱ */
    { "ngeq",                            "\xE2\x89\xB1"              , 0 }, /* ≱ */
    { "ngeqq",                           "\xE2\x89\xA7\xCC\xB8"      , 0 }, /* ≧̸ */
    { "ngeqslant",                       "\xE2\xA9\xBE\xCC\xB8"      , 0 }, /* ⩾̸ */
    { "nges",                            "\xE2\xA9\xBE\xCC\xB8"      , 0 }, /* ⩾̸ */
    { "ngsim",                           "\xE2\x89\xB5"              , 0 }, /* ≵ */
    { "ngt",                             "\xE2\x89\xAF"              , 0 }, /* ≯ */
    { "ngtr",                            "\xE2\x89\xAF"              , 0 }, /* ≯ */
    { "nhArr",                           "\xE2\x87\x8E"              , 0 }, /* ⇎ */
    { "nharr",                           "\xE2\x86\xAE"              , 0 }, /* ↮ */
    { "nhpar",                           "\xE2\xAB\xB2"              , 0 }, /* ⫲ */
    { "ni",                              "\xE2\x88\x8B"              , 0 }, /* ∋ */
    { "nis",                             "\xE2\x8B\xBC"              , 0 }, /* ⋼ */
    { "nisd",                            "\xE2\x8B\xBA"              , 0 }, /* ⋺ */
    { "niv",                             "\xE2\x88\x8B"              , 0 }, /* ∋ */
    { "njcy",                            "\xD1\x9A"                  , 0 }, /* њ */
    { "nlArr",                           "\xE2\x87\x8D"              , 0 }, /* ⇍ */
    { "nlE",                             "\xE2\x89\xA6\xCC\xB8"      , 0 }, /* ≦̸ */
    { "nlarr",                           "\xE2\x86\x9A"              , 0 }, /* ↚ */
    { "nldr",                            "\xE2\x80\xA5"              , 0 }, /* ‥ */
    { "nle",                             "\xE2\x89\xB0"              , 0 }, /* ≰ */
    { "nleftarrow",                      "\xE2\x86\x9A"              , 0 }, /* ↚ */
    { "nleftrightarrow",                 "\xE2\x86\xAE"              , 0 }, /* ↮ */
    { "nleq",                            "\xE2\x89\xB0"              , 0 }, /* ≰ */
    { "nleqq",                           "\xE2\x89\xA6\xCC\xB8"      , 0 }, /* ≦̸ */
    { "nleqslant",                       "\xE2\xA9\xBD\xCC\xB8"      , 0 }, /* ⩽̸ */
    { "nles",                            "\xE2\xA9\xBD\xCC\xB8"      , 0 }, /* ⩽̸ */
    { "nless",                           "\xE2\x89\xAE"              , 0 }, /* ≮ */
    { "nlsim",                           "\xE2\x89\xB4"              , 0 }, /* ≴ */
    { "nlt",                             "\xE2\x89\xAE"              , 0 }, /* ≮ */
    { "nltri",                           "\xE2\x8B\xAA"              , 0 }, /* ⋪ */
    { "nltrie",                          "\xE2\x8B\xAC"              , 0 }, /* ⋬ */
    { "nmid",                            "\xE2\x88\xA4"              , 0 }, /* ∤ */
    { "nopf",                            "\xF0\x9D\x95\x9F"          , 0 }, /* 𝕟 */
    { "not",                             "\xC2\xAC"                  , 0 }, /* ¬ */
    { "notin",                           "\xE2\x88\x89"              , 0 }, /* ∉ */
    { "notinE",                          "\xE2\x8B\xB9\xCC\xB8"      , 0 }, /* ⋹̸ */
    { "notindot",                        "\xE2\x8B\xB5\xCC\xB8"      , 0 }, /* ⋵̸ */
    { "notinva",                         "\xE2\x88\x89"              , 0 }, /* ∉ */
    { "notinvb",                         "\xE2\x8B\xB7"              , 0 }, /* ⋷ */
    { "notinvc",                         "\xE2\x8B\xB6"              , 0 }, /* ⋶ */
    { "notni",                           "\xE2\x88\x8C"              , 0 }, /* ∌ */
    { "notniva",                         "\xE2\x88\x8C"              , 0 }, /* ∌ */
    { "notnivb",                         "\xE2\x8B\xBE"              , 0 }, /* ⋾ */
    { "notnivc",                         "\xE2\x8B\xBD"              , 0 }, /* ⋽ */
    { "npar",                            "\xE2\x88\xA6"              , 0 }, /* ∦ */
    { "nparallel",                       "\xE2\x88\xA6"              , 0 }, /* ∦ */
    { "nparsl",                          "\xE2\xAB\xBD\xE2\x83\xA5"  , 0 }, /* ⫽⃥ */
    { "npart",                           "\xE2\x88\x82\xCC\xB8"      , 0 }, /* ∂̸ */
    { "npolint",                         "\xE2\xA8\x94"              , 0 }, /* ⨔ */
    { "npr",                             "\xE2\x8A\x80"              , 0 }, /* ⊀ */
    { "nprcue",                          "\xE2\x8B\xA0"              , 0 }, /* ⋠ */
    { "npre",                            "\xE2\xAA\xAF\xCC\xB8"      , 0 }, /* ⪯̸ */
    { "nprec",                           "\xE2\x8A\x80"              , 0 }, /* ⊀ */
    { "npreceq",                         "\xE2\xAA\xAF\xCC\xB8"      , 0 }, /* ⪯̸ */
    { "nrArr",                           "\xE2\x87\x8F"              , 0 }, /* ⇏ */
    { "nrarr",                           "\xE2\x86\x9B"              , 0 }, /* ↛ */
    { "nrarrc",                          "\xE2\xA4\xB3\xCC\xB8"      , 0 }, /* ⤳̸ */
    { "nrarrw",                          "\xE2\x86\x9D\xCC\xB8"      , 0 }, /* ↝̸ */
    { "nrightarrow",                     "\xE2\x86\x9B"              , 0 }, /* ↛ */
    { "nrtri",                           "\xE2\x8B\xAB"              , 0 }, /* ⋫ */
    { "nrtrie",                          "\xE2\x8B\xAD"              , 0 }, /* ⋭ */
    { "nsc",                             "\xE2\x8A\x81"              , 0 }, /* ⊁ */
    { "nsccue",                          "\xE2\x8B\xA1"              , 0 }, /* ⋡ */
    { "nsce",                            "\xE2\xAA\xB0\xCC\xB8"      , 0 }, /* ⪰̸ */
    { "nscr",                            "\xF0\x9D\x93\x83"          , 0 }, /* 𝓃 */
    { "nshortmid",                       "\xE2\x88\xA4"              , 0 }, /* ∤ */
    { "nshortparallel",                  "\xE2\x88\xA6"              , 0 }, /* ∦ */
    { "nsim",                            "\xE2\x89\x81"              , 0 }, /* ≁ */
    { "nsime",                           "\xE2\x89\x84"              , 0 }, /* ≄ */
    { "nsimeq",                          "\xE2\x89\x84"              , 0 }, /* ≄ */
    { "nsmid",                           "\xE2\x88\xA4"              , 0 }, /* ∤ */
    { "nspar",                           "\xE2\x88\xA6"              , 0 }, /* ∦ */
    { "nsqsube",                         "\xE2\x8B\xA2"              , 0 }, /* ⋢ */
    { "nsqsupe",                         "\xE2\x8B\xA3"              , 0 }, /* ⋣ */
    { "nsub",                            "\xE2\x8A\x84"              , 0 }, /* ⊄ */
    { "nsubE",                           "\xE2\xAB\x85\xCC\xB8"      , 0 }, /* ⫅̸ */
    { "nsube",                           "\xE2\x8A\x88"              , 0 }, /* ⊈ */
    { "nsubset",                         "\xE2\x8A\x82\xE2\x83\x92"  , 0 }, /* ⊂⃒ */
    { "nsubseteq",                       "\xE2\x8A\x88"              , 0 }, /* ⊈ */
    { "nsubseteqq",                      "\xE2\xAB\x85\xCC\xB8"      , 0 }, /* ⫅̸ */
    { "nsucc",                           "\xE2\x8A\x81"              , 0 }, /* ⊁ */
    { "nsucceq",                         "\xE2\xAA\xB0\xCC\xB8"      , 0 }, /* ⪰̸ */
    { "nsup",                            "\xE2\x8A\x85"              , 0 }, /* ⊅ */
    { "nsupE",                           "\xE2\xAB\x86\xCC\xB8"      , 0 }, /* ⫆̸ */
    { "nsupe",                           "\xE2\x8A\x89"              , 0 }, /* ⊉ */
    { "nsupset",                         "\xE2\x8A\x83\xE2\x83\x92"  , 0 }, /* ⊃⃒ */
    { "nsupseteq",                       "\xE2\x8A\x89"              , 0 }, /* ⊉ */
    { "nsupseteqq",                      "\xE2\xAB\x86\xCC\xB8"      , 0 }, /* ⫆̸ */
    { "ntgl",                            "\xE2\x89\xB9"              , 0 }, /* ≹ */
    { "ntilde",                          "\xC3\xB1"                  , 0 }, /* ñ */
    { "ntlg",                            "\xE2\x89\xB8"              , 0 }, /* ≸ */
    { "ntriangleleft",                   "\xE2\x8B\xAA"              , 0 }, /* ⋪ */
    { "ntrianglelefteq",                 "\xE2\x8B\xAC"              , 0 }, /* ⋬ */
    { "ntriangleright",                  "\xE2\x8B\xAB"              , 0 }, /* ⋫ */
    { "ntrianglerighteq",                "\xE2\x8B\xAD"              , 0 }, /* ⋭ */
    { "nu",                              "\xCE\xBD"                  , 0 }, /* ν */
    { "num",                             "\x23"                      , 0 }, /* # */
    { "numero",                          "\xE2\x84\x96"              , 0 }, /* № */
    { "numsp",                           "\xE2\x80\x87"              , 0 }, /*   */
    { "nvDash",                          "\xE2\x8A\xAD"              , 0 }, /* ⊭ */
    { "nvHarr",                          "\xE2\xA4\x84"              , 0 }, /* ⤄ */
    { "nvap",                            "\xE2\x89\x8D\xE2\x83\x92"  , 0 }, /* ≍⃒ */
    { "nvdash",                          "\xE2\x8A\xAC"              , 0 }, /* ⊬ */
    { "nvge",                            "\xE2\x89\xA5\xE2\x83\x92"  , 0 }, /* ≥⃒ */
    { "nvgt",                            "\x3E\xE2\x83\x92"          , 0 }, /* >⃒ */
    { "nvinfin",                         "\xE2\xA7\x9E"              , 0 }, /* ⧞ */
    { "nvlArr",                          "\xE2\xA4\x82"              , 0 }, /* ⤂ */
    { "nvle",                            "\xE2\x89\xA4\xE2\x83\x92"  , 0 }, /* ≤⃒ */
    { "nvlt",                            "\x3C\xE2\x83\x92"          , 0 }, /* <⃒ */
    { "nvltrie",                         "\xE2\x8A\xB4\xE2\x83\x92"  , 0 }, /* ⊴⃒ */
    { "nvrArr",                          "\xE2\xA4\x83"              , 0 }, /* ⤃ */
    { "nvrtrie",                         "\xE2\x8A\xB5\xE2\x83\x92"  , 0 }, /* ⊵⃒ */
    { "nvsim",                           "\xE2\x88\xBC\xE2\x83\x92"  , 0 }, /* ∼⃒ */
    { "nwArr",                           "\xE2\x87\x96"              , 0 }, /* ⇖ */
    { "nwarhk",                          "\xE2\xA4\xA3"              , 0 }, /* ⤣ */
    { "nwarr",                           "\xE2\x86\x96"              , 0 }, /* ↖ */
    { "nwarrow",                         "\xE2\x86\x96"              , 0 }, /* ↖ */
    { "nwnear",                          "\xE2\xA4\xA7"              , 0 }, /* ⤧ */
    { "oS",                              "\xE2\x93\x88"              , 0 }, /* Ⓢ */
    { "oacute",                          "\xC3\xB3"                  , 0 }, /* ó */
    { "oast",                            "\xE2\x8A\x9B"              , 0 }, /* ⊛ */
    { "ocir",                            "\xE2\x8A\x9A"              , 0 }, /* ⊚ */
    { "ocirc",                           "\xC3\xB4"                  , 0 }, /* ô */
    { "ocy",                             "\xD0\xBE"                  , 0 }, /* о */
    { "odash",                           "\xE2\x8A\x9D"              , 0 }, /* ⊝ */
    { "odblac",                          "\xC5\x91"                  , 0 }, /* ő */
    { "odiv",                            "\xE2\xA8\xB8"              , 0 }, /* ⨸ */
    { "odot",                            "\xE2\x8A\x99"              , 0 }, /* ⊙ */
    { "odsold",                          "\xE2\xA6\xBC"              , 0 }, /* ⦼ */
    { "oelig",                           "\xC5\x93"                  , 0 }, /* œ */
    { "ofcir",                           "\xE2\xA6\xBF"              , 0 }, /* ⦿ */
    { "ofr",                             "\xF0\x9D\x94\xAC"          , 0 }, /* 𝔬 */
    { "ogon",                            "\xCB\x9B"                  , 0 }, /* ˛ */
    { "ograve",                          "\xC3\xB2"                  , 0 }, /* ò */
    { "ogt",                             "\xE2\xA7\x81"              , 0 }, /* ⧁ */
    { "ohbar",                           "\xE2\xA6\xB5"              , 0 }, /* ⦵ */
    { "ohm",                             "\xCE\xA9"                  , 0 }, /* Ω */
    { "oint",                            "\xE2\x88\xAE"              , 0 }, /* ∮ */
    { "olarr",                           "\xE2\x86\xBA"              , 0 }, /* ↺ */
    { "olcir",                           "\xE2\xA6\xBE"              , 0 }, /* ⦾ */
    { "olcross",                         "\xE2\xA6\xBB"              , 0 }, /* ⦻ */
    { "oline",                           "\xE2\x80\xBE"              , 0 }, /* ‾ */
    { "olt",                             "\xE2\xA7\x80"              , 0 }, /* ⧀ */
    { "omacr",                           "\xC5\x8D"                  , 0 }, /* ō */
    { "omega",                           "\xCF\x89"                  , 0 }, /* ω */
    { "omicron",                         "\xCE\xBF"                  , 0 }, /* ο */
    { "omid",                            "\xE2\xA6\xB6"              , 0 }, /* ⦶ */
    { "ominus",                          "\xE2\x8A\x96"              , 0 }, /* ⊖ */
    { "oopf",                            "\xF0\x9D\x95\xA0"          , 0 }, /* 𝕠 */
    { "opar",                            "\xE2\xA6\xB7"              , 0 }, /* ⦷ */
    { "operp",                           "\xE2\xA6\xB9"              , 0 }, /* ⦹ */
    { "oplus",                           "\xE2\x8A\x95"              , 0 }, /* ⊕ */
    { "or",                              "\xE2\x88\xA8"              , 0 }, /* ∨ */
    { "orarr",                           "\xE2\x86\xBB"              , 0 }, /* ↻ */
    { "ord",                             "\xE2\xA9\x9D"              , 0 }, /* ⩝ */
    { "order",                           "\xE2\x84\xB4"              , 0 }, /* ℴ */
    { "orderof",                         "\xE2\x84\xB4"              , 0 }, /* ℴ */
    { "ordf",                            "\xC2\xAA"                  , 0 }, /* ª */
    { "ordm",                            "\xC2\xBA"                  , 0 }, /* º */
    { "origof",                          "\xE2\x8A\xB6"              , 0 }, /* ⊶ */
    { "oror",                            "\xE2\xA9\x96"              , 0 }, /* ⩖ */
    { "orslope",                         "\xE2\xA9\x97"              , 0 }, /* ⩗ */
    { "orv",                             "\xE2\xA9\x9B"              , 0 }, /* ⩛ */
    { "oscr",                            "\xE2\x84\xB4"              , 0 }, /* ℴ */
    { "oslash",                          "\xC3\xB8"                  , 0 }, /* ø */
    { "osol",                            "\xE2\x8A\x98"              , 0 }, /* ⊘ */
    { "otilde",                          "\xC3\xB5"                  , 0 }, /* õ */
    { "otimes",                          "\xE2\x8A\x97"              , 0 }, /* ⊗ */
    { "otimesas",                        "\xE2\xA8\xB6"              , 0 }, /* ⨶ */
    { "ouml",                            "\xC3\xB6"                  , 0 }, /* ö */
    { "ovbar",                           "\xE2\x8C\xBD"              , 0 }, /* ⌽ */
    { "par",                             "\xE2\x88\xA5"              , 0 }, /* ∥ */
    { "para",                            "\xC2\xB6"                  , 0 }, /* ¶ */
    { "parallel",                        "\xE2\x88\xA5"              , 0 }, /* ∥ */
    { "parsim",                          "\xE2\xAB\xB3"              , 0 }, /* ⫳ */
    { "parsl",                           "\xE2\xAB\xBD"              , 0 }, /* ⫽ */
    { "part",                            "\xE2\x88\x82"              , 0 }, /* ∂ */
    { "pcy",                             "\xD0\xBF"                  , 0 }, /* п */
    { "percnt",                          "\x25"                      , 0 }, /* % */
    { "period",                          "\x2E"                      , 0 }, /* . */
    { "permil",                          "\xE2\x80\xB0"              , 0 }, /* ‰ */
    { "perp",                            "\xE2\x8A\xA5"              , 0 }, /* ⊥ */
    { "pertenk",                         "\xE2\x80\xB1"              , 0 }, /* ‱ */
    { "pfr",                             "\xF0\x9D\x94\xAD"          , 0 }, /* 𝔭 */
    { "phi",                             "\xCF\x86"                  , 0 }, /* φ */
    { "phiv",                            "\xCF\x95"                  , 0 }, /* ϕ */
    { "phmmat",                          "\xE2\x84\xB3"              , 0 }, /* ℳ */
    { "phone",                           "\xE2\x98\x8E"              , 0 }, /* ☎ */
    { "pi",                              "\xCF\x80"                  , 0 }, /* π */
    { "pitchfork",                       "\xE2\x8B\x94"              , 0 }, /* ⋔ */
    { "piv",                             "\xCF\x96"                  , 0 }, /* ϖ */
    { "planck",                          "\xE2\x84\x8F"              , 0 }, /* ℏ */
    { "planckh",                         "\xE2\x84\x8E"              , 0 }, /* ℎ */
    { "plankv",                          "\xE2\x84\x8F"              , 0 }, /* ℏ */
    { "plus",                            "\x2B"                      , 0 }, /* + */
    { "plusacir",                        "\xE2\xA8\xA3"              , 0 }, /* ⨣ */
    { "plusb",                           "\xE2\x8A\x9E"              , 0 }, /* ⊞ */
    { "pluscir",                         "\xE2\xA8\xA2"              , 0 }, /* ⨢ */
    { "plusdo",                          "\xE2\x88\x94"              , 0 }, /* ∔ */
    { "plusdu",                          "\xE2\xA8\xA5"              , 0 }, /* ⨥ */
    { "pluse",                           "\xE2\xA9\xB2"              , 0 }, /* ⩲ */
    { "plusmn",                          "\xC2\xB1"                  , 0 }, /* ± */
    { "plussim",                         "\xE2\xA8\xA6"              , 0 }, /* ⨦ */
    { "plustwo",                         "\xE2\xA8\xA7"              , 0 }, /* ⨧ */
    { "pm",                              "\xC2\xB1"                  , 0 }, /* ± */
    { "pointint",                        "\xE2\xA8\x95"              , 0 }, /* ⨕ */
    { "popf",                            "\xF0\x9D\x95\xA1"          , 0 }, /* 𝕡 */
    { "pound",                           "\xC2\xA3"                  , 0 }, /* £ */
    { "pr",                              "\xE2\x89\xBA"              , 0 }, /* ≺ */
    { "prE",                             "\xE2\xAA\xB3"              , 0 }, /* ⪳ */
    { "prap",                            "\xE2\xAA\xB7"              , 0 }, /* ⪷ */
    { "prcue",                           "\xE2\x89\xBC"              , 0 }, /* ≼ */
    { "pre",                             "\xE2\xAA\xAF"              , 0 }, /* ⪯ */
    { "prec",                            "\xE2\x89\xBA"              , 0 }, /* ≺ */
    { "precapprox",                      "\xE2\xAA\xB7"              , 0 }, /* ⪷ */
    { "preccurlyeq",                     "\xE2\x89\xBC"              , 0 }, /* ≼ */
    { "preceq",                          "\xE2\xAA\xAF"              , 0 }, /* ⪯ */
    { "precnapprox",                     "\xE2\xAA\xB9"              , 0 }, /* ⪹ */
    { "precneqq",                        "\xE2\xAA\xB5"              , 0 }, /* ⪵ */
    { "precnsim",                        "\xE2\x8B\xA8"              , 0 }, /* ⋨ */
    { "precsim",                         "\xE2\x89\xBE"              , 0 }, /* ≾ */
    { "prime",                           "\xE2\x80\xB2"              , 0 }, /* ′ */
    { "primes",                          "\xE2\x84\x99"              , 0 }, /* ℙ */
    { "prnE",                            "\xE2\xAA\xB5"              , 0 }, /* ⪵ */
    { "prnap",                           "\xE2\xAA\xB9"              , 0 }, /* ⪹ */
    { "prnsim",                          "\xE2\x8B\xA8"              , 0 }, /* ⋨ */
    { "prod",                            "\xE2\x88\x8F"              , 0 }, /* ∏ */
    { "profalar",                        "\xE2\x8C\xAE"              , 0 }, /* ⌮ */
    { "profline",                        "\xE2\x8C\x92"              , 0 }, /* ⌒ */
    { "profsurf",                        "\xE2\x8C\x93"              , 0 }, /* ⌓ */
    { "prop",                            "\xE2\x88\x9D"              , 0 }, /* ∝ */
    { "propto",                          "\xE2\x88\x9D"              , 0 }, /* ∝ */
    { "prsim",                           "\xE2\x89\xBE"              , 0 }, /* ≾ */
    { "prurel",                          "\xE2\x8A\xB0"              , 0 }, /* ⊰ */
    { "pscr",                            "\xF0\x9D\x93\x85"          , 0 }, /* 𝓅 */
    { "psi",                             "\xCF\x88"                  , 0 }, /* ψ */
    { "puncsp",                          "\xE2\x80\x88"              , 0 }, /*   */
    { "qfr",                             "\xF0\x9D\x94\xAE"          , 0 }, /* 𝔮 */
    { "qint",                            "\xE2\xA8\x8C"              , 0 }, /* ⨌ */
    { "qopf",                            "\xF0\x9D\x95\xA2"          , 0 }, /* 𝕢 */
    { "qprime",                          "\xE2\x81\x97"              , 0 }, /* ⁗ */
    { "qscr",                            "\xF0\x9D\x93\x86"          , 0 }, /* 𝓆 */
    { "quaternions",                     "\xE2\x84\x8D"              , 0 }, /* ℍ */
    { "quatint",                         "\xE2\xA8\x96"              , 0 }, /* ⨖ */
    { "quest",                           "\x3F"                      , 0 }, /* ? */
    { "questeq",                         "\xE2\x89\x9F"              , 0 }, /* ≟ */
    { "quot",                            "\x22"                      , 0 }, /* " */
    { "rAarr",                           "\xE2\x87\x9B"              , 0 }, /* ⇛ */
    { "rArr",                            "\xE2\x87\x92"              , 0 }, /* ⇒ */
    { "rAtail",                          "\xE2\xA4\x9C"              , 0 }, /* ⤜ */
    { "rBarr",                           "\xE2\xA4\x8F"              , 0 }, /* ⤏ */
    { "rHar",                            "\xE2\xA5\xA4"              , 0 }, /* ⥤ */
    { "race",                            "\xE2\x88\xBD\xCC\xB1"      , 0 }, /* ∽̱ */
    { "racute",                          "\xC5\x95"                  , 0 }, /* ŕ */
    { "radic",                           "\xE2\x88\x9A"              , 0 }, /* √ */
    { "raemptyv",                        "\xE2\xA6\xB3"              , 0 }, /* ⦳ */
    { "rang",                            "\xE2\x9F\xA9"              , 0 }, /* ⟩ */
    { "rangd",                           "\xE2\xA6\x92"              , 0 }, /* ⦒ */
    { "range",                           "\xE2\xA6\xA5"              , 0 }, /* ⦥ */
    { "rangle",                          "\xE2\x9F\xA9"              , 0 }, /* ⟩ */
    { "raquo",                           "\xC2\xBB"                  , 0 }, /* » */
    { "rarr",                            "\xE2\x86\x92"              , 0 }, /* → */
    { "rarrap",                          "\xE2\xA5\xB5"              , 0 }, /* ⥵ */
    { "rarrb",                           "\xE2\x87\xA5"              , 0 }, /* ⇥ */
    { "rarrbfs",                         "\xE2\xA4\xA0"              , 0 }, /* ⤠ */
    { "rarrc",                           "\xE2\xA4\xB3"              , 0 }, /* ⤳ */
    { "rarrfs",                          "\xE2\xA4\x9E"              , 0 }, /* ⤞ */
    { "rarrhk",                          "\xE2\x86\xAA"              , 0 }, /* ↪ */
    { "rarrlp",                          "\xE2\x86\xAC"              , 0 }, /* ↬ */
    { "rarrpl",                          "\xE2\xA5\x85"              , 0 }, /* ⥅ */
    { "rarrsim",                         "\xE2\xA5\xB4"              , 0 }, /* ⥴ */
    { "rarrtl",                          "\xE2\x86\xA3"              , 0 }, /* ↣ */
    { "rarrw",                           "\xE2\x86\x9D"              , 0 }, /* ↝ */
    { "ratail",                          "\xE2\xA4\x9A"              , 0 }, /* ⤚ */
    { "ratio",                           "\xE2\x88\xB6"              , 0 }, /* ∶ */
    { "rationals",                       "\xE2\x84\x9A"              , 0 }, /* ℚ */
    { "rbarr",                           "\xE2\xA4\x8D"              , 0 }, /* ⤍ */
    { "rbbrk",                           "\xE2\x9D\xB3"              , 0 }, /* ❳ */
    { "rbrace",                          "\x7D"                      , 0 }, /* } */
    { "rbrack",                          "\x5D"                      , 0 }, /* ] */
    { "rbrke",                           "\xE2\xA6\x8C"              , 0 }, /* ⦌ */
    { "rbrksld",                         "\xE2\xA6\x8E"              , 0 }, /* ⦎ */
    { "rbrkslu",                         "\xE2\xA6\x90"              , 0 }, /* ⦐ */
    { "rcaron",                          "\xC5\x99"                  , 0 }, /* ř */
    { "rcedil",                          "\xC5\x97"                  , 0 }, /* ŗ */
    { "rceil",                           "\xE2\x8C\x89"              , 0 }, /* ⌉ */
    { "rcub",                            "\x7D"                      , 0 }, /* } */
    { "rcy",                             "\xD1\x80"                  , 0 }, /* р */
    { "rdca",                            "\xE2\xA4\xB7"              , 0 }, /* ⤷ */
    { "rdldhar",                         "\xE2\xA5\xA9"              , 0 }, /* ⥩ */
    { "rdquo",                           "\xE2\x80\x9D"              , 0 }, /* ” */
    { "rdquor",                          "\xE2\x80\x9D"              , 0 }, /* ” */
    { "rdsh",                            "\xE2\x86\xB3"              , 0 }, /* ↳ */
    { "real",                            "\xE2\x84\x9C"              , 0 }, /* ℜ */
    { "realine",                         "\xE2\x84\x9B"              , 0 }, /* ℛ */
    { "realpart",                        "\xE2\x84\x9C"              , 0 }, /* ℜ */
    { "reals",                           "\xE2\x84\x9D"              , 0 }, /* ℝ */
    { "rect",                            "\xE2\x96\xAD"              , 0 }, /* ▭ */
    { "reg",                             "\xC2\xAE"                  , 0 }, /* ® */
    { "rfisht",                          "\xE2\xA5\xBD"              , 0 }, /* ⥽ */
    { "rfloor",                          "\xE2\x8C\x8B"              , 0 }, /* ⌋ */
    { "rfr",                             "\xF0\x9D\x94\xAF"          , 0 }, /* 𝔯 */
    { "rhard",                           "\xE2\x87\x81"              , 0 }, /* ⇁ */
    { "rharu",                           "\xE2\x87\x80"              , 0 }, /* ⇀ */
    { "rharul",                          "\xE2\xA5\xAC"              , 0 }, /* ⥬ */
    { "rho",                             "\xCF\x81"                  , 0 }, /* ρ */
    { "rhov",                            "\xCF\xB1"                  , 0 }, /* ϱ */
    { "rightarrow",                      "\xE2\x86\x92"              , 0 }, /* → */
    { "rightarrowtail",                  "\xE2\x86\xA3"              , 0 }, /* ↣ */
    { "rightharpoondown",                "\xE2\x87\x81"              , 0 }, /* ⇁ */
    { "rightharpoonup",                  "\xE2\x87\x80"              , 0 }, /* ⇀ */
    { "rightleftarrows",                 "\xE2\x87\x84"              , 0 }, /* ⇄ */
    { "rightleftharpoons",               "\xE2\x87\x8C"              , 0 }, /* ⇌ */
    { "rightrightarrows",                "\xE2\x87\x89"              , 0 }, /* ⇉ */
    { "rightsquigarrow",                 "\xE2\x86\x9D"              , 0 }, /* ↝ */
    { "rightthreetimes",                 "\xE2\x8B\x8C"              , 0 }, /* ⋌ */
    { "ring",                            "\xCB\x9A"                  , 0 }, /* ˚ */
    { "risingdotseq",                    "\xE2\x89\x93"              , 0 }, /* ≓ */
    { "rlarr",                           "\xE2\x87\x84"              , 0 }, /* ⇄ */
    { "rlhar",                           "\xE2\x87\x8C"              , 0 }, /* ⇌ */
    { "rlm",                             "\xE2\x80\x8F"              , 0 }, /* ‏ */
    { "rmoust",                          "\xE2\x8E\xB1"              , 0 }, /* ⎱ */
    { "rmoustache",                      "\xE2\x8E\xB1"              , 0 }, /* ⎱ */
    { "rnmid",                           "\xE2\xAB\xAE"              , 0 }, /* ⫮ */
    { "roang",                           "\xE2\x9F\xAD"              , 0 }, /* ⟭ */
    { "roarr",                           "\xE2\x87\xBE"              , 0 }, /* ⇾ */
    { "robrk",                           "\xE2\x9F\xA7"              , 0 }, /* ⟧ */
    { "ropar",                           "\xE2\xA6\x86"              , 0 }, /* ⦆ */
    { "ropf",                            "\xF0\x9D\x95\xA3"          , 0 }, /* 𝕣 */
    { "roplus",                          "\xE2\xA8\xAE"              , 0 }, /* ⨮ */
    { "rotimes",                         "\xE2\xA8\xB5"              , 0 }, /* ⨵ */
    { "rpar",                            "\x29"                      , 0 }, /* ) */
    { "rpargt",                          "\xE2\xA6\x94"              , 0 }, /* ⦔ */
    { "rppolint",                        "\xE2\xA8\x92"              , 0 }, /* ⨒ */
    { "rrarr",                           "\xE2\x87\x89"              , 0 }, /* ⇉ */
    { "rsaquo",                          "\xE2\x80\xBA"              , 0 }, /* › */
    { "rscr",                            "\xF0\x9D\x93\x87"          , 0 }, /* 𝓇 */
    { "rsh",                             "\xE2\x86\xB1"              , 0 }, /* ↱ */
    { "rsqb",                            "\x5D"                      , 0 }, /* ] */
    { "rsquo",                           "\xE2\x80\x99"              , 0 }, /* ’ */
    { "rsquor",                          "\xE2\x80\x99"              , 0 }, /* ’ */
    { "rthree",                          "\xE2\x8B\x8C"              , 0 }, /* ⋌ */
    { "rtimes",                          "\xE2\x8B\x8A"              , 0 }, /* ⋊ */
    { "rtri",                            "\xE2\x96\xB9"              , 0 }, /* ▹ */
    { "rtrie",                           "\xE2\x8A\xB5"              , 0 }, /* ⊵ */
    { "rtrif",                           "\xE2\x96\xB8"              , 0 }, /* ▸ */
    { "rtriltri",                        "\xE2\xA7\x8E"              , 0 }, /* ⧎ */
    { "ruluhar",                         "\xE2\xA5\xA8"              , 0 }, /* ⥨ */
    { "rx",                              "\xE2\x84\x9E"              , 0 }, /* ℞ */
    { "sacute",                          "\xC5\x9B"                  , 0 }, /* ś */
    { "sbquo",                           "\xE2\x80\x9A"              , 0 }, /* ‚ */
    { "sc",                              "\xE2\x89\xBB"              , 0 }, /* ≻ */
    { "scE",                             "\xE2\xAA\xB4"              , 0 }, /* ⪴ */
    { "scap",                            "\xE2\xAA\xB8"              , 0 }, /* ⪸ */
    { "scaron",                          "\xC5\xA1"                  , 0 }, /* š */
    { "sccue",                           "\xE2\x89\xBD"              , 0 }, /* ≽ */
    { "sce",                             "\xE2\xAA\xB0"              , 0 }, /* ⪰ */
    { "scedil",                          "\xC5\x9F"                  , 0 }, /* ş */
    { "scirc",                           "\xC5\x9D"                  , 0 }, /* ŝ */
    { "scnE",                            "\xE2\xAA\xB6"              , 0 }, /* ⪶ */
    { "scnap",                           "\xE2\xAA\xBA"              , 0 }, /* ⪺ */
    { "scnsim",                          "\xE2\x8B\xA9"              , 0 }, /* ⋩ */
    { "scpolint",                        "\xE2\xA8\x93"              , 0 }, /* ⨓ */
    { "scsim",                           "\xE2\x89\xBF"              , 0 }, /* ≿ */
    { "scy",                             "\xD1\x81"                  , 0 }, /* с */
    { "sdot",                            "\xE2\x8B\x85"              , 0 }, /* ⋅ */
    { "sdotb",                           "\xE2\x8A\xA1"              , 0 }, /* ⊡ */
    { "sdote",                           "\xE2\xA9\xA6"              , 0 }, /* ⩦ */
    { "seArr",                           "\xE2\x87\x98"              , 0 }, /* ⇘ */
    { "searhk",                          "\xE2\xA4\xA5"              , 0 }, /* ⤥ */
    { "searr",                           "\xE2\x86\x98"              , 0 }, /* ↘ */
    { "searrow",                         "\xE2\x86\x98"              , 0 }, /* ↘ */
    { "sect",                            "\xC2\xA7"                  , 0 }, /* § */
    { "semi",                            "\x3B"                      , 0 }, /* ; */
    { "seswar",                          "\xE2\xA4\xA9"              , 0 }, /* ⤩ */
    { "setminus",                        "\xE2\x88\x96"              , 0 }, /* ∖ */
    { "setmn",                           "\xE2\x88\x96"              , 0 }, /* ∖ */
    { "sext",                            "\xE2\x9C\xB6"              , 0 }, /* ✶ */
    { "sfr",                             "\xF0\x9D\x94\xB0"          , 0 }, /* 𝔰 */
    { "sfrown",                          "\xE2\x8C\xA2"              , 0 }, /* ⌢ */
    { "sharp",                           "\xE2\x99\xAF"              , 0 }, /* ♯ */
    { "shchcy",                          "\xD1\x89"                  , 0 }, /* щ */
    { "shcy",                            "\xD1\x88"                  , 0 }, /* ш */
    { "shortmid",                        "\xE2\x88\xA3"              , 0 }, /* ∣ */
    { "shortparallel",                   "\xE2\x88\xA5"              , 0 }, /* ∥ */
    { "shy",                             "\xC2\xAD"                  , 0 }, /* ­ */
    { "sigma",                           "\xCF\x83"                  , 0 }, /* σ */
    { "sigmaf",                          "\xCF\x82"                  , 0 }, /* ς */
    { "sigmav",                          "\xCF\x82"                  , 0 }, /* ς */
    { "sim",                             "\xE2\x88\xBC"              , 0 }, /* ∼ */
    { "simdot",                          "\xE2\xA9\xAA"              , 0 }, /* ⩪ */
    { "sime",                            "\xE2\x89\x83"              , 0 }, /* ≃ */
    { "simeq",                           "\xE2\x89\x83"              , 0 }, /* ≃ */
    { "simg",                            "\xE2\xAA\x9E"              , 0 }, /* ⪞ */
    { "simgE",                           "\xE2\xAA\xA0"              , 0 }, /* ⪠ */
    { "siml",                            "\xE2\xAA\x9D"              , 0 }, /* ⪝ */
    { "simlE",                           "\xE2\xAA\x9F"              , 0 }, /* ⪟ */
    { "simne",                           "\xE2\x89\x86"              , 0 }, /* ≆ */
    { "simplus",                         "\xE2\xA8\xA4"              , 0 }, /* ⨤ */
    { "simrarr",                         "\xE2\xA5\xB2"              , 0 }, /* ⥲ */
    { "slarr",                           "\xE2\x86\x90"              , 0 }, /* ← */
    { "smallsetminus",                   "\xE2\x88\x96"              , 0 }, /* ∖ */
    { "smashp",                          "\xE2\xA8\xB3"              , 0 }, /* ⨳ */
    { "smeparsl",                        "\xE2\xA7\xA4"              , 0 }, /* ⧤ */
    { "smid",                            "\xE2\x88\xA3"              , 0 }, /* ∣ */
    { "smile",                           "\xE2\x8C\xA3"              , 0 }, /* ⌣ */
    { "smt",                             "\xE2\xAA\xAA"              , 0 }, /* ⪪ */
    { "smte",                            "\xE2\xAA\xAC"              , 0 }, /* ⪬ */
    { "smtes",                           "\xE2\xAA\xAC\xEF\xB8\x80"  , 0 }, /* ⪬︀ */
    { "softcy",                          "\xD1\x8C"                  , 0 }, /* ь */
    { "sol",                             "\x2F"                      , 0 }, /* / */
    { "solb",                            "\xE2\xA7\x84"              , 0 }, /* ⧄ */
    { "solbar",                          "\xE2\x8C\xBF"              , 0 }, /* ⌿ */
    { "sopf",                            "\xF0\x9D\x95\xA4"          , 0 }, /* 𝕤 */
    { "spades",                          "\xE2\x99\xA0"              , 0 }, /* ♠ */
    { "spadesuit",                       "\xE2\x99\xA0"              , 0 }, /* ♠ */
    { "spar",                            "\xE2\x88\xA5"              , 0 }, /* ∥ */
    { "sqcap",                           "\xE2\x8A\x93"              , 0 }, /* ⊓ */
    { "sqcaps",                          "\xE2\x8A\x93\xEF\xB8\x80"  , 0 }, /* ⊓︀ */
    { "sqcup",                           "\xE2\x8A\x94"              , 0 }, /* ⊔ */
    { "sqcups",                          "\xE2\x8A\x94\xEF\xB8\x80"  , 0 }, /* ⊔︀ */
    { "sqsub",                           "\xE2\x8A\x8F"              , 0 }, /* ⊏ */
    { "sqsube",                          "\xE2\x8A\x91"              , 0 }, /* ⊑ */
    { "sqsubset",                        "\xE2\x8A\x8F"              , 0 }, /* ⊏ */
    { "sqsubseteq",                      "\xE2\x8A\x91"              , 0 }, /* ⊑ */
    { "sqsup",                           "\xE2\x8A\x90"              , 0 }, /* ⊐ */
    { "sqsupe",                          "\xE2\x8A\x92"              , 0 }, /* ⊒ */
    { "sqsupset",                        "\xE2\x8A\x90"              , 0 }, /* ⊐ */
    { "sqsupseteq",                      "\xE2\x8A\x92"              , 0 }, /* ⊒ */
    { "squ",                             "\xE2\x96\xA1"              , 0 }, /* □ */
    { "square",                          "\xE2\x96\xA1"              , 0 }, /* □ */
    { "squarf",                          "\xE2\x96\xAA"              , 0 }, /* ▪ */
    { "squf",                            "\xE2\x96\xAA"              , 0 }, /* ▪ */
    { "srarr",                           "\xE2\x86\x92"              , 0 }, /* → */
    { "sscr",                            "\xF0\x9D\x93\x88"          , 0 }, /* 𝓈 */
    { "ssetmn",                          "\xE2\x88\x96"              , 0 }, /* ∖ */
    { "ssmile",                          "\xE2\x8C\xA3"              , 0 }, /* ⌣ */
    { "sstarf",                          "\xE2\x8B\x86"              , 0 }, /* ⋆ */
    { "star",                            "\xE2\x98\x86"              , 0 }, /* ☆ */
    { "starf",                           "\xE2\x98\x85"              , 0 }, /* ★ */
    { "straightepsilon",                 "\xCF\xB5"                  , 0 }, /* ϵ */
    { "straightphi",                     "\xCF\x95"                  , 0 }, /* ϕ */
    { "strns",                           "\xC2\xAF"                  , 0 }, /* ¯ */
    { "sub",                             "\xE2\x8A\x82"              , 0 }, /* ⊂ */
    { "subE",                            "\xE2\xAB\x85"              , 0 }, /* ⫅ */
    { "subdot",                          "\xE2\xAA\xBD"              , 0 }, /* ⪽ */
    { "sube",                            "\xE2\x8A\x86"              , 0 }, /* ⊆ */
    { "subedot",                         "\xE2\xAB\x83"              , 0 }, /* ⫃ */
    { "submult",                         "\xE2\xAB\x81"              , 0 }, /* ⫁ */
    { "subnE",                           "\xE2\xAB\x8B"              , 0 }, /* ⫋ */
    { "subne",                           "\xE2\x8A\x8A"              , 0 }, /* ⊊ */
    { "subplus",                         "\xE2\xAA\xBF"              , 0 }, /* ⪿ */
    { "subrarr",                         "\xE2\xA5\xB9"              , 0 }, /* ⥹ */
    { "subset",                          "\xE2\x8A\x82"              , 0 }, /* ⊂ */
    { "subseteq",                        "\xE2\x8A\x86"              , 0 }, /* ⊆ */
    { "subseteqq",                       "\xE2\xAB\x85"              , 0 }, /* ⫅ */
    { "subsetneq",                       "\xE2\x8A\x8A"              , 0 }, /* ⊊ */
    { "subsetneqq",                      "\xE2\xAB\x8B"              , 0 }, /* ⫋ */
    { "subsim",                          "\xE2\xAB\x87"              , 0 }, /* ⫇ */
    { "subsub",                          "\xE2\xAB\x95"              , 0 }, /* ⫕ */
    { "subsup",                          "\xE2\xAB\x93"              , 0 }, /* ⫓ */
    { "succ",                            "\xE2\x89\xBB"              , 0 }, /* ≻ */
    { "succapprox",                      "\xE2\xAA\xB8"              , 0 }, /* ⪸ */
    { "succcurlyeq",                     "\xE2\x89\xBD"              , 0 }, /* ≽ */
    { "succeq",                          "\xE2\xAA\xB0"              , 0 }, /* ⪰ */
    { "succnapprox",                     "\xE2\xAA\xBA"              , 0 }, /* ⪺ */
    { "succneqq",                        "\xE2\xAA\xB6"              , 0 }, /* ⪶ */
    { "succnsim",                        "\xE2\x8B\xA9"              , 0 }, /* ⋩ */
    { "succsim",                         "\xE2\x89\xBF"              , 0 }, /* ≿ */
    { "sum",                             "\xE2\x88\x91"              , 0 }, /* ∑ */
    { "sung",                            "\xE2\x99\xAA"              , 0 }, /* ♪ */
    { "sup",                             "\xE2\x8A\x83"              , 0 }, /* ⊃ */
    { "sup1",                            "\xC2\xB9"                  , 0 }, /* ¹ */
    { "sup2",                            "\xC2\xB2"                  , 0 }, /* ² */
    { "sup3",                            "\xC2\xB3"                  , 0 }, /* ³ */
    { "supE",                            "\xE2\xAB\x86"              , 0 }, /* ⫆ */
    { "supdot",                          "\xE2\xAA\xBE"              , 0 }, /* ⪾ */
    { "supdsub",                         "\xE2\xAB\x98"              , 0 }, /* ⫘ */
    { "supe",                            "\xE2\x8A\x87"              , 0 }, /* ⊇ */
    { "supedot",                         "\xE2\xAB\x84"              , 0 }, /* ⫄ */
    { "suphsol",                         "\xE2\x9F\x89"              , 0 }, /* ⟉ */
    { "suphsub",                         "\xE2\xAB\x97"              , 0 }, /* ⫗ */
    { "suplarr",                         "\xE2\xA5\xBB"              , 0 }, /* ⥻ */
    { "supmult",                         "\xE2\xAB\x82"              , 0 }, /* ⫂ */
    { "supnE",                           "\xE2\xAB\x8C"              , 0 }, /* ⫌ */
    { "supne",                           "\xE2\x8A\x8B"              , 0 }, /* ⊋ */
    { "supplus",                         "\xE2\xAB\x80"              , 0 }, /* ⫀ */
    { "supset",                          "\xE2\x8A\x83"              , 0 }, /* ⊃ */
    { "supseteq",                        "\xE2\x8A\x87"              , 0 }, /* ⊇ */
    { "supseteqq",                       "\xE2\xAB\x86"              , 0 }, /* ⫆ */
    { "supsetneq",                       "\xE2\x8A\x8B"              , 0 }, /* ⊋ */
    { "supsetneqq",                      "\xE2\xAB\x8C"              , 0 }, /* ⫌ */
    { "supsim",                          "\xE2\xAB\x88"              , 0 }, /* ⫈ */
    { "supsub",                          "\xE2\xAB\x94"              , 0 }, /* ⫔ */
    { "supsup",                          "\xE2\xAB\x96"              , 0 }, /* ⫖ */
    { "swArr",                           "\xE2\x87\x99"              , 0 }, /* ⇙ */
    { "swarhk",                          "\xE2\xA4\xA6"              , 0 }, /* ⤦ */
    { "swarr",                           "\xE2\x86\x99"              , 0 }, /* ↙ */
    { "swarrow",                         "\xE2\x86\x99"              , 0 }, /* ↙ */
    { "swnwar",                          "\xE2\xA4\xAA"              , 0 }, /* ⤪ */
    { "szlig",                           "\xC3\x9F"                  , 0 }, /* ß */
    { "target",                          "\xE2\x8C\x96"              , 0 }, /* ⌖ */
    { "tau",                             "\xCF\x84"                  , 0 }, /* τ */
    { "tbrk",                            "\xE2\x8E\xB4"              , 0 }, /* ⎴ */
    { "tcaron",                          "\xC5\xA5"                  , 0 }, /* ť */
    { "tcedil",                          "\xC5\xA3"                  , 0 }, /* ţ */
    { "tcy",                             "\xD1\x82"                  , 0 }, /* т */
    { "tdot",                            "\x20\xE2\x83\x9B"          , 0 }, /*  ⃛ */
    { "telrec",                          "\xE2\x8C\x95"              , 0 }, /* ⌕ */
    { "tfr",                             "\xF0\x9D\x94\xB1"          , 0 }, /* 𝔱 */
    { "there4",                          "\xE2\x88\xB4"              , 0 }, /* ∴ */
    { "therefore",                       "\xE2\x88\xB4"              , 0 }, /* ∴ */
    { "theta",                           "\xCE\xB8"                  , 0 }, /* θ */
    { "thetasym",                        "\xCF\x91"                  , 0 }, /* ϑ */
    { "thetav",                          "\xCF\x91"                  , 0 }, /* ϑ */
    { "thickapprox",                     "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "thicksim",                        "\xE2\x88\xBC"              , 0 }, /* ∼ */
    { "thinsp",                          "\xE2\x80\x89"              , 0 }, /*   */
    { "thkap",                           "\xE2\x89\x88"              , 0 }, /* ≈ */
    { "thksim",                          "\xE2\x88\xBC"              , 0 }, /* ∼ */
    { "thorn",                           "\xC3\xBE"                  , 0 }, /* þ */
    { "tilde",                           "\xCB\x9C"                  , 0 }, /* ˜ */
    { "times",                           "\xC3\x97"                  , 0 }, /* × */
    { "timesb",                          "\xE2\x8A\xA0"              , 0 }, /* ⊠ */
    { "timesbar",                        "\xE2\xA8\xB1"              , 0 }, /* ⨱ */
    { "timesd",                          "\xE2\xA8\xB0"              , 0 }, /* ⨰ */
    { "tint",                            "\xE2\x88\xAD"              , 0 }, /* ∭ */
    { "toea",                            "\xE2\xA4\xA8"              , 0 }, /* ⤨ */
    { "top",                             "\xE2\x8A\xA4"              , 0 }, /* ⊤ */
    { "topbot",                          "\xE2\x8C\xB6"              , 0 }, /* ⌶ */
    { "topcir",                          "\xE2\xAB\xB1"              , 0 }, /* ⫱ */
    { "topf",                            "\xF0\x9D\x95\xA5"          , 0 }, /* 𝕥 */
    { "topfork",                         "\xE2\xAB\x9A"              , 0 }, /* ⫚ */
    { "tosa",                            "\xE2\xA4\xA9"              , 0 }, /* ⤩ */
    { "tprime",                          "\xE2\x80\xB4"              , 0 }, /* ‴ */
    { "trade",                           "\xE2\x84\xA2"              , 0 }, /* ™ */
    { "triangle",                        "\xE2\x96\xB5"              , 0 }, /* ▵ */
    { "triangledown",                    "\xE2\x96\xBF"              , 0 }, /* ▿ */
    { "triangleleft",                    "\xE2\x97\x83"              , 0 }, /* ◃ */
    { "trianglelefteq",                  "\xE2\x8A\xB4"              , 0 }, /* ⊴ */
    { "triangleq",                       "\xE2\x89\x9C"              , 0 }, /* ≜ */
    { "triangleright",                   "\xE2\x96\xB9"              , 0 }, /* ▹ */
    { "trianglerighteq",                 "\xE2\x8A\xB5"              , 0 }, /* ⊵ */
    { "tridot",                          "\xE2\x97\xAC"              , 0 }, /* ◬ */
    { "trie",                            "\xE2\x89\x9C"              , 0 }, /* ≜ */
    { "triminus",                        "\xE2\xA8\xBA"              , 0 }, /* ⨺ */
    { "triplus",                         "\xE2\xA8\xB9"              , 0 }, /* ⨹ */
    { "trisb",                           "\xE2\xA7\x8D"              , 0 }, /* ⧍ */
    { "tritime",                         "\xE2\xA8\xBB"              , 0 }, /* ⨻ */
    { "trpezium",                        "\xE2\x8F\xA2"              , 0 }, /* ⏢ */
    { "tscr",                            "\xF0\x9D\x93\x89"          , 0 }, /* 𝓉 */
    { "tscy",                            "\xD1\x86"                  , 0 }, /* ц */
    { "tshcy",                           "\xD1\x9B"                  , 0 }, /* ћ */
    { "tstrok",                          "\xC5\xA7"                  , 0 }, /* ŧ */
    { "twixt",                           "\xE2\x89\xAC"              , 0 }, /* ≬ */
    { "twoheadleftarrow",                "\xE2\x86\x9E"              , 0 }, /* ↞ */
    { "twoheadrightarrow",               "\xE2\x86\xA0"              , 0 }, /* ↠ */
    { "uArr",                            "\xE2\x87\x91"              , 0 }, /* ⇑ */
    { "uHar",                            "\xE2\xA5\xA3"              , 0 }, /* ⥣ */
    { "uacute",                          "\xC3\xBA"                  , 0 }, /* ú */
    { "uarr",                            "\xE2\x86\x91"              , 0 }, /* ↑ */
    { "ubrcy",                           "\xD1\x9E"                  , 0 }, /* ў */
    { "ubreve",                          "\xC5\xAD"                  , 0 }, /* ŭ */
    { "ucirc",                           "\xC3\xBB"                  , 0 }, /* û */
    { "ucy",                             "\xD1\x83"                  , 0 }, /* у */
    { "udarr",                           "\xE2\x87\x85"              , 0 }, /* ⇅ */
    { "udblac",                          "\xC5\xB1"                  , 0 }, /* ű */
    { "udhar",                           "\xE2\xA5\xAE"              , 0 }, /* ⥮ */
    { "ufisht",                          "\xE2\xA5\xBE"              , 0 }, /* ⥾ */
    { "ufr",                             "\xF0\x9D\x94\xB2"          , 0 }, /* 𝔲 */
    { "ugrave",                          "\xC3\xB9"                  , 0 }, /* ù */
    { "uharl",                           "\xE2\x86\xBF"              , 0 }, /* ↿ */
    { "uharr",                           "\xE2\x86\xBE"              , 0 }, /* ↾ */
    { "uhblk",                           "\xE2\x96\x80"              , 0 }, /* ▀ */
    { "ulcorn",                          "\xE2\x8C\x9C"              , 0 }, /* ⌜ */
    { "ulcorner",                        "\xE2\x8C\x9C"              , 0 }, /* ⌜ */
    { "ulcrop",                          "\xE2\x8C\x8F"              , 0 }, /* ⌏ */
    { "ultri",                           "\xE2\x97\xB8"              , 0 }, /* ◸ */
    { "umacr",                           "\xC5\xAB"                  , 0 }, /* ū */
    { "uml",                             "\xC2\xA8"                  , 0 }, /* ¨ */
    { "uogon",                           "\xC5\xB3"                  , 0 }, /* ų */
    { "uopf",                            "\xF0\x9D\x95\xA6"          , 0 }, /* 𝕦 */
    { "uparrow",                         "\xE2\x86\x91"              , 0 }, /* ↑ */
    { "updownarrow",                     "\xE2\x86\x95"              , 0 }, /* ↕ */
    { "upharpoonleft",                   "\xE2\x86\xBF"              , 0 }, /* ↿ */
    { "upharpoonright",                  "\xE2\x86\xBE"              , 0 }, /* ↾ */
    { "uplus",                           "\xE2\x8A\x8E"              , 0 }, /* ⊎ */
    { "upsi",                            "\xCF\x85"                  , 0 }, /* υ */
    { "upsih",                           "\xCF\x92"                  , 0 }, /* ϒ */
    { "upsilon",                         "\xCF\x85"                  , 0 }, /* υ */
    { "upuparrows",                      "\xE2\x87\x88"              , 0 }, /* ⇈ */
    { "urcorn",                          "\xE2\x8C\x9D"              , 0 }, /* ⌝ */
    { "urcorner",                        "\xE2\x8C\x9D"              , 0 }, /* ⌝ */
    { "urcrop",                          "\xE2\x8C\x8E"              , 0 }, /* ⌎ */
    { "uring",                           "\xC5\xAF"                  , 0 }, /* ů */
    { "urtri",                           "\xE2\x97\xB9"              , 0 }, /* ◹ */
    { "uscr",                            "\xF0\x9D\x93\x8A"          , 0 }, /* 𝓊 */
    { "utdot",                           "\xE2\x8B\xB0"              , 0 }, /* ⋰ */
    { "utilde",                          "\xC5\xA9"                  , 0 }, /* ũ */
    { "utri",                            "\xE2\x96\xB5"              , 0 }, /* ▵ */
    { "utrif",                           "\xE2\x96\xB4"              , 0 }, /* ▴ */
    { "uuarr",                           "\xE2\x87\x88"              , 0 }, /* ⇈ */
    { "uuml",                            "\xC3\xBC"                  , 0 }, /* ü */
    { "uwangle",                         "\xE2\xA6\xA7"              , 0 }, /* ⦧ */
    { "vArr",                            "\xE2\x87\x95"              , 0 }, /* ⇕ */
    { "vBar",                            "\xE2\xAB\xA8"              , 0 }, /* ⫨ */
    { "vBarv",                           "\xE2\xAB\xA9"              , 0 }, /* ⫩ */
    { "vDash",                           "\xE2\x8A\xA8"              , 0 }, /* ⊨ */
    { "vangrt",                          "\xE2\xA6\x9C"              , 0 }, /* ⦜ */
    { "varepsilon",                      "\xCF\xB5"                  , 0 }, /* ϵ */
    { "varkappa",                        "\xCF\xB0"                  , 0 }, /* ϰ */
    { "varnothing",                      "\xE2\x88\x85"              , 0 }, /* ∅ */
    { "varphi",                          "\xCF\x95"                  , 0 }, /* ϕ */
    { "varpi",                           "\xCF\x96"                  , 0 }, /* ϖ */
    { "varpropto",                       "\xE2\x88\x9D"              , 0 }, /* ∝ */
    { "varr",                            "\xE2\x86\x95"              , 0 }, /* ↕ */
    { "varrho",                          "\xCF\xB1"                  , 0 }, /* ϱ */
    { "varsigma",                        "\xCF\x82"                  , 0 }, /* ς */
    { "varsubsetneq",                    "\xE2\x8A\x8A\xEF\xB8\x80"  , 0 }, /* ⊊︀ */
    { "varsubsetneqq",                   "\xE2\xAB\x8B\xEF\xB8\x80"  , 0 }, /* ⫋︀ */
    { "varsupsetneq",                    "\xE2\x8A\x8B\xEF\xB8\x80"  , 0 }, /* ⊋︀ */
    { "varsupsetneqq",                   "\xE2\xAB\x8C\xEF\xB8\x80"  , 0 }, /* ⫌︀ */
    { "vartheta",                        "\xCF\x91"                  , 0 }, /* ϑ */
    { "vartriangleleft",                 "\xE2\x8A\xB2"              , 0 }, /* ⊲ */
    { "vartriangleright",                "\xE2\x8A\xB3"              , 0 }, /* ⊳ */
    { "vcy",                             "\xD0\xB2"                  , 0 }, /* в */
    { "vdash",                           "\xE2\x8A\xA2"              , 0 }, /* ⊢ */
    { "vee",                             "\xE2\x88\xA8"              , 0 }, /* ∨ */
    { "veebar",                          "\xE2\x8A\xBB"              , 0 }, /* ⊻ */
    { "veeeq",                           "\xE2\x89\x9A"              , 0 }, /* ≚ */
    { "vellip",                          "\xE2\x8B\xAE"              , 0 }, /* ⋮ */
    { "verbar",                          "\x7C"                      , 0 }, /* | */
    { "vert",                            "\x7C"                      , 0 }, /* | */
    { "vfr",                             "\xF0\x9D\x94\xB3"          , 0 }, /* 𝔳 */
    { "vltri",                           "\xE2\x8A\xB2"              , 0 }, /* ⊲ */
    { "vnsub",                           "\xE2\x8A\x82\xE2\x83\x92"  , 0 }, /* ⊂⃒ */
    { "vnsup",                           "\xE2\x8A\x83\xE2\x83\x92"  , 0 }, /* ⊃⃒ */
    { "vopf",                            "\xF0\x9D\x95\xA7"          , 0 }, /* 𝕧 */
    { "vprop",                           "\xE2\x88\x9D"              , 0 }, /* ∝ */
    { "vrtri",                           "\xE2\x8A\xB3"              , 0 }, /* ⊳ */
    { "vscr",                            "\xF0\x9D\x93\x8B"          , 0 }, /* 𝓋 */
    { "vsubnE",                          "\xE2\xAB\x8B\xEF\xB8\x80"  , 0 }, /* ⫋︀ */
    { "vsubne",                          "\xE2\x8A\x8A\xEF\xB8\x80"  , 0 }, /* ⊊︀ */
    { "vsupnE",                          "\xE2\xAB\x8C\xEF\xB8\x80"  , 0 }, /* ⫌︀ */
    { "vsupne",                          "\xE2\x8A\x8B\xEF\xB8\x80"  , 0 }, /* ⊋︀ */
    { "vzigzag",                         "\xE2\xA6\x9A"              , 0 }, /* ⦚ */
    { "wcirc",                           "\xC5\xB5"                  , 0 }, /* ŵ */
    { "wedbar",                          "\xE2\xA9\x9F"              , 0 }, /* ⩟ */
    { "wedge",                           "\xE2\x88\xA7"              , 0 }, /* ∧ */
    { "wedgeq",                          "\xE2\x89\x99"              , 0 }, /* ≙ */
    { "weierp",                          "\xE2\x84\x98"              , 0 }, /* ℘ */
    { "wfr",                             "\xF0\x9D\x94\xB4"          , 0 }, /* 𝔴 */
    { "wopf",                            "\xF0\x9D\x95\xA8"          , 0 }, /* 𝕨 */
    { "wp",                              "\xE2\x84\x98"              , 0 }, /* ℘ */
    { "wr",                              "\xE2\x89\x80"              , 0 }, /* ≀ */
    { "wreath",                          "\xE2\x89\x80"              , 0 }, /* ≀ */
    { "wscr",                            "\xF0\x9D\x93\x8C"          , 0 }, /* 𝓌 */
    { "xcap",                            "\xE2\x8B\x82"              , 0 }, /* ⋂ */
    { "xcirc",                           "\xE2\x97\xAF"              , 0 }, /* ◯ */
    { "xcup",                            "\xE2\x8B\x83"              , 0 }, /* ⋃ */
    { "xdtri",                           "\xE2\x96\xBD"              , 0 }, /* ▽ */
    { "xfr",                             "\xF0\x9D\x94\xB5"          , 0 }, /* 𝔵 */
    { "xhArr",                           "\xE2\x9F\xBA"              , 0 }, /* ⟺ */
    { "xharr",                           "\xE2\x9F\xB7"              , 0 }, /* ⟷ */
    { "xi",                              "\xCE\xBE"                  , 0 }, /* ξ */
    { "xlArr",                           "\xE2\x9F\xB8"              , 0 }, /* ⟸ */
    { "xlarr",                           "\xE2\x9F\xB5"              , 0 }, /* ⟵ */
    { "xmap",                            "\xE2\x9F\xBC"              , 0 }, /* ⟼ */
    { "xnis",                            "\xE2\x8B\xBB"              , 0 }, /* ⋻ */
    { "xodot",                           "\xE2\xA8\x80"              , 0 }, /* ⨀ */
    { "xopf",                            "\xF0\x9D\x95\xA9"          , 0 }, /* 𝕩 */
    { "xoplus",                          "\xE2\xA8\x81"              , 0 }, /* ⨁ */
    { "xotime",                          "\xE2\xA8\x82"              , 0 }, /* ⨂ */
    { "xrArr",                           "\xE2\x9F\xB9"              , 0 }, /* ⟹ */
    { "xrarr",                           "\xE2\x9F\xB6"              , 0 }, /* ⟶ */
    { "xscr",                            "\xF0\x9D\x93\x8D"          , 0 }, /* 𝓍 */
    { "xsqcup",                          "\xE2\xA8\x86"              , 0 }, /* ⨆ */
    { "xuplus",                          "\xE2\xA8\x84"              , 0 }, /* ⨄ */
    { "xutri",                           "\xE2\x96\xB3"              , 0 }, /* △ */
    { "xvee",                            "\xE2\x8B\x81"              , 0 }, /* ⋁ */
    { "xwedge",                          "\xE2\x8B\x80"              , 0 }, /* ⋀ */
    { "yacute",                          "\xC3\xBD"                  , 0 }, /* ý */
    { "yacy",                            "\xD1\x8F"                  , 0 }, /* я */
    { "ycirc",                           "\xC5\xB7"                  , 0 }, /* ŷ */
    { "ycy",                             "\xD1\x8B"                  , 0 }, /* ы */
    { "yen",                             "\xC2\xA5"                  , 0 }, /* ¥ */
    { "yfr",                             "\xF0\x9D\x94\xB6"          , 0 }, /* 𝔶 */
    { "yicy",                            "\xD1\x97"                  , 0 }, /* ї */
    { "yopf",                            "\xF0\x9D\x95\xAA"          , 0 }, /* 𝕪 */
    { "yscr",                            "\xF0\x9D\x93\x8E"          , 0 }, /* 𝓎 */
    { "yucy",                            "\xD1\x8E"                  , 0 }, /* ю */
    { "yuml",                            "\xC3\xBF"                  , 0 }, /* ÿ */
    { "zacute",                          "\xC5\xBA"                  , 0 }, /* ź */
    { "zcaron",                          "\xC5\xBE"                  , 0 }, /* ž */
    { "zcy",                             "\xD0\xB7"                  , 0 }, /* з */
    { "zdot",                            "\xC5\xBC"                  , 0 }, /* ż */
    { "zeetrf",                          "\xE2\x84\xA8"              , 0 }, /* ℨ */
    { "zeta",                            "\xCE\xB6"                  , 0 }, /* ζ */
    { "zfr",                             "\xF0\x9D\x94\xB7"          , 0 }, /* 𝔷 */
    { "zhcy",                            "\xD0\xB6"                  , 0 }, /* ж */
    { "zigrarr",                         "\xE2\x87\x9D"              , 0 }, /* ⇝ */
    { "zopf",                            "\xF0\x9D\x95\xAB"          , 0 }, /* 𝕫 */
    { "zscr",                            "\xF0\x9D\x93\x8F"          , 0 }, /* 𝓏 */
    { "zwj",                             "\xE2\x80\x8D"              , 0 }, /* ‍ */
    { "zwnj",                            "\xE2\x80\x8C"              , 0 }, /* ‌ */
};


/*----------------------------------------------------------------------------
|   ErInit --
|
|       Initialize the entity reference hash table
|
\---------------------------------------------------------------------------*/
static void ErInit (void)
{
    size_t i;  /* For looping through the list of entity references */
    int h;  /* The hash on a entity */

    for(i=0; i<sizeof(er_sequences)/sizeof(er_sequences[0]); i++){
        h = ErHash(er_sequences[i].zName);
        er_sequences[i].pNext = apErHash[h];
        apErHash[h] = &er_sequences[i];
    }

} /* ErInit */


/*----------------------------------------------------------------------------
|    TranslateEntityRefs  --
|
|        Translate entity references and character references in the string
|        "z".  "z" is overwritten with the translated sequence.
|
|        Unrecognized entity references are unaltered.
|
|        Example:
|
|          input =    "AT&amp;T &gt MCI"
|          output =   "AT&T > MCI"
|
\---------------------------------------------------------------------------*/
static void TranslateEntityRefs (
    char *z,
    domLength  *newLen
)
{
    int from;    /* Read characters from this position in z[] */
    int to;      /* Write characters into this position in z[] */
    int h;       /* A hash on the entity reference */
    char *zVal;  /* The substituted value */
    Er *p;       /* For looping down the entity reference collision chain */
    int value;

    from = to = 0;

    if (bErNeedsInit) {
        TDomThreaded(Tcl_MutexLock(&initMutex);)
        if (bErNeedsInit) {
            ErInit();
            bErNeedsInit = 0;
        }
        TDomThreaded(Tcl_MutexUnlock(&initMutex);)
    }

    while (z[from]) {
        if (z[from]=='&') {
            int isInvalid = 0;
            int i = from+1;
            int c;

            if (z[i] == '#') {
                /*---------------------------------------------
                |   convert character reference
                \--------------------------------------------*/
                value = 0;
                if (z[++i] == 'x') {
                    i++;
                    while ((c=z[i]) && (c!=';')) {
                        value = value * 16;
                        if ((c>='0') && (c<='9')) {
                            value += c-'0';
                        } else
                        if ((c>='A') && (c<='F')) {
                            value += c-'A' + 10;
                        } else
                        if ((c>='a') && (c<='f')) {
                            value += c-'a' + 10;
                        } else {
                            /* error */
			    isInvalid = 1;
			    break;
                        }
                        if (value > 2097152) {
                            /* error */
			    isInvalid = 1;
			    break;
                        }
                        i++;
                    }
                } else {
                    while ((c=z[i]) && (c!=';')) {
                        value = value * 10;
                        if ((c>='0') && (c<='9')) {
                            value += c-'0';
                        } else {
                            /* error */
  			    isInvalid = 1;
			    break;
                        }
                        if (value > 2097152) {
                            /* error */
			    isInvalid = 1;
			    break;
                        }
                        i++;
                    }
                }
                if (z[i]!=';') {
                    /* error */
		    isInvalid = 1;
                }
		if (isInvalid) {
		    /*
		     * In case the character reference was invalid
		     * it was a false alaram, no valid character
		     * reference, just copy the source chars;
		     */
		    int j;
		    for (j = from; j < i; j++) {
		        z[to++] = z[j];
		    }
		    from = i;
		} else {
                if (value < 0x80) {
                    z[to++] = value;
                } else if (value <= 0x7FF) {
                    z[to++] = (char) ((value >> 6) | 0xC0);
                    z[to++] = (char) ((value | 0x80) & 0xBF);
                } else if (value <= 0xFFFF) {
                    z[to++] = (char) ((value >> 12) | 0xE0);
                    z[to++] = (char) (((value >> 6) | 0x80) & 0xBF);
                    z[to++] = (char) ((value | 0x80) & 0xBF);
                } else {
                    /* error */
                }
		    from = i+1;
		}
            } else {
                while (z[i] && isalnum((unsigned char)z[i])) {
                   i++;
                }
                c = z[i];
                z[i] = 0;
                h = ErHash(&z[from+1]);
                p = apErHash[h];
                while (p && strcmp(p->zName,&z[from+1])!=0 ) {
                    p = p->pNext;
                }
                z[i] = c;
                if (p) {
                    zVal = p->zValue;
                    while (*zVal) {
                        z[to++] = *(zVal++);
                    }
                    from = i;
                    if (c==';') from++;
                } else {
                    z[to++] = z[from++];
                }
            }
        } else {
            z[to++] = z[from++];
        }
    }
    z[to] = 0;
    *newLen = to;
}
/*----------------------------------------------------------------------------
|   End Of Character Entity Translator
\---------------------------------------------------------------------------*/



DBG(
/*----------------------------------------------------------------------------
|   getDeep
|
\---------------------------------------------------------------------------*/
static int getDeep (domNode *n) {
    int d;
    d = 0;
    while (n->parentNode != NULL) {
        d++;
        n  = n->parentNode;
    }
    return d;
}
)

/*----------------------------------------------------------------------------
|   HTML_SimpleParse (non recursive)
|
|       Parses the HTML string starting at 'pos' and continuing to the
|       first encountered error.
|
\---------------------------------------------------------------------------*/
static int
HTML_SimpleParse (
    char        *html,  /* HTML string  */
    domLength   *pos,   /* Index of next unparsed character in xml */
    domDocument *doc,
    domNode     *parent,
    int          ignoreWhiteSpaces,
    int          forest,
    char       **errStr
) {
    register int   c;          /* Next character of the input file */
    register char *pn, *e;
    register char *x, *start, *piSep;
    int            saved;
    int            hasContent;
    domNode       *pnode;
    domNode       *node = NULL, *parent_node = parent;
    domTextNode   *tnode;
    domAttrNode   *attrnode, *lastAttr;
    int            ampersandSeen = 0;
    int            only_whites   = 0;
    int            hnew, autoclose, ignore;
    char           tmp[250], *y = NULL;
    Tcl_HashEntry *h;
    domProcessingInstructionNode *pinode;

    x = &(html[*pos]);

    while ( (c=*x)!=0 ) {

        start = x;

        if ((c!='<') || ((c=='<') && (x[1]!='!') && (x[2]!='-') && (x[3]!='-') && (x[1]!='/') && !IsLetter(x[1])) ) {
            /*----------------------------------------------------------------
            |   read text between tags
            |
            \---------------------------------------------------------------*/
          readText:
            ampersandSeen = 0;
            only_whites = 1;
            if (c=='<') x++;
            while ( (c=*x)!=0 && c!='<' ) {
                if (c=='&') ampersandSeen = 1;
                if ( !SPACE(c) ) only_whites = 0;
                x++;
            }
                
            if (!(only_whites && ignoreWhiteSpaces) && parent_node) {
                /*--------------------------------------------------------
                |   allocate new TEXT node
                \-------------------------------------------------------*/
                tnode = (domTextNode*) domAlloc(sizeof(domTextNode));
                memset(tnode, 0, sizeof(domTextNode));
                tnode->nodeType    = TEXT_NODE;
                tnode->ownerDocument = doc;
                tnode->nodeNumber  = NODE_NO(doc);
                tnode->valueLength = (x - start);
                tnode->nodeValue   = (char*)MALLOC((x - start)+1);
                memmove(tnode->nodeValue, start, (x - start));
                *(tnode->nodeValue + (x - start)) = 0;
                DBG(fprintf(stderr, "New text node: '%s'\n", tnode->nodeValue);)
                if (ampersandSeen) {
                    TranslateEntityRefs(tnode->nodeValue, &(tnode->valueLength) );
                }
                tnode->parentNode = parent_node;
                if (parent_node->firstChild)  {
                    parent_node->lastChild->nextSibling = (domNode*)tnode;
                    tnode->previousSibling = parent_node->lastChild;
                    parent_node->lastChild = (domNode*)tnode;
                } else {
                    parent_node->firstChild = parent_node->lastChild = (domNode*)tnode;
                }
                node = (domNode*)tnode;
            }

        } else if (x[1]=='/') {
            /*------------------------------------------------------------
            |   read and check closing tag
            \-----------------------------------------------------------*/
            x += 2;
            while ((c=*x)!=0 && c!='>' && c!='<' && !SPACE(c) ) {
                *x = tolower(c);
                x++;
            }
            if (c==0) {
                RetError("Missing \">\"",(start-html) );
            }
            if ( (x-start)==2) {
                RetError("Null markup name",(start-html) );
            }
            *x = '\0'; /* temporarily terminate the string */


            /*----------------------------------------------------------------------
            |   check for tags which could optional be close 
            |   like <option>...</option>
            \---------------------------------------------------------------------*/
            ignore = 0;
            pnode = NULL;
            if (parent_node) {
                if (parent_node->lastChild 
                    && (parent_node->lastChild->nodeType == ELEMENT_NODE)) {
                    pnode = parent_node->lastChild;
                } else 
                    if (parent_node->lastChild 
                        && parent_node->lastChild->previousSibling 
                        && (parent_node->lastChild->previousSibling->nodeType 
                            == ELEMENT_NODE)) {
                        pnode = parent_node->lastChild->previousSibling;
                    }
            }
            if (pnode) {
                DBG(fprintf(stderr, "'%s' closing with last empty tag '%s' ?\n", start+2, pnode->nodeName);)
                if (strcmp(start+2,pnode->nodeName)==0)  {
                    switch (*(start+2)) {
                        case 'o': if (!strcmp(start+2,"option")) ignore = 1; break;
                    }
                }
            } 

            if (!ignore) {
                /*----------------------------------------------------------------------
                |   look for a corresponding opening tag the way up the tag hierarchy
                \---------------------------------------------------------------------*/
                pnode = parent_node;
                while (pnode != NULL) {
                    DBG(fprintf(stderr, "checking '%s' to top hierarchy: '%s' \n", start+2,pnode->nodeName);)
                    if (!strcmp(start+2,pnode->nodeName)) break;
                    pnode = pnode->parentNode;
                }
                if (pnode == NULL) {
                    /* beginning tag was not found the way up the tag hierarchy
                       -> ignore the tag */
                    DBG(fprintf(stderr,"ignoring closing '%s' \n", start+2);)
                    ignore = 1;
                }
            }            
            if (!ignore) {

                pn = (char*)parent_node->nodeName;                        

                while (1) {
                    DBG(fprintf(stderr, "comparing '%s' with pn='%s' \n", start+2, pn);)
                    if (strcmp(start+2,pn)!=0) {

                        /*----------------------------------------------------------
                        |   check for parent tags which allow closing of sub tags
                        |   which belong to the parent tag
                        \---------------------------------------------------------*/
                        ignore = 0;
                        if (!strcmp(pn,"table")
                            && (!strcmp(start+2,"tr") || !strcmp(start+2,"td"))
                        ) {
                            ignore = 1;
                        }
                        if (ignore) {
                            parent_node = node->parentNode;
                            break;
                        }


                        /*---------------------------------------------------------------
                        |   check for tags for which end tag can be omitted
                        \--------------------------------------------------------------*/
                        autoclose = 0;
                        switch (pn[0]) {
                        case 'a': if (!strcmp(pn,"a"))        {autoclose = 1;} break;
                        case 'b': if (!strcmp(pn,"b"))        {autoclose = 1;} break;
                        case 'c': if (!strcmp(pn,"colgroup")) {autoclose = 1;} break;
                        case 'd': if (!strcmp(pn,"dd") ||
                                      !strcmp(pn,"dt") ||
                                      (!strcmp(start+2,"form") && !strcmp(pn,"div"))
                            )                        {autoclose = 1;}          break;
                        case 'h': if (!strcmp(pn,"head") ||
                                      !strcmp(pn,"html"))     {autoclose = 1;} break;
                        case 'f': if (!strcmp(pn,"font")||
                                      !strcmp(pn,"form"))     {autoclose = 1;} break;
                        case 'i': if (!strcmp(pn,"i"))        {autoclose = 1;} break;
                        case 'l': if (!strcmp(pn,"li"))       {autoclose = 1;} break;
                        case 'n': if (!strcmp(pn,"noscript")) {autoclose = 1;} break;
                        case 'o': if (!strcmp(pn,"option"))   {autoclose = 1;} break;
                        case 'p': if (!strcmp(pn,"p"))        {autoclose = 1;} break;
                        case 's': if (!strcmp(pn,"span"))     {autoclose = 1;} break;
                        case 't': if (!strcmp(pn,"tbody") ||
                                      !strcmp(pn,"td")    ||
                                      !strcmp(pn,"tfoot") ||
                                      !strcmp(pn,"thead") ||
                                      !strcmp(pn,"th")    ||
                                      !strcmp(pn,"tr")    ||
                                      !strcmp(pn,"tt"))       {autoclose = 1;} break;
                        case 'u': if (!strcmp(pn,"ul"))       {autoclose = 1;} break; /* ext */
                        }
                        /*---------------------------------------------------------------
                        |   check for tags for close inner tags
                        \--------------------------------------------------------------*/
                        switch (start[2]) {
                            case 'b': if (!strcmp(start+2,"body")) autoclose = 1; break;
                        }
                        if (autoclose) {
                            DBG(fprintf(stderr, "autoclose '%s' with '%s' \n", pn, start+2);)
                            if (parent_node != NULL) {
                                node = parent_node;
                                parent_node = node->parentNode;
                                pn = (char*)node->nodeName;
                                break; 
                            }
                        }
                        sprintf(tmp, "Unterminated element '%s' (within '%s')", start+2, pn);
                        *x = c;  /* remove temporarily termination */
                        RetError(tmp,(x - html));
                    }
                    break;
                }
                
                /* move up */
                node = parent_node;
                parent_node = NULL;
                if (node) parent_node = node->parentNode;
            }
            *x = c;  /* remove temporarily termination */

                     
            while (SPACE(*x)) {
                x++;
            }
            if (*x=='>') {
                x++;
            } else {
                if (*x == '<') {
                    /* start of new tag, ends closing tag */
                } else {
                    RetError("Missing \">\"",(x - html)-1);
                }
            }            
            if (parent_node == NULL) {
                /* we return to main node and so finished parsing */
                return TCL_OK;
            }
            continue;

        } else {

            x++;
            if (*x=='!') {
                if (x[1]=='-' && x[2]=='-') {
                    /*--------------------------------------------------------
                    |   read over a comment
                    \-------------------------------------------------------*/
                    x += 3;
                    while ( (c=*x)!=0 &&
                            (c!='-' || x[1]!='-' || x[2]!='>')) {
                        x++;
                    }
                    if (*x) {
                        /*----------------------------------------------------
                        |   allocate new COMMENT node for comments
                        \---------------------------------------------------*/
                        tnode = (domTextNode*) domAlloc(sizeof(domTextNode));
                        memset(tnode, 0, sizeof(domTextNode));
                        tnode->nodeType      = COMMENT_NODE;
                        tnode->ownerDocument = doc;
                        tnode->nodeNumber    = NODE_NO(doc);
                        tnode->parentNode    = parent_node;
                        tnode->valueLength   = x - start - 4;
                        tnode->nodeValue     = (char*)MALLOC(tnode->valueLength+1);
                        memmove(tnode->nodeValue, start+4, tnode->valueLength);
                        *(tnode->nodeValue + tnode->valueLength) = 0;
                        if (parent_node == NULL) {
                            if (doc->rootNode->lastChild) {
                                tnode->previousSibling = 
                                    doc->rootNode->lastChild;
                                doc->rootNode->lastChild->nextSibling 
                                    = (domNode *) tnode;
                            } else {
                                doc->rootNode->firstChild = (domNode*) tnode;
                            }
                            doc->rootNode->lastChild = (domNode*) tnode;
                        } else {
                            if (parent_node->firstChild)  {
                                parent_node->lastChild->nextSibling = (domNode*)tnode;
                                tnode->previousSibling = parent_node->lastChild;
                                parent_node->lastChild = (domNode*)tnode;
                            } else {
                                parent_node->firstChild = parent_node->lastChild = (domNode*)tnode;
                            }
                        }
                        x += 3;
                    } else {
                        RetError("Unterminated comment",(start-html));
                    }
                    continue;

                } else if (TU(x[1])=='D' && TU(x[2])=='O' &&
                           TU(x[3])=='C' && TU(x[4])=='T' &&
                           TU(x[5])=='Y' && TU(x[6])=='P' && TU(x[7])=='E' ) {
                    /*--------------------------------------------------------
                    |   read over a DOCTYPE definition
                    \-------------------------------------------------------*/
                    x += 8;
                    start = x;
                    while (*x!=0) {
                        if (*x=='[') {
                            x++;
                            while ((*x!=0) && (*x!=']')) x++;
                        } else
                        if (*x=='>') {
                            break;
                        } else {
                            x++;
                        }
                    }
                    if (*x) {
                        x++;
                    } else {
                        RetError("Unterminated DOCTYPE definition",(start-html));
                    }
                    continue;

                } else if (x[1]=='[' && x[2]=='C' &&
                           x[3]=='D' && x[4]=='A' &&
                           x[5]=='T' && x[6]=='A' && x[7]=='[' ) {
                    /*--------------------------------------------------------
                    |   read over a <![CDATA[ section
                    \-------------------------------------------------------*/
                    x += 8;
                    start = x;
                    while ( (*x!=0) &&
                            ((*x!=']') || (x[1]!=']') || (x[2]!='>'))) {
                        x++;
                    }
                    if (*x) {
                        if (parent_node) {
                            /*----------------------------------------------------
                            |   allocate new TEXT node for CDATA section data
                            \---------------------------------------------------*/
                            tnode = (domTextNode*) domAlloc(sizeof(domTextNode));
                            memset(tnode, 0, sizeof(domTextNode));
                            tnode->nodeType      = TEXT_NODE;
                            tnode->ownerDocument = doc;
                            tnode->nodeNumber    = NODE_NO(doc);
                            tnode->parentNode    = parent_node;
                            tnode->valueLength   = (x - start);
                            tnode->nodeValue     = (char*)MALLOC((x - start)+1);
                            memmove(tnode->nodeValue, start, (x - start));
                            *(tnode->nodeValue + (x - start)) = 0;
                            if (parent_node->firstChild)  {
                                parent_node->lastChild->nextSibling = (domNode*)tnode;
                                tnode->previousSibling = parent_node->lastChild;
                                parent_node->lastChild = (domNode*)tnode;
                            } else {
                                parent_node->firstChild = parent_node->lastChild = (domNode*)tnode;
                            }
                        }
                        x += 3;
                    } else {
                        RetError("Unterminated CDATA definition",(start-html) );
                    }
                    continue;
                 } else {
                        RetError("Incorrect <!... tag",(start-html) );
                 }

            } else if (*x=='?') {
                /*--------------------------------------------------------
                |   read over a processing instructions(PI) / XMLDecl
                \-------------------------------------------------------*/
                x++;
                start = x;
                while ( (c=*x)!=0 &&
                        (c!='?' || x[1]!='>')) {
                    x++;
                }
                if (*x) {
                    /*------------------------------------------------------------
                    |   allocate new PI node for processing instruction section
                    \-----------------------------------------------------------*/
                    pinode = (domProcessingInstructionNode*)
                            MALLOC(sizeof(domProcessingInstructionNode));
                    memset(pinode, 0, sizeof(domProcessingInstructionNode));
                    pinode->nodeType      = PROCESSING_INSTRUCTION_NODE;
                    pinode->ownerDocument = doc;
                    pinode->nodeNumber    = NODE_NO(doc);
                    pinode->parentNode    = parent_node;

                    /*-------------------------------------------------
                    |   extract PI target
                    \------------------------------------------------*/
                    piSep = start;
                    while ( (c=*piSep)!=0 && !SPACE(c) &&
                            (c!='?' || piSep[1]!='>')) {
                         piSep++;
                    }
                    *piSep = '\0'; /* temporarily terminate the string */

                    pinode->targetLength = strlen(start);
                    pinode->targetValue  = (char*)MALLOC(pinode->targetLength);
                    memmove(pinode->targetValue, start, pinode->targetLength);

                    *piSep = c;  /* remove temporarily termination */

                    /*-------------------------------------------------
                    |   extract PI data
                    \------------------------------------------------*/
                    while (SPACE(*piSep)) {
                        piSep++;
                    }
                    pinode->dataLength = x - piSep;
                    pinode->dataValue  = (char*)MALLOC(pinode->dataLength);
                    memmove(pinode->dataValue, piSep, pinode->dataLength);

                    if (parent_node == NULL) {
                        if (doc->rootNode->lastChild) {
                            pinode->previousSibling = doc->rootNode->lastChild;
                            doc->rootNode->lastChild->nextSibling 
                                = (domNode*) pinode;
                        } else {
                            doc->rootNode->firstChild = (domNode*) pinode;
                        }
                        doc->rootNode->lastChild = (domNode*) pinode;
                    } else {
                        if (parent_node->firstChild)  {
                            parent_node->lastChild->nextSibling = (domNode*)pinode;
                            pinode->previousSibling = parent_node->lastChild;
                            parent_node->lastChild = (domNode*)pinode;
                        } else {
                            parent_node->firstChild = parent_node->lastChild = (domNode*)pinode;
                        }
                    }
                    x += 2;
                } else {
                    RetError("Unterminated processing instruction(PI)",(start-html) );
                }
                continue;
            }


            /*----------------------------------------------------------------
            |   new tag/element
            |
            \---------------------------------------------------------------*/
            while ((c=*x)!=0 && c!='/' && c!='>' && c!='<' && !SPACE(c) ) {
                if (!isalnum(c)) goto readText;
                *x = tolower(c);
                x++;
            }
            hasContent = 1;
            if (c==0) {
                RetError("Missing \">\"",(start-html) );
            }
            if ( (x-start)==1) {
                RetError("Null markup name",(start-html) );
            }
            DBG(fprintf(stderr, "\nnew tag '%70.70s...' \n", start);)
            *x = '\0'; /* temporarily terminate the string */


            /*-----------------------------------------------------------
            |   check, whether new starting element close an other
            |   currently open one
            \----------------------------------------------------------*/
            e = start+1;
            pn = ""; if (parent_node) { pn = (char*)parent_node->nodeName; }
            autoclose = 0;
            switch (*e) {
                case 'a': if(!strcmp(e,"a")&&!strcmp(pn,"a")) autoclose=1;
                          break;
                case 'b': if(!strcmp(e,"b")&&!strcmp(pn,"b")) autoclose=1;
                          break;
                case 'o': if (!strcmp(e,"option")&&!strcmp(pn,"option")) autoclose = 1; break;
                case 'p': if((!strcmp(e,"pre")&&!strcmp(pn,"pre")) 
                             ||(!strcmp(e,"p")&&!strcmp(pn,"p"))) autoclose=1;
                          break;
            }
            if (autoclose) {
                DBG(fprintf(stderr, "autoclose '%s' because of new '%s' \n", pn, e);)
                node = parent_node;
                parent_node = node->parentNode;
            }

            /*-----------------------------------------------------------
            |   create new DOM element node
            \----------------------------------------------------------*/
            if (!parent_node && (strcmp(e,"html")!=0)) {
                // Insert missing html tag
                h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_tagNames), "html", &hnew);
                node = (domNode*) domAlloc(sizeof(domNode));
                memset(node, 0, sizeof(domNode));
                node->nodeType      = ELEMENT_NODE;
                node->nodeName      = (char *)&(h->key);
                node->ownerDocument = doc;
                node->nodeNumber    = NODE_NO(doc);
                if (doc->rootNode->lastChild) {
                    node->previousSibling = doc->rootNode->lastChild;
                    doc->rootNode->lastChild->nextSibling = node;
                } else {
                    doc->rootNode->firstChild = node;
                }
                doc->rootNode->lastChild = node;
                parent_node = node;
                DBG(fprintf(stderr, "%d: Inserted missing tag '%s' hasContent=%d nodeNumber=%d\n", getDeep(node), node->nodeName, hasContent, node->nodeNumber);)
            }
            h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_tagNames), e, &hnew);
            node = (domNode*) domAlloc(sizeof(domNode));
            memset(node, 0, sizeof(domNode));
            node->nodeType      = ELEMENT_NODE;
            node->nodeName      = (char *)&(h->key);
            node->ownerDocument = doc;
            node->nodeNumber    = NODE_NO(doc);

            if (parent_node == NULL) {
                if (doc->rootNode->lastChild) {
                    node->previousSibling = doc->rootNode->lastChild;
                    doc->rootNode->lastChild->nextSibling = node;
                } else {
                    doc->rootNode->firstChild = node;
                }
                doc->rootNode->lastChild = node;
            } else {
                node->parentNode = parent_node;
                if (parent_node->firstChild)  {
                    parent_node->lastChild->nextSibling = node;
                    node->previousSibling = parent_node->lastChild;
                    parent_node->lastChild = node;
                } else {
                    parent_node->firstChild = parent_node->lastChild = node;
                }
            }

            *x = c;  /* remove temporarily termination */

            while (SPACE(*x) ) {
                x++;
            }
            /*-----------------------------------------------------------
            |   read attribute name-value pairs
            \----------------------------------------------------------*/
            lastAttr = NULL;
            while ( (c=*x) && (c!='/') && (c!='>') && (c!='<') ) {
                char *ArgName = x;
                int nArgName;
                char *ArgVal = NULL;
                int nArgVal = 0;

                while ((c=*x)!=0 && c!='=' && c!='>' && !SPACE(c) ) {
                    *x = tolower(c);
                    x++;
                }
                nArgName = x - ArgName;
                while (SPACE(*x)) {
                    x++;
                }
                if (*x == '=') {
                
                    /* attribute with value, like width="1234" */
                    
                    x++;
                    while (SPACE(*x)) {
                        x++;
                    }
                    saved = *(ArgName + nArgName);
                    *(ArgName + nArgName) = '\0'; /* terminate arg name */

                    if (*x=='>' || *x==0) {
                        ArgVal = ArgName;
                        nArgVal = nArgName;
                    } else if ((c=*x)=='\"' || c=='\'') {
                        register int cDelim = c;
                        x++;
                        ArgVal = x;
                        ampersandSeen = 0;
                        while ((c=*x)!=0 && c!=cDelim) {
                            if (c=='&') ampersandSeen = 1;
                            x++;
                        }
                        nArgVal = x - ArgVal;
                        if (c==0) {
                            RetError("Unterminated string",(ArgVal - html - 1) );
                        } else {
                            x++;
                        }
                    } else if (c!=0 && c!='>') {
                        ArgVal = x;
                        while ((c=*x)!=0 && c!='>' && !SPACE(c)) {
                            if (c=='&') ampersandSeen = 1;
                            x++;
                        }
                        if (c==0) {
                            RetError("Missing \">\"",(start-html));
                        }
                        nArgVal = x - ArgVal;
                    }
                } else {
                    /* attribute without value, like 'nowrap' */
                    saved = *(ArgName + nArgName);
                    *(ArgName + nArgName) = '\0'; /* terminate arg name */
                    ArgVal = ArgName;
                    nArgVal = nArgName;
                }

                /*--------------------------------------------------
                |   allocate new attribute node
                \--------------------------------------------------*/
                h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_attrNames),
                                        ArgName, &hnew);
                attrnode = (domAttrNode*) domAlloc(sizeof(domAttrNode));
                memset(attrnode, 0, sizeof(domAttrNode));
                attrnode->parentNode  = node;
                attrnode->nodeName    = (char *)&(h->key);
                attrnode->nodeType    = ATTRIBUTE_NODE;
                attrnode->nodeValue   = (char*)MALLOC(nArgVal+1);
                attrnode->valueLength = nArgVal;
                memmove(attrnode->nodeValue, ArgVal, nArgVal);
                *(attrnode->nodeValue + nArgVal) = 0;
                if (ampersandSeen) {
                    TranslateEntityRefs(attrnode->nodeValue, 
                                        &(attrnode->valueLength) );
                }
                if (!strcmp(ArgName, "id")) {
                    if (!doc->ids) {
                        doc->ids = 
                            (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
                        Tcl_InitHashTable (doc->ids, TCL_STRING_KEYS);
                    }
                    h = Tcl_CreateHashEntry (doc->ids, attrnode->nodeValue,
                                             &hnew);
                    /* How to resolve in case of dublicates?  We
                       follow, what the core dom building code does:
                       the first value in document order wins. */
                    if (hnew) {
                        Tcl_SetHashValue (h, node);
                        attrnode->nodeFlags |= IS_ID_ATTRIBUTE;
                    }
                }
                if (node->firstAttr) {
                    lastAttr->nextSibling = attrnode;
                } else {
                    node->firstAttr = attrnode;
                }
                lastAttr = attrnode;

                *(ArgName + nArgName) = saved;

                while (SPACE(*x)) {
                    x++;
                }
            }

            /*-----------------------------------------------------------
            |   check for empty HTML tags
            \----------------------------------------------------------*/
            switch (node->nodeName[0]) {
            case 'a':  if (!strcmp(node->nodeName,"area"))     {hasContent = 0;} break;
            case 'b':  if (!strcmp(node->nodeName,"br")     ||
                           !strcmp(node->nodeName,"base")   ||
                           !strcmp(node->nodeName,"basefont")) {hasContent = 0;} break;
            case 'c':  if (!strcmp(node->nodeName,"col"))      {hasContent = 0;} break;
            case 'e':  if (!strcmp(node->nodeName,"embed"))    {hasContent = 0;} break;
            case 'f':  if (!strcmp(node->nodeName,"frame"))    {hasContent = 0;} break;
            case 'h':  if (!strcmp(node->nodeName,"hr"))       {hasContent = 0;} break;
            case 'i':  if (!strcmp(node->nodeName,"img")   ||
                           !strcmp(node->nodeName,"input") ||
                           !strcmp(node->nodeName,"isindex"))  {hasContent = 0;} break;
            case 'l':  if (!strcmp(node->nodeName,"link"))     {hasContent = 0;} break;
            case 'm':  if (!strcmp(node->nodeName,"meta"))     {hasContent = 0;} break;
            case 'p':  if (!strcmp(node->nodeName,"param"))    {hasContent = 0;} break;
            case 's':  if (!strcmp(node->nodeName,"spacer") || 
                           !strcmp(node->nodeName,"source"))   {hasContent = 0;} break; /*html5*/
            }

            if (*x=='/') {
                hasContent = 0;
                x++;
                if (*x!='>') {
                    RetError("Syntax Error",(x - html - 1) );
                }
            }
            if (*x=='>') {
                x++;
            }
            DBG(fprintf(stderr, "%d: new node '%s' hasContent=%d \n", getDeep(node), node->nodeName, hasContent);)

            if ((strcmp(node->nodeName,"style" )==0) ||
                (strcmp(node->nodeName,"script")==0)
            ) {
                /*-----------------------------------------------------------
                |   read over any data within a 'style' or 'script' tag
                \----------------------------------------------------------*/
                hasContent = 1;
                start = x;
                while (1) {
                    while ( (*x!=0) && ((*x!='<') || (x[1]!='/'))) {
                        x++;
                    }
                    if (!*x) break;
                    y = x + 2;
                    while (*y!=0 && SPACE(*y)) y++;
                    if (TU(y[0]) == 'S' && 
                        TU(y[1]) == 'C' &&
                        TU(y[2]) == 'R' &&
                        TU(y[3]) == 'I' &&
                        TU(y[4]) == 'P' &&
                        TU(y[5]) == 'T' )  break;
                         
                    if (TU(y[0]) == 'S' && 
                        TU(y[1]) == 'T' &&
                        TU(y[2]) == 'Y' &&
                        TU(y[3]) == 'L' &&
                        TU(y[4]) == 'E' ) break;
                    x++;                                      
                }
                if (*x) {
                    /*----------------------------------------------------
                    |   allocate new TEXT node for style/script data
                    \---------------------------------------------------*/
                    tnode = (domTextNode*) domAlloc(sizeof(domTextNode));
                    memset(tnode, 0, sizeof(domTextNode));
                    tnode->nodeType      = TEXT_NODE;
                    tnode->ownerDocument = doc;
                    tnode->nodeNumber    = NODE_NO(doc);
                    tnode->parentNode    = node;
                    tnode->valueLength   = (x - start);
                    tnode->nodeValue     = (char*)MALLOC((x - start)+1);
                    memmove(tnode->nodeValue, start, (x - start));
                    *(tnode->nodeValue + (x - start)) = 0;
                    if (node->firstChild)  {
                        node->lastChild->nextSibling = (domNode*)tnode;
                        tnode->previousSibling = node->lastChild;
                        node->lastChild = (domNode*)tnode;
                    } else {
                        node->firstChild = node->lastChild = (domNode*)tnode;
                    }
                }
            }
            if (hasContent) {
                /*------------------------------------------------------------
                |   recurs to read child tags/texts
                \-----------------------------------------------------------*/
                parent_node = node;
            }
            DBG(fprintf(stderr, "%d: after node '%s' \n", getDeep(node), node->nodeName);)
        }
    }

    while (parent_node != NULL && node->parentNode) {
        pn = (char*)node->parentNode->nodeName;
        DBG(fprintf(stderr, "final autoclose '%s'? \n", pn);)
        /*---------------------------------------------------------------
        |   check for tags for which end tag can be omitted
        \--------------------------------------------------------------*/
        autoclose = 0;
        switch (pn[0]) {
        case 'b': if (!strcmp(pn,"body"))     {autoclose = 1;} break;
        case 'c': if (!strcmp(pn,"colgroup")) {autoclose = 1;} break;
        case 'd': if (!strcmp(pn,"dd") ||
                      !strcmp(pn,"dt"))       {autoclose = 1;} break;
        case 'h': if (!strcmp(pn,"head") ||
                      !strcmp(pn,"html"))     {autoclose = 1;} break;
        case 'l': if (!strcmp(pn,"li"))       {autoclose = 1;} break;
        case 'o': if (!strcmp(pn,"option"))   {autoclose = 1;} break;
        case 'p': if (!strcmp(pn,"p"))        {autoclose = 1;} break;
        case 't': if (!strcmp(pn,"tbody") ||
                      !strcmp(pn,"td")    ||
                      !strcmp(pn,"tfoot") ||
                      !strcmp(pn,"thead") ||
                      !strcmp(pn,"th")    ||
                      !strcmp(pn,"tr"))       {autoclose = 1;} break;
        case 'u': if (!strcmp(pn,"ul"))       {autoclose = 1;} break; /* ext */
        }
        if (!autoclose) break;
        DBG(fprintf(stderr, "final autoclosed '%s'! \n", pn);)
        node = node->parentNode;
        parent_node = node->parentNode;
    }
    if (parent_node == NULL) {
        /* we return to main node and so finished parsing */
        return TCL_OK;
    }
    if (forest && parent_node == parent) {
        return TCL_OK;
    }
    RetError("Unexpected end",(x - html) );

} /* HTML_SimpleParse */


/*----------------------------------------------------------------------------
|   HTML_SimpleParseDocument
|
|       Create a document, parses the HTML string starting at 'pos' and
|       continuing to the first encountered error.
|
\---------------------------------------------------------------------------*/
domDocument *
HTML_SimpleParseDocument (
    char   *html,              /* Complete text of the file being parsed  */
    int     ignoreWhiteSpaces,
    int     forest,
    domLength *pos,
    char  **errStr
) {
    domDocument   *doc = domCreateDoc(NULL, 0);
    domNode *save, *node = NULL;
    Tcl_HashEntry *h;
    int hnew;

    if (forest) {
        // Create umbrella tag
        h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_tagNames), "forestroot",
                                &hnew);
        node = (domNode*) domAlloc(sizeof(domNode));
        memset(node, 0, sizeof(domNode));
        node->nodeType      = ELEMENT_NODE;
        node->nodeName      = (char *)&(h->key);
        node->ownerDocument = doc;
        doc->rootNode->firstChild = node;
        doc->rootNode->lastChild = node;
    }

    *pos = 0;
    HTML_SimpleParse (html, pos, doc, node, ignoreWhiteSpaces, forest,
                      errStr);
    if (forest) {
        doc->rootNode->firstChild = node->firstChild;
        doc->rootNode->lastChild = node->lastChild;
        save = node;
        for (node = doc->rootNode->firstChild;
             node != NULL;
             node = node->nextSibling) {
            node->parentNode = NULL;
        }
        domFree ((void*)save);
    }
    domSetDocumentElement (doc);
    return doc;

} /* HTML_SimpleParseDocument */

