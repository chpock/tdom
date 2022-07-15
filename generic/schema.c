/*----------------------------------------------------------------------------
|   Copyright (c) 2018, 2019  Rolf Ade (rolf@pointsman.de)
|-----------------------------------------------------------------------------
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
|   Contributor(s):
|
|
|   written by Rolf Ade
|   2018-2020
|
\---------------------------------------------------------------------------*/

#include <tdom.h>
#include <tcldom.h>
#include <domxpath.h>
#include <schema.h>

#ifndef TDOM_NO_SCHEMA

#include <inttypes.h>
#include <fcntl.h>

#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#endif

/* #define DEBUG */
/* #define DDEBUG */
/*----------------------------------------------------------------------------
|   Debug Macros
|
\---------------------------------------------------------------------------*/
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x)
#endif
#if defined(DEBUG) || defined(DDEBUG)
# define DDBG(x) x
#else
# define DDBG(x)
#endif


/*----------------------------------------------------------------------------
| Choice/attribute handling method threshold
|
\---------------------------------------------------------------------------*/
#ifndef TDOM_CHOICE_HASH_THRESHOLD
# define TDOM_CHOICE_HASH_THRESHOLD 5
#endif
#ifndef TDOM_ATTRIBUTE_HASH_THRESHOLD
# define TDOM_ATTRIBUTE_HASH_THRESHOLD 5
#endif
#ifndef TDOM_EXPAT_READ_SIZE
# define TDOM_EXPAT_READ_SIZE (1024*8)
#endif

/*----------------------------------------------------------------------------
|   Initial buffer sizes
|
\---------------------------------------------------------------------------*/
#ifndef CONTENT_ARRAY_SIZE_INIT
#  define CONTENT_ARRAY_SIZE_INIT 20
#endif
#ifndef ANON_PATTERN_ARRAY_SIZE_INIT
#  define ANON_PATTERN_ARRAY_SIZE_INIT 256
#endif
#ifndef URI_BUFFER_LEN_INIT
#  define URI_BUFFER_LEN_INIT 128
#endif
#ifndef ATTR_ARRAY_INIT
#  define ATTR_ARRAY_INIT 4
#endif
#ifndef WHITESPACETC_BUFFER_LEN_INIT
#  define WHITESPACETC_BUFFER_LEN_INIT 200
#endif

/*----------------------------------------------------------------------------
|   Local defines
|
\---------------------------------------------------------------------------*/
#ifndef O_BINARY
# ifdef _O_BINARY
#  define O_BINARY _O_BINARY
# else
#  define O_BINARY 0
# endif
#endif
#if !defined(PTR2UINT)
# if defined(HAVE_UINTPTR_T) || defined(uintptr_t)
#  define PTR2UINT(p) ((unsigned int)(uintptr_t)(p))
# else
#  define PTR2UINT(p) ((unsigned int)(p))
# endif
#endif
#if !defined(UINT2PTR)
# if defined(HAVE_UINTPTR_T) || defined(uintptr_t)
#  define UINT2PTR(p) ((void *)(uintptr_t)(p))
# else
#  define UINT2PTR(p) ((void *)(p))
# endif
#endif
#ifndef TCL_MATCH_NOCASE
# define TCL_MATCH_NOCASE 1
#endif

/*----------------------------------------------------------------------------
|   Local typedefs
|
\---------------------------------------------------------------------------*/

typedef struct
{
    SchemaData    *sdata;
    Tcl_Interp    *interp;
    XML_Parser     parser;
    Tcl_DString   *cdata;
    int            onlyWhiteSpace;
    char          *uri;
    int            maxUriLen;
    Tcl_Obj       *externalentitycommandObj;
} ValidateMethodData;

typedef enum {
    MATCH_GLOBAL = 1,
    MATCH_ELEMENT_START,
    MATCH_ELEMENT_END,
    MATCH_TEXT,
    MATCH_ATTRIBUTE_TEXT,
    MATCH_DOM_KEYCONSTRAINT,
    MATCH_DOM_XPATH_BOOLEAN
} ValidationAction;

static char *ValidationAction2str[] = {
    "NOT_USED",
    "MATCH_GLOBAL",
    "MATCH_ELEMENT_START",
    "MATCH_ELEMENT_END",
    "MATCH_TEXT",
    "MATCH_ATTRIBUTE_TEXT",
    "MATCH_DOM_KEYCONSTRAINT",
    "MATCH_DOM_XPATH_BOOLEAN"
};
    
typedef enum {
    DOM_KEYCONSTRAINT,
    DOM_XPATH_BOOLEAN,
    MISSING_ATTRIBUTE,
    MISSING_ELEMENT_MATCH_START,
    MISSING_ELEMENT_MATCH_END,
    UNEXPECTED_TEXT,
    MISSING_TEXT_MATCH_START,
    MISSING_TEXT_MATCH_END,
    UNEXPECTED_ROOT_ELEMENT,
    UNEXPECTED_ELEMENT,
    UNKNOWN_ATTRIBUTE,
    INVALID_KEYREF_MATCH_START,
    INVALID_KEYREF_MATCH_END,
    INVALID_KEYREF_MATCH_TEXT,
    UNKNOWN_ROOT_ELEMENT,
    UNKNOWN_GLOBAL_ID,
    UNKNOWN_ID,
    INVALID_ATTRIBUTE_VALUE,
    INVALID_VALUE
} ValidationErrorType;

static char *ValidationErrorType2str[] = {
    "DOM_KEYCONSTRAINT",
    "DOM_XPATH_BOOLEAN",
    "MISSING_ATTRIBUTE",
    "MISSING_ELEMENT",
    "MISSING_ELEMENT",
    "UNEXPECTED_TEXT",
    "MISSING_TEXT",
    "MISSING_TEXT",
    "UNEXPECTED_ROOT_ELEMENT",
    "UNEXPECTED_ELEMENT",
    "UNKNOWN_ATTRIBUTE",
    "INVALID_KEYREF",
    "INVALID_KEYREF",
    "INVALID_KEYREF",
    "UNKNOWN_ROOT_ELEMENT",
    "UNKNOWN_GLOBAL_ID",
    "UNKNOWN_ID",
    "INVALID_ATTRIBUTE_VALUE",
    "INVALID_VALUE"
};

typedef enum {
    VALIDATE_STRING,
    VALIDATE_FILENAME,
    VALIDATE_CHANNEL
} ValidationInput;

/*----------------------------------------------------------------------------
|   Recovering related flage
|
\---------------------------------------------------------------------------*/

#define RECOVER_FLAG_REWIND 1
#define RECOVER_FLAG_DONT_REPORT 2
#define RECOVER_FLAG_IGNORE 4
#define RECOVER_FLAG_MATCH_END_CONTINUE 8

/*----------------------------------------------------------------------------
|   [schemacmd info expected] related flags
|
\---------------------------------------------------------------------------*/

#define EXPECTED_IGNORE_MATCHED 1
#define EXPECTED_ONLY_MANDATORY 2
#define EXPECTED_PROBE_MAYSKIP 4

/*----------------------------------------------------------------------------
|   domKeyConstraint related flage
|
\---------------------------------------------------------------------------*/

#define DKC_FLAG_IGNORE_EMPTY_FIELD_SET 1
#define DKC_FLAG_BOOLEAN 2

/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)
#define SetResultV(str) if (!sdata->evalError) { \
        Tcl_ResetResult(interp);                                        \
        Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1);          \
    }
#define SetResult3(str1,str2,str3) Tcl_ResetResult(interp);     \
                     Tcl_AppendResult(interp, (str1), (str2), (str3), NULL)
#define SetResult3V(str1,str2,str3) if (!sdata->evalError) {            \
        Tcl_ResetResult(interp);                                        \
        Tcl_AppendResult(interp, (str1), (str2), (str3), NULL);         \
    }
#define SetIntResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetIntObj(Tcl_GetObjResult(interp), (i))
#define SetLongResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetLongObj(Tcl_GetObjResult(interp), (i))
#define SetBooleanResult(i) Tcl_ResetResult(interp); \
                     Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (i))

#define SPACE(c) IS_XML_WHITESPACE ((c))
    
#define checkNrArgs(l,h,err) if (objc < l || objc > h) {      \
        SetResult (err);                                      \
        return TCL_ERROR;                                     \
    }

#if defined(DEBUG) || defined(DDEBUG)
static char *Schema_CP_Type2str[] = {
    "ANY",
    "NAME",
    "CHOICE",
    "INTERLEAVE",
    "PATTERN",
    "TEXT",
    "VIRTUAL",
    "KEYSPACE_START",
    "KEYSPACE_END"
};
static char *Schema_Quant_Type2str[] = {
    "ONE",
    "OPT",
    "REP",
    "PLUS",
    "NM",
    "ERROR"
};
#endif


#ifndef TCL_THREADS
  static SchemaData *activeSchemaData = 0;
# define GETASI activeSchemaData
# define SETASI(v) activeSchemaData = v
#else
  static Tcl_ThreadDataKey activeSchemaData;
# define GETASI  *(SchemaData**) Tcl_GetThreadData(&activeSchemaData, \
                                                     sizeof(SchemaData*))
static void SetActiveSchemaData (SchemaData *v)
{
    SchemaData **schemaInfoPtr = Tcl_GetThreadData(&activeSchemaData,
                                                        sizeof (SchemaData*));
    *schemaInfoPtr = v;
}
# define SETASI(v) SetActiveSchemaData (v)
#endif

#define CHECK_SI                                                        \
    if (!sdata) {                                                       \
        SetResult ("Command called outside of schema context");         \
        return TCL_ERROR;                                               \
    }                                                                   \
    if (sdata->isTextConstraint) {                                      \
        SetResult ("Command called in invalid schema context");         \
        return TCL_ERROR;                                               \
    }

#define CHECK_TI                                                        \
    if (!sdata) {                                                       \
        SetResult ("Command called outside of schema context");         \
        return TCL_ERROR;                                               \
    }                                                                   \
    if (!sdata->isTextConstraint) {                                     \
        SetResult ("Command called in invalid schema context");         \
        return TCL_ERROR;                                               \
    }

#define CHECK_TOPLEVEL                                                  \
    if (sdata->defineToplevel) {                                        \
        SetResult("Command not allowed at top level "                   \
                  "in schema define evaluation");                       \
        return TCL_ERROR;                                               \
    }

#define CHECK_RECURSIVE_CALL                                            \
    if (clientData != NULL) {                                           \
        savedsdata = GETASI;                                            \
        if (savedsdata == sdata) {                                      \
            SetResult ("This recursive call is not allowed");           \
            return TCL_ERROR;                                           \
        }                                                               \
    } else if (!sdata->defineToplevel) {                                \
        SetResult ("Command only allowed at lop level");                \
        return TCL_ERROR;                                               \
    }
      
#define CHECK_EVAL                                                      \
    if (sdata->currentEvals) {                                          \
        SetResult ("This method is not allowed in nested evaluation");  \
        return TCL_ERROR;                                               \
    }

#define CHECK_REWIND                                                    \
    if (sdata->recoverFlags & RECOVER_FLAG_REWIND) {                    \
        rewindStack (sdata);                                            \
        sdata->recoverFlags &= ~RECOVER_FLAG_REWIND;                    \
    }                                                                   \

#define REMEMBER_PATTERN(pattern)                                       \
    if (sdata->numPatternList == sdata->patternListSize) {              \
        sdata->patternList = (SchemaCP **) REALLOC (                    \
            sdata->patternList,                                         \
            sizeof (SchemaCP*) * sdata->patternListSize * 2);           \
        sdata->patternListSize *= 2;                                    \
    }                                                                   \
    sdata->patternList[sdata->numPatternList] = pattern;                \
    sdata->numPatternList++;


#define ADD_CONSTRAINT(sdata, sc)                                       \
    sc = TMALLOC (SchemaConstraint);                                    \
    memset (sc, 0, sizeof (SchemaConstraint));                          \
    if (sdata->cp->nc == sdata->contentSize) {                          \
        sdata->cp->content =                                            \
            REALLOC (sdata->cp->content,                                \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaCP*));                             \
        sdata->cp->quants =                                             \
            REALLOC (sdata->cp->quants,                                 \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaQuant));                           \
        sdata->contentSize *= 2;                                        \
    }                                                                   \
    sdata->cp->content[sdata->cp->nc] = (SchemaCP *) sc;                \
    sdata->cp->quants[sdata->cp->nc] = SCHEMA_CQUANT_ONE;               \
    sdata->cp->nc++;                                                    \

#define maxOne(quant) \
    ((quant) == SCHEMA_CQUANT_ONE || (quant) == SCHEMA_CQUANT_OPT) ? 1 : 0

#define minOne(quant) \
    ((quant) == SCHEMA_CQUANT_ONE || (quant) == SCHEMA_CQUANT_PLUS) ? 1 : 0

#define mayRepeat(quant) \
    ((quant) == SCHEMA_CQUANT_REP || (quant) == SCHEMA_CQUANT_PLUS) ? 1 : 0

#define mayMiss(quant) \
    ((quant) == SCHEMA_CQUANT_REP || (quant) == SCHEMA_CQUANT_OPT) ? 1 : 0

#define hasMatched(quant,hm) \
    (hm) == 0 ?  mayMiss(quant) :  1

#define mustMatch(quant,hm) \
    (hm) == 0 ? minOne(quant) : 0


#define getContext(cp, ac, hm)                    \
    cp = se->pattern;                             \
    ac = se->activeChild;                         \
    hm = se->hasMatched;                          \
    if (hm && maxOne (cp->quants[ac])) {          \
        ac += + 1;                                \
        hm = 0;                                   \
    }                                             \


#define updateStack(sdata,se,ac)                        \
    if (!(sdata->recoverFlags & RECOVER_FLAG_REWIND)) { \
        se->activeChild = ac;                           \
        se->hasMatched = 1;                             \
    }                                                   \


static const char *unknownNS = "<unknownNamespace";
static char *emptyStr = "";


#ifndef TCL_THREADS
SchemaData *
tdomGetSchemadata (Tcl_Interp *interp) 
{
    return activeSchemaData;
}
#else
SchemaData *
tdomGetSchemadata (void) 
{
    return GETASI;
}
#endif

static SchemaCP*
initSchemaCP (
    Schema_CP_Type type,
    void *namespace,
    char *name
    )
{
    SchemaCP *pattern;

    pattern = TMALLOC (SchemaCP);
    memset (pattern, 0, sizeof(SchemaCP));
    pattern->type = type;
    switch (type) {
    case SCHEMA_CTYPE_NAME:
        pattern->flags |= CONSTRAINT_TEXT_CHILD;
        /* Fall through. */
    case SCHEMA_CTYPE_PATTERN:
        pattern->namespace = (char *)namespace;
        pattern->name = name;
        /* Fall through. */
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_INTERLEAVE:
        pattern->content = (SchemaCP**) MALLOC (
            sizeof(SchemaCP*) * CONTENT_ARRAY_SIZE_INIT
            );
        pattern->quants = (SchemaQuant*) MALLOC (
            sizeof (SchemaQuant) * CONTENT_ARRAY_SIZE_INIT
            );
        break;
    case SCHEMA_CTYPE_TEXT:
        /* content/quant will be allocated, if the cp in fact has
         * constraints */
        break;
    case SCHEMA_CTYPE_KEYSPACE_END:
    case SCHEMA_CTYPE_KEYSPACE:
        pattern->name = name;
        break;
    case SCHEMA_CTYPE_ANY:
        pattern->namespace = namespace;
        break;
    case SCHEMA_CTYPE_VIRTUAL:
        /* Do nothing */
        break;
    }
    return pattern;
}

DDBG(
static void serializeCP (
    SchemaCP *pattern
    )
{
    fprintf (stderr, "CP %p type: %s\n",
             pattern, Schema_CP_Type2str[pattern->type]);
    switch (pattern->type) {
    case SCHEMA_CTYPE_KEYSPACE_END:
    case SCHEMA_CTYPE_KEYSPACE:
        fprintf (stderr, "\tName: '%s'\n", pattern->name);
        break;
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
        fprintf (stderr, "\tName: '%s' Namespace: '%s'\n",
                 pattern->name, pattern->namespace);
        if (pattern->flags & FORWARD_PATTERN_DEF) {
            fprintf (stderr, "\tAnonymously defined NAME\n");
        }
        if (pattern->flags & PLACEHOLDER_PATTERN_DEF) {
            fprintf (stderr, "\tAs placeholder defined NAME\n");
        }
        if (pattern->flags & LOCAL_DEFINED_ELEMENT) {
            fprintf (stderr, "\tLocal defined NAME\n");
        }
        if (pattern->flags & ELEMENTTYPE_DEF) {
            fprintf (stderr, "\tElementtype '%s'\n", pattern->name);
        }
        if (pattern->flags & TYPED_ELEMENT) {
            fprintf (stderr, "\tTyped element - type '%s'\n",
                     pattern->typeptr->name);
        }
        /* Fall through. */
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_INTERLEAVE:
        fprintf (stderr, "\t%d children\n", pattern->nc);
        break;
    case SCHEMA_CTYPE_ANY:
        if (pattern->namespace) {
            fprintf (stderr, "\tNamespace: '%s'\n",
                     pattern->namespace);
        }
        if (pattern->typedata) {
            fprintf (stderr, "\t%d namespaces\n",
                     (Tcl_HashTable*)pattern->typedata->numEntries);
        }            
        break;
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_VIRTUAL:
        /* Do nothing */
        break;
    }
}

static void serializeQuant (
    SchemaQuant quant
    )
{
    fprintf (stderr, "Quant type: %s\n",
             Schema_Quant_Type2str[quant]);
}

static int getDeep (
    SchemaValidationStack *se
    )
{
    int i = 0;
    while (se) {
        if (se->pattern->type == SCHEMA_CTYPE_NAME) i++;
        se = se->down;
    }
    return i;
}
    
static void serializeStack (
    SchemaData *sdata
    )
{
    SchemaValidationStack *se;

    fprintf (stderr, "++++ Current validation stack:\n");
    se = sdata->stack;
    while (se) {
        serializeCP (se->pattern);
        fprintf (stderr, "\tdeep: %d ac: %d hm: %d\n",
                 getDeep (se), se->activeChild, se->hasMatched);
        se = se->down;
    }
    fprintf (stderr, "++++ Stack bottom\n");
}
)

/* DBG end */


static void freedomKeyConstraints (
    domKeyConstraint *kc
    )
{
    domKeyConstraint *knext;
    int i;

    while (kc) {
        knext = kc->next;
        if (kc->name) FREE (kc->name);
        if (kc->emptyFieldSetValue) FREE (kc->emptyFieldSetValue);
        xpathFreeAst (kc->selector);
        for (i = 0; i < kc->nrFields; i++) {
            xpathFreeAst (kc->fields[i]);
        }
        FREE (kc->fields);
        FREE (kc);
        kc = knext;
    }
}

static void freeSchemaCP (
    SchemaCP *pattern
    )
{
    unsigned int i;
    SchemaConstraint *sc;
    
    switch (pattern->type) {
    case SCHEMA_CTYPE_ANY:
        if (pattern->typedata) {
            Tcl_DeleteHashTable ((Tcl_HashTable *) pattern->typedata);
            FREE (pattern->typedata);
        }
        break;
    case SCHEMA_CTYPE_VIRTUAL:
        for (i = 0; i < pattern->nc; i++) {
            Tcl_DecrRefCount ((Tcl_Obj *)pattern->content[i]);
        }
        FREE (pattern->content);
        break;
    case SCHEMA_CTYPE_TEXT:
        for (i = 0; i < pattern->nc; i++) {
            sc = (SchemaConstraint *) pattern->content[i];
            if (sc->freeData) {
                (sc->freeData) (sc->constraintData);
            }
            FREE (pattern->content[i]);
        }
        /* Fall throu */
    default:
        if (pattern->flags & TYPED_ELEMENT) break;
        FREE (pattern->content);
        FREE (pattern->quants);
        if (pattern->attrs) {
            for (i = 0; i < pattern->numAttr; i++) {
                FREE (pattern->attrs[i]);
            }
            FREE (pattern->attrs);
        }
        freedomKeyConstraints (pattern->domKeys);
        if (pattern->typedata) {
            Tcl_DeleteHashTable ((Tcl_HashTable *) pattern->typedata);
            FREE (pattern->typedata);
        }
        break;
    }
    if (pattern->defScript) {
        Tcl_DecrRefCount (pattern->defScript);
    }
    if (pattern->associated) {
        Tcl_DecrRefCount (pattern->associated);
    }
    FREE (pattern);
}

static SchemaData*
initSchemaData (
    Tcl_Obj *cmdNameObj)
{
    SchemaData *sdata;
    int len;
    char *name;

    sdata = TMALLOC (SchemaData);
    memset (sdata, 0, sizeof(SchemaData));
    name = Tcl_GetStringFromObj (cmdNameObj, &len);
    sdata->self = Tcl_NewStringObj (name, len);
    Tcl_IncrRefCount (sdata->self);
    Tcl_InitHashTable (&sdata->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->elementType, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->elementTypeInstance, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable (&sdata->prefix, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->attrNames, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->textDef, TCL_STRING_KEYS);
    sdata->patternList = (SchemaCP **) MALLOC (
        sizeof (SchemaCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->patternListSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    /* evalStub initialization */
    sdata->evalStub = (Tcl_Obj **) MALLOC (sizeof (Tcl_Obj*) * 4);
    sdata->evalStub[0] = Tcl_NewStringObj("::namespace", 11);
    Tcl_IncrRefCount (sdata->evalStub[0]);
    sdata->evalStub[1] = Tcl_NewStringObj("eval", 4);
    Tcl_IncrRefCount (sdata->evalStub[1]);
    sdata->evalStub[2] = Tcl_NewStringObj("::tdom::schema", 14);
    Tcl_IncrRefCount (sdata->evalStub[2]);
    /* textStub initialization */
    sdata->textStub = (Tcl_Obj **) MALLOC (sizeof (Tcl_Obj*) * 4);
    sdata->textStub[0] = Tcl_NewStringObj("::namespace", 11);
    Tcl_IncrRefCount (sdata->textStub[0]);
    sdata->textStub[1] = Tcl_NewStringObj("eval", 4);
    Tcl_IncrRefCount (sdata->textStub[1]);
    sdata->textStub[2] = Tcl_NewStringObj("::tdom::schema::text", 20);
    Tcl_IncrRefCount (sdata->textStub[2]);
    sdata->cdata = TMALLOC (Tcl_DString);
    Tcl_DStringInit (sdata->cdata);
    Tcl_InitHashTable (&sdata->ids, TCL_STRING_KEYS);
    sdata->unknownIDrefs = 0;
    Tcl_InitHashTable (&sdata->idTables, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->keySpaces, TCL_STRING_KEYS);
    sdata->choiceHashThreshold = TDOM_CHOICE_HASH_THRESHOLD;
    sdata->attributeHashThreshold = TDOM_ATTRIBUTE_HASH_THRESHOLD;
    return sdata;
}

static void schemaInstanceDelete (
    ClientData clientData
    )
{
    SchemaData *sdata = (SchemaData *) clientData;
    unsigned int i;
    SchemaValidationStack *down;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaDocKey *dk;
    SchemaKeySpace *ks;

    /* Protect the clientData to be freed inside (even nested)
     * Tcl_Eval*() calls to avoid invalid mem access and postpone the
     * cleanup until the Tcl_Eval*() calls are finished (done in
     * tDOM_schemaInstanceCmd()). */
    if (sdata->currentEvals || sdata->inuse > 0) {
        sdata->cleanupAfterUse = 1;
        return;
    }
    Tcl_DecrRefCount (sdata->self);
    if (sdata->start) FREE (sdata->start);
    if (sdata->prefixns) {
        i = 0;
        while (sdata->prefixns[i]) {
            FREE (sdata->prefixns[i]);
            i++;
        }
        FREE (sdata->prefixns);
    }
    Tcl_DeleteHashTable (&sdata->namespace);
    Tcl_DeleteHashTable (&sdata->element);
    Tcl_DeleteHashTable (&sdata->elementType);
    Tcl_DeleteHashTable (&sdata->elementTypeInstance);
    Tcl_DeleteHashTable (&sdata->prefix);
    Tcl_DeleteHashTable (&sdata->pattern);
    Tcl_DeleteHashTable (&sdata->attrNames);
    Tcl_DeleteHashTable (&sdata->textDef);
    for (i = 0; i < sdata->numPatternList; i++) {
        freeSchemaCP (sdata->patternList[i]);
    }
    FREE (sdata->patternList);
    FREE (sdata->quants);
    while (sdata->stack) {
        down = sdata->stack->down;
        if (sdata->stack->interleaveState)
            FREE (sdata->stack->interleaveState);
        FREE (sdata->stack);
        sdata->stack = down;
    }
    while (sdata->lastMatchse) {
        down = sdata->lastMatchse->down;
        if (sdata->lastMatchse->interleaveState)
            FREE (sdata->lastMatchse->interleaveState);
        FREE (sdata->lastMatchse);
        sdata->lastMatchse = down;
    }
    while (sdata->stackPool) {
        down = sdata->stackPool->down;
        /* interleaveState always got cleand up at putting se back to
         * pool */
        FREE (sdata->stackPool);
        sdata->stackPool = down;
    }
    Tcl_DecrRefCount (sdata->evalStub[0]);
    Tcl_DecrRefCount (sdata->evalStub[1]);
    Tcl_DecrRefCount (sdata->evalStub[2]);
    FREE (sdata->evalStub);
    Tcl_DecrRefCount (sdata->textStub[0]);
    Tcl_DecrRefCount (sdata->textStub[1]);
    Tcl_DecrRefCount (sdata->textStub[2]);
    FREE (sdata->textStub);
    Tcl_DStringFree (sdata->cdata);
    FREE (sdata->cdata);
    if (sdata->reportCmd) {
        Tcl_DecrRefCount (sdata->reportCmd);
    }
    Tcl_DeleteHashTable (&sdata->ids);
    for (h = Tcl_FirstHashEntry (&sdata->idTables, &search);
         h != NULL;
         h = Tcl_NextHashEntry (&search)) {
        dk = Tcl_GetHashValue (h);
        Tcl_DeleteHashTable (&dk->ids);
        FREE (dk);
    }
    Tcl_DeleteHashTable (&sdata->idTables);
    for (h = Tcl_FirstHashEntry (&sdata->keySpaces, &search);
         h != NULL;
         h = Tcl_NextHashEntry (&search)) {
        ks = Tcl_GetHashValue (h);
        if (ks->active) {
            Tcl_DeleteHashTable (&ks->ids);
        }
        FREE (ks);
    }
    Tcl_DeleteHashTable (&sdata->keySpaces);
    if (sdata->wsbufLen) {
        FREE (sdata->wsbuf);
    }
    FREE (sdata);
}

static void
cleanupLastPattern (
    SchemaData *sdata,
    unsigned int from
    )
{
    unsigned int i, j, k, freed, isElement;
    char *name;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *h, *h1;
    SchemaCP *this, *previous, *current, *typePattern, *this1;

    for (i = from; i < sdata->numPatternList; i++) {
        this = sdata->patternList[i];
        hashTable = NULL;
        isElement = 0;
        name = this->name;
        if (this->type == SCHEMA_CTYPE_NAME) {
            /* Local defined elements aren't saved under their local
             * name bucket in the sdata->element hash table. */
            if (this->flags & LOCAL_DEFINED_ELEMENT) {
                freeSchemaCP (sdata->patternList[i]);
                continue;
            }
            if (this->flags & ELEMENTTYPE_DEF) {
                hashTable = &sdata->elementType;
            } else if (this->flags & TYPED_ELEMENT) {
                hashTable = &sdata->elementTypeInstance;
                if (this->flags & HASH_ENTRY_DELETED) {
                    freeSchemaCP (sdata->patternList[i]);
                    continue;
                }
            } else {
                hashTable = &sdata->element;
                isElement = 1;
            }
        } else if (this->type == SCHEMA_CTYPE_PATTERN) {
            hashTable = &sdata->pattern;
        }
        if (name && hashTable) {
            if (this->flags & FORWARD_PATTERN_DEF) {
                sdata->forwardPatternDefs--;
            }
            h = Tcl_FindHashEntry (hashTable, name);
            if (h) {
                previous = NULL;
                current = Tcl_GetHashValue (h);
                while (current != NULL && current != this) {
                    previous = current;
                    current = current->next;
                }
                if (current) {
                    if (previous) {
                        if (current->next) {
                            previous->next = current->next;
                        } else {
                            previous->next = NULL;
                        }
                    } else  {
                        if (current->next) {
                            Tcl_SetHashValue (h, current->next);
                        } else {
                            if (isElement) {
                                /* Typed elements with the same name
                                 * use the pointer to the key string
                                 * of the below deleted hash entry in
                                 * the name element of the pattern
                                 * structure. Additionally the pointer
                                 * to the key string is used as key in
                                 * the elementTypeInstance hash table.
                                 * We mark every element instance
                                 * pattern stored in
                                 * elementTypeInstance under this key
                                 * and delete the entry. */
                                h1 = Tcl_FindHashEntry (
                                    &sdata->elementTypeInstance, name
                                    );
                                if (h1) {
                                    this1 = (SchemaCP *) Tcl_GetHashValue (h1);
                                    while (this1) {
                                        this1->flags |= HASH_ENTRY_DELETED;
                                        this1 = this1->next;
                                    }
                                    Tcl_DeleteHashEntry (h1);
                                }
                            }
                            Tcl_DeleteHashEntry (h);
                        }
                    }
                }
            }
            if (this->flags & TYPED_ELEMENT) {
                /* If the type of the element is forward defined, then
                 * the pointer to the below freed cp is noted in the
                 * forward type definition. Clean that up. */
                /* First step: Check, if the typePtr is still valid. */
                freed = 0;
                for (j = from; j < i; j++) {
                    if (this->typeptr == sdata->patternList[j]) {
                        freed = 1;
                        break;
                    }
                }
                if (!freed) {
                    typePattern = this->typeptr;
                    if (typePattern->flags & FORWARD_PATTERN_DEF) {
                        for (k = 0; k < typePattern->nc; k++) {
                            if (this == typePattern->content[k]) {
                                /* Later noted pattern are allocated
                                 * later than this and will be freed
                                 * later on in this cleanupLastPattern
                                 * call, so this is save and
                                 * efficient. */
                                typePattern->nc = k;
                                break;
                            }
                        }
                    }
                }
            }
        }
        freeSchemaCP (sdata->patternList[i]);
    }
    sdata->numPatternList = from;
}

static void
addToContent (
    SchemaData *sdata,
    SchemaCP *pattern,
    SchemaQuant quant,
    int n,
    int m
    )
{
    SchemaCP *wrapperCP;
    SchemaCP *savedCP = NULL;
    unsigned int savedContenSize;

    if (sdata->cp->type == SCHEMA_CTYPE_NAME
        && sdata->cp->flags & CONSTRAINT_TEXT_CHILD
        && (pattern->type != SCHEMA_CTYPE_TEXT
            || pattern->nc == 0)) {
        sdata->cp->flags &= ~CONSTRAINT_TEXT_CHILD;
    }
    if (sdata->cp->type == SCHEMA_CTYPE_CHOICE
        || sdata->cp->type == SCHEMA_CTYPE_INTERLEAVE) {
        if (pattern->type == SCHEMA_CTYPE_CHOICE) {
            if (pattern->flags & MIXED_CONTENT) {
                sdata->cp->flags |= MIXED_CONTENT;
            }
            wrapperCP = initSchemaCP (SCHEMA_CTYPE_PATTERN, NULL, NULL);
            REMEMBER_PATTERN (wrapperCP);
            wrapperCP->content[0] = pattern;
            wrapperCP->quants[0] = SCHEMA_CQUANT_ONE;
            wrapperCP->nc = 1;
            pattern = wrapperCP;
        }
        if (sdata->cp->type == SCHEMA_CTYPE_CHOICE
            && quant != SCHEMA_CQUANT_ONE) {
            wrapperCP = initSchemaCP (SCHEMA_CTYPE_PATTERN, NULL, NULL);
            REMEMBER_PATTERN (wrapperCP);
            if (sdata->cp->nc == sdata->contentSize) {
                sdata->cp->content =
                    REALLOC (sdata->cp->content,
                             2 * sdata->contentSize
                             * sizeof (SchemaCP*));
                sdata->cp->quants =
                    REALLOC (sdata->cp->quants,
                             2 * sdata->contentSize
                             * sizeof (SchemaQuant));
                sdata->contentSize *= 2;
            }
            sdata->cp->content[sdata->cp->nc] = wrapperCP;
            sdata->cp->quants[sdata->cp->nc] = SCHEMA_CQUANT_ONE;
            sdata->cp->nc++;
            savedCP = sdata->cp;
            savedContenSize = sdata->contentSize;
            sdata->cp = wrapperCP;
            sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        }
    }
    if (quant == SCHEMA_CQUANT_NM) {
        int i, newChilds, thisquant;
        if (m == -1) {
            m = n + 1;
            newChilds = m;
            thisquant = SCHEMA_CQUANT_REP;
        } else {
            newChilds = (n >= m) ? n : m;
            thisquant = SCHEMA_CQUANT_OPT;
        }
        while (sdata->cp->nc + newChilds >= sdata->contentSize) {
            sdata->cp->content =
                REALLOC (sdata->cp->content,
                         2 * sdata->contentSize
                         * sizeof (SchemaCP*));
            sdata->cp->quants =
                REALLOC (sdata->cp->quants,
                         2 * sdata->contentSize
                         * sizeof (SchemaQuant));
            sdata->contentSize *= 2;
        }
        for (i = 0; i < n; i++) {
            sdata->cp->content[sdata->cp->nc+i] = pattern;
            sdata->cp->quants[sdata->cp->nc+i] = SCHEMA_CQUANT_ONE;
        }
        for (i = n; i < m; i++) {
            sdata->cp->content[sdata->cp->nc+i] = pattern;
            sdata->cp->quants[sdata->cp->nc+i] = thisquant;
        }
        sdata->cp->nc = sdata->cp->nc + newChilds;
    } else {
        if (sdata->cp->nc == sdata->contentSize) {
            sdata->cp->content =
                REALLOC (sdata->cp->content,
                         2 * sdata->contentSize
                         * sizeof (SchemaCP*));
            sdata->cp->quants =
                REALLOC (sdata->cp->quants,
                         2 * sdata->contentSize
                         * sizeof (SchemaQuant));
            sdata->contentSize *= 2;
        }
        sdata->cp->content[sdata->cp->nc] = pattern;
        sdata->cp->quants[sdata->cp->nc] = quant;
        sdata->cp->nc++;
    }
    if (savedCP) {
        sdata->cp = savedCP;
        sdata->contentSize = savedContenSize;
    }
}

static SchemaValidationStack * 
getStackElement (
    SchemaData *sdata,
    SchemaCP *pattern
    )
{
    SchemaValidationStack *stackElm;

    if (sdata->stackPool) {
        stackElm = sdata->stackPool;
        sdata->stackPool = stackElm->down;
    } else {
        stackElm = TMALLOC (SchemaValidationStack);
    }
    memset (stackElm, 0, sizeof (SchemaValidationStack));
    stackElm->pattern = pattern;
    return stackElm;
}

static void
repoolStackElement (
    SchemaData *sdata,
    SchemaValidationStack *se
    ) 
{
    if (se->interleaveState) {
        FREE (se->interleaveState);
        se->interleaveState = NULL;
    }
    se->down = sdata->stackPool;
    sdata->stackPool = se;
}

static void
pushToStack (
    SchemaData *sdata,
    SchemaCP *pattern
    )
{
    SchemaValidationStack *se, *nextse;
    DBG(fprintf(stderr, "push to Stack:\n");serializeCP(pattern));
    if (pattern->type == SCHEMA_CTYPE_NAME && sdata->lastMatchse) {
        se = sdata->lastMatchse;
        while (se) {
            nextse = se->down;
            repoolStackElement (sdata, se);
            se = nextse;
        }
        sdata->lastMatchse = NULL;
    }
    se = getStackElement (sdata, pattern);
    se->down = sdata->stack;
    if (pattern->type == SCHEMA_CTYPE_INTERLEAVE) {
        se->interleaveState = MALLOC (sizeof (int) * pattern->nc);
        memset (se->interleaveState, 0, sizeof (int) * pattern->nc);
    }
    sdata->stack = se;
}

static void
popFromStack (
    SchemaData *sdata,
    SchemaValidationStack **stack
    )
{
    SchemaValidationStack *se;
    DBG(fprintf(stderr, "popFromStack:\n"); serializeCP((*stack)->pattern));
    se = (*stack)->down;
    repoolStackElement (sdata, *stack);
    *stack = se;
}

static void
popStack (
    SchemaData *sdata
    )
{
    SchemaValidationStack *se, *nextse;
    DBG(fprintf(stderr, "popStack:\n"); serializeCP(sdata->stack->pattern));
    if (sdata->stack->pattern->type == SCHEMA_CTYPE_NAME) {
        se = sdata->lastMatchse;
        while (se) {
            nextse = se->down;
            repoolStackElement (sdata, se);
            se = nextse;
        }
        sdata->lastMatchse = NULL;
        se = sdata->stack->down;
        repoolStackElement (sdata, sdata->stack);
        sdata->stack = se;
    } else {
        if (sdata->stack->hasMatched) {
            se = sdata->stack->down;
            sdata->stack->down = sdata->lastMatchse;
            sdata->lastMatchse = sdata->stack;
            sdata->stack = se;
        } else {
            se = sdata->stack->down;
            repoolStackElement (sdata, sdata->stack);
            sdata->stack = se;
        }
    }
}

static void
finalizeElement (
    SchemaData *sdata,
    int ac
    )
{
    SchemaValidationStack *se;
    SchemaCP *cp, *cp1;
    int i;

    se = sdata->stack;
    cp = se->pattern;
    if (cp->type == SCHEMA_CTYPE_NAME || cp->type == SCHEMA_CTYPE_PATTERN) {
        for (i = ac; i < cp->nc; i++) {
            cp1 = cp->content[ac];
            if (cp1->type == SCHEMA_CTYPE_KEYSPACE) {
                if (!cp1->keySpace->active) {
                    Tcl_InitHashTable (&cp1->keySpace->ids,
                                       TCL_STRING_KEYS);
                    cp1->keySpace->active = 1;
                    cp1->keySpace->unknownIDrefs = 0;
                } else {
                    cp1->keySpace->active++;
                }                    
            } else if (cp->content[ac]->type == SCHEMA_CTYPE_KEYSPACE_END) {
                cp1->keySpace->active--;
                if (!cp1->keySpace->active) {
                    cp1->keySpace->unknownIDrefs = 0;
                    Tcl_DeleteHashTable (&cp1->keySpace->ids);
                }
            }
        }
    }
    popStack (sdata);
    /* cp is still the pattern from stack top before the popStack */
    if (cp->type != SCHEMA_CTYPE_NAME) {
        finalizeElement (sdata, sdata->stack->activeChild + 1);
    }
}

static void
rewindStack (
    SchemaData *sdata
    ) 
{
    SchemaValidationStack *se;

    while (sdata->lastMatchse) {
        se = sdata->lastMatchse;
        sdata->lastMatchse = se->down;
        se->down = sdata->stack;
        sdata->stack = se;
    }
}

static int 
recover (
    Tcl_Interp *interp,
    SchemaData *sdata,
    ValidationErrorType errorType,
    const char *name,
    const char *ns,
    char *text,
    int ac
    )
{
    Tcl_Obj *cmdPtr;
    int rc;
    SchemaValidationStack *se;

    if (!sdata->reportCmd || sdata->evalError) return 0;
    if (sdata->recoverFlags & RECOVER_FLAG_DONT_REPORT) return 1;
    /* If non SCHEMA_CTYPE_NAME and the pattern hasn't already matched
     * that's a pattern pushed on stack to look for (probe) if it
     * matches (or allows empty match). Even if the pattern fail it
     * may be optional; recovering is done at the caller level in case
     * the pattern isn't optional. */
    if (sdata->stack
        && sdata->stack->pattern->type != SCHEMA_CTYPE_NAME
        && sdata->stack->activeChild == 0
        && sdata->stack->hasMatched == 0) return 0;
    cmdPtr = Tcl_DuplicateObj (sdata->reportCmd);
    Tcl_IncrRefCount(cmdPtr);
    Tcl_ListObjAppendElement (interp, cmdPtr,
                              sdata->self);
    Tcl_ListObjAppendElement (
        interp, cmdPtr,
        Tcl_NewStringObj (ValidationErrorType2str[errorType], -1)
        );
    /* In case of unknown element the name/ns arguments of recover()
     * are NULL, but sdata->vname/sdata->vns are already
     * pre-filled. */
    if (name) sdata->vname = name;
    if (ns) sdata->vns = ns;
    sdata->vtext = text;
    switch (errorType) {
    case DOM_KEYCONSTRAINT:
        sdata->vaction = MATCH_DOM_KEYCONSTRAINT;
        break;
    case DOM_XPATH_BOOLEAN:
        sdata->vaction = MATCH_DOM_XPATH_BOOLEAN;
        break;
    case MISSING_ATTRIBUTE:
    case UNKNOWN_ATTRIBUTE:
    case MISSING_ELEMENT_MATCH_START:
    case MISSING_TEXT_MATCH_START:
    case INVALID_KEYREF_MATCH_START:
    case UNEXPECTED_ROOT_ELEMENT:
    case UNKNOWN_ROOT_ELEMENT:
    case UNEXPECTED_ELEMENT:
        sdata->vaction = MATCH_ELEMENT_START;
        break;
    case MISSING_TEXT_MATCH_END:
    case INVALID_KEYREF_MATCH_END:
    case MISSING_ELEMENT_MATCH_END:
        if (sdata->stack) {
            se = sdata->stack;
            while (se->pattern->type != SCHEMA_CTYPE_NAME) {
                se = se->down;
            }
            sdata->vname = se->pattern->name;
            sdata->vns = se->pattern->namespace;
        }
        sdata->vaction = MATCH_ELEMENT_END;
        break;
    case UNEXPECTED_TEXT:
        sdata->vaction = MATCH_TEXT;
        break;
    case INVALID_KEYREF_MATCH_TEXT:
    case INVALID_VALUE:
        if (sdata->stack) {
            se = sdata->stack;
            while (se->pattern->type != SCHEMA_CTYPE_NAME) {
                se = se->down;
            }
            sdata->vname = se->pattern->name;
            sdata->vns = se->pattern->namespace;
        }
        sdata->vaction = MATCH_TEXT;
        break;
    case UNKNOWN_GLOBAL_ID:
    case UNKNOWN_ID:
        sdata->vaction = MATCH_GLOBAL;
        break;
    case INVALID_ATTRIBUTE_VALUE:
        sdata->vaction = MATCH_ATTRIBUTE_TEXT;
        break;
    }
    sdata->currentEvals++;
    rc = Tcl_EvalObjEx (interp, cmdPtr,
                        TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    sdata->currentEvals--;
    sdata->vaction = 0;
    if (name) sdata->vname = name;
    if (ns) sdata->vns = ns;
    sdata->vtext = NULL;
    Tcl_DecrRefCount (cmdPtr);
    if (rc != TCL_OK) {
        sdata->evalError = 1;
        return 0;
    }
    switch (errorType) {
    case MISSING_ELEMENT_MATCH_START:
        if (strcmp (Tcl_GetStringResult (interp), "ignore") == 0) {
            sdata->recoverFlags |= RECOVER_FLAG_IGNORE;
            return 1;
        } else if (strcmp (Tcl_GetStringResult (interp), "vanish") == 0) {
            sdata->recoverFlags |= RECOVER_FLAG_REWIND;
            sdata->skipDeep = 1;
            return 1;
        } else {
            /* Rewind stack to last match and ignore the just opened
             * Element. */
           finalizeElement (sdata, ac+1);
            sdata->skipDeep = 2;
        }
        break;
    case UNEXPECTED_ELEMENT:
        if (strcmp (Tcl_GetStringResult (interp), "vanish") == 0) {
            sdata->recoverFlags |= RECOVER_FLAG_REWIND;
            sdata->skipDeep = 1;
            return 1;
        } else {
            finalizeElement (sdata, ac+1);
            sdata->skipDeep = 2;
        }
        break;
    case UNEXPECTED_TEXT:
        sdata->recoverFlags |= RECOVER_FLAG_REWIND;
        break;
    case MISSING_ELEMENT_MATCH_END:
    case MISSING_TEXT_MATCH_END:
        if (strcmp (Tcl_GetStringResult (interp), "ignore") == 0) {
            sdata->recoverFlags |= RECOVER_FLAG_MATCH_END_CONTINUE;
        } else {
            sdata->recoverFlags |= RECOVER_FLAG_DONT_REPORT;
        }
        break;        
    case DOM_KEYCONSTRAINT:
    case DOM_XPATH_BOOLEAN:
    case MISSING_ATTRIBUTE:
    case MISSING_TEXT_MATCH_START:
    case UNEXPECTED_ROOT_ELEMENT:
    case UNKNOWN_ATTRIBUTE:
    case INVALID_KEYREF_MATCH_START:
    case INVALID_KEYREF_MATCH_END:
    case INVALID_KEYREF_MATCH_TEXT:
    case UNKNOWN_ROOT_ELEMENT:
    case UNKNOWN_GLOBAL_ID:
    case UNKNOWN_ID:
    case INVALID_ATTRIBUTE_VALUE:
    case INVALID_VALUE:
        break;
    }
    return 1;
}

/* The cp argument must be type SCHEMA_CTYPE_TEXT */
static int
checkText (
    Tcl_Interp *interp,
    void *clientData,
    char *text
    )
{
    unsigned int i;
    SchemaCP *cp = (SchemaCP *) clientData;
    SchemaConstraint *sc;

    /* Look also at oneOfImpl */
    for (i = 0; i < cp->nc; i++) {
        sc = (SchemaConstraint *) cp->content[i];
        if (!(sc->constraint) (interp, sc->constraintData, text)) {
            return 0;
        }
    }
    return 1;
}

/* The argument ac points to the child of the current top-most stack
 * element pattern which is to evaluate. */
static int
evalVirtual (
    Tcl_Interp *interp,
    SchemaData *sdata,
    int ac
    )
{
    int rc;
    SchemaCP *cp;

    cp = sdata->stack->pattern->content[ac];
    sdata->currentEvals++;
    rc = Tcl_EvalObjv (interp, cp->nc, (Tcl_Obj **) cp->content,
                       TCL_EVAL_GLOBAL);
    sdata->currentEvals--;
    if (rc != TCL_OK) {
        sdata->evalError = 1;
        return 0;
    }
    return 1;
}

/* Check, if the pattern to probe does not call itself (even
 * indirectly) without a match inbetween.*/
static int inline
recursivePattern (
    SchemaValidationStack *se,
    SchemaCP *pattern
    )
{
    int rc = 0;
    
    while (se && se->pattern->type != SCHEMA_CTYPE_NAME) {
        if (!se->hasMatched && se->pattern == pattern) {
            rc = 1;
            break;
        }
        se = se->down;
    }
    return rc;
}

static int
matchingAny (
    char *name,
    char *namespace,
    SchemaData *sdata,
    SchemaCP *candidate
    )
{
    Tcl_HashEntry *h;

    if (candidate->flags & ANY_NOT) {
        if (candidate->namespace || candidate->typedata) {
            /* The any wildcard is limited to one or several
             * namespaces (the empty namespace may be one of
             * them). */
            if (namespace) {
                if (candidate->typedata) {
                    h = Tcl_FindHashEntry (
                        (Tcl_HashTable *)candidate->typedata,
                        namespace);
                    if (h) return 0;
                } else {
                    if (candidate->namespace == namespace) {
                        return 0;
                    }
                }
            } else {
                if (candidate->namespace == emptyStr) {
                    return 0;
                }
            }
        }
        return 1;
    } else {
        if (candidate->namespace || candidate->typedata) {
            /* The any wildcard is limited to one or several
             * namespaces (the empty namespace may be one of
             * them). */
            if (namespace) {
                if (candidate->typedata) {
                    h = Tcl_FindHashEntry (
                        (Tcl_HashTable *)candidate->typedata,
                        namespace);
                    if (!h) return 0;
                } else {
                    if (candidate->namespace != namespace) {
                        return 0;
                    }
                }
            } else {
                if (candidate->namespace != emptyStr) {
                    return 0;
                }
            }
        }
        return 1;
    }
}

static int
matchElementStart (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *name,
    char *namespace
    )
{
    SchemaCP *cp, *candidate, *icp;
    int hm, mayskip, thismayskip, rc, isName = 0;
    unsigned int ac, i;
    SchemaValidationStack *se;
    Tcl_HashEntry *h;

    if (!sdata->stack) return 0;
    se = sdata->stack;
    getContext (cp, ac, hm);

    switch (cp->type) {
    case SCHEMA_CTYPE_NAME:
        isName = 1;
        /* fall through */
    case SCHEMA_CTYPE_PATTERN:
        while (ac < cp->nc) {
            candidate = cp->content[ac];
            mayskip = 0;
            switch (candidate->type) {
            case SCHEMA_CTYPE_TEXT:
                if (candidate->nc) {
                    if (!checkText (interp, candidate, "")) {
                        if (recover (interp, sdata, MISSING_TEXT_MATCH_START,
                                     name, namespace, NULL, ac)) {
                            mayskip = 1;
                            break;
                        }            
                        return 0;
                    }
                }
                break;

            case SCHEMA_CTYPE_ANY:
                if (!matchingAny (name, namespace, sdata, candidate)) break;
                updateStack (sdata, se, ac);
                sdata->skipDeep = 1;
                /* See comment in tDOM_probeElement: sdata->vname and
                 * sdata->vns may be pre-filled. We reset it here.*/
                sdata->vname = NULL;
                sdata->vns = NULL;
                return 1;

            case SCHEMA_CTYPE_NAME:
                DBG(fprintf (stderr, "name: %s ns: %s candidate name: %s "
                             "candidate ns: %s\n", name, namespace,
                             candidate->name, candidate->namespace));
                if (candidate->name == name
                    && candidate->namespace == namespace) {
                    pushToStack (sdata, candidate);
                    updateStack (sdata, se, ac);
                    return 1;
                }
                break;

            case SCHEMA_CTYPE_CHOICE:
                if (candidate->typedata) {
                    h = Tcl_FindHashEntry ((Tcl_HashTable *)candidate->typedata,
                                           name);
                    if (h) {
                        icp = Tcl_GetHashValue (h);
                        if (icp->namespace == namespace) {
                            pushToStack (sdata, icp);
                            updateStack (sdata, se, ac);
                            return 1;
                        }
                    }
                    /* TODO: Short-cut in case of no match (looking
                     * for empty match, recovering). For now fall
                     * throu to simple, serial approach. */
                }
                for (i = 0; i < candidate->nc; i++) {
                    icp = candidate->content[i];
                    switch (icp->type) {
                    case SCHEMA_CTYPE_TEXT:
                        break;

                    case SCHEMA_CTYPE_ANY:
                        if (!matchingAny (name, namespace, sdata, icp)) break;
                        updateStack (sdata, se, ac);
                        sdata->skipDeep = 1;
                        /* See comment in tDOM_probeElement: sdata->vname
                         * and sdata->vns may be pre-filled. We reset it
                         * here.*/
                        sdata->vname = NULL;
                        sdata->vns = NULL;
                        return 1;

                    case SCHEMA_CTYPE_NAME:
                        if (icp->name == name
                            && icp->namespace == namespace) {
                            pushToStack (sdata, icp);
                            updateStack (sdata, se, ac);
                            return 1;
                        }
                        break;

                    case SCHEMA_CTYPE_CHOICE:
                        Tcl_Panic ("MIXED or CHOICE child of MIXED or CHOICE");

                    case SCHEMA_CTYPE_PATTERN:
                        if (recursivePattern (se, icp)) {
                            mayskip = 1;
                            continue;
                        }
                        /* fall throu */
                    case SCHEMA_CTYPE_INTERLEAVE:
                        pushToStack (sdata, icp);
                        rc = matchElementStart (interp, sdata, name, namespace);
                        if (rc == 1) {
                            updateStack (sdata, se, ac);
                            return 1;
                        }
                        popStack (sdata);
                        if (rc == -1) mayskip = 1;
                        break;

                    case SCHEMA_CTYPE_VIRTUAL:
                        Tcl_Panic ("Virtual constrain in MIXED or CHOICE");
                        
                    case SCHEMA_CTYPE_KEYSPACE_END:
                    case SCHEMA_CTYPE_KEYSPACE:
                        Tcl_Panic ("Keyspace constrain in MIXED or CHOICE");
                        
                    }
                    if (!mayskip && mayMiss (candidate->quants[i]))
                        mayskip = 1;
                }
                break;

            case SCHEMA_CTYPE_VIRTUAL:
                if (evalVirtual (interp, sdata, ac)) {
                    hm = 1;
                    break;
                }
                else return 0;

            case SCHEMA_CTYPE_PATTERN:
                if (recursivePattern (se, candidate)) {
                    mayskip = 1;
                    break;
                }
                /* fall throu */
            case SCHEMA_CTYPE_INTERLEAVE:
                pushToStack (sdata, candidate);
                rc = matchElementStart (interp, sdata, name, namespace);
                if (rc == 1) {
                    updateStack (sdata, se, ac);
                    return 1;
                }
                popStack (sdata);
                if (rc == -1) mayskip = 1;
                break;

            case SCHEMA_CTYPE_KEYSPACE_END:
                candidate->keySpace->active--;
                if (!candidate->keySpace->active) {
                    if (candidate->keySpace->unknownIDrefs) {
                        if (!recover (interp, sdata,
                                      INVALID_KEYREF_MATCH_START, name,
                                      namespace, NULL, ac)) {
                            return 0;
                        }
                        candidate->keySpace->unknownIDrefs = 0;
                    }
                    Tcl_DeleteHashTable (&candidate->keySpace->ids);
                }
                ac++;
                hm = 0;
                continue;

            case SCHEMA_CTYPE_KEYSPACE:
                if (!candidate->keySpace->active) {
                    Tcl_InitHashTable (&candidate->keySpace->ids,
                                       TCL_STRING_KEYS);
                    candidate->keySpace->active = 1;
                    candidate->keySpace->unknownIDrefs = 0;
                } else {
                    candidate->keySpace->active++;
                }
                ac++;
                hm = 0;
                continue;
            }
            if (!mayskip && mustMatch (cp->quants[ac], hm)) {
                if (recover (interp, sdata, MISSING_ELEMENT_MATCH_START, name,
                             namespace, NULL, ac)) {
                    if (sdata->recoverFlags & RECOVER_FLAG_IGNORE) {
                        /* We pretend the ac content particle had
                         * matched. */
                        updateStack (sdata, se, ac);
                    }
                    return 1;
                }
                return 0;
            }
            ac++;
            hm = 0;
        }
        if (isName) {
            if (recover (interp, sdata, UNEXPECTED_ELEMENT, name, namespace,
                         NULL, 0)) {
                return 1;
            }
            return 0;
        }
        return -1;

    case SCHEMA_CTYPE_KEYSPACE:
    case SCHEMA_CTYPE_KEYSPACE_END:
    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");

    case SCHEMA_CTYPE_INTERLEAVE:
        mayskip = 1;
        for (i = 0; i < cp->nc; i++) {
            thismayskip = 0;
            if (se->interleaveState[i]) {
                if (maxOne (cp->quants[i])) continue;
            }
            icp = cp->content[i];
            switch (icp->type) {
            case SCHEMA_CTYPE_TEXT:
                if (icp->nc) {
                    if (checkText (interp, icp, "")) {
                        thismayskip = 1;
                    }
                } else {
                    thismayskip = 1;
                }
                break;

            case SCHEMA_CTYPE_ANY:
                if (!matchingAny (name, namespace, sdata, icp)) break;
                sdata->skipDeep = 1;
                se->hasMatched = 1;
                se->interleaveState[i] = 1;
                /* See comment in tDOM_probeElement: sdata->vname and
                 * sdata->vns may be pre-filled. We reset it here.*/
                sdata->vname = NULL;
                sdata->vns = NULL;
                return 1;

            case SCHEMA_CTYPE_NAME:
                if (icp->name == name
                    && icp->namespace == namespace) {
                    pushToStack (sdata, icp);
                    se->hasMatched = 1;
                    se->interleaveState[i] = 1;
                    return 1;
                }
                break;

            case SCHEMA_CTYPE_CHOICE:
                Tcl_Panic ("MIXED or CHOICE child of INTERLEAVE");

            case SCHEMA_CTYPE_PATTERN:
                if (recursivePattern (se, icp)) {
                    thismayskip = 1;
                    break;
                }
                /* fall throu */
            case SCHEMA_CTYPE_INTERLEAVE:
                pushToStack (sdata, icp);
                rc = matchElementStart (interp, sdata, name, namespace);
                if (rc == 1) {
                    if (!(sdata->recoverFlags & RECOVER_FLAG_REWIND)) {
                        se->hasMatched = 1;
                        se->interleaveState[i] = 1;
                    }
                    return 1;
                }
                popStack (sdata);
                if (rc == -1) thismayskip = 1;
                break;

            case SCHEMA_CTYPE_VIRTUAL:
                Tcl_Panic ("Virtual constraint child of INTERLEAVE");
                break;

            case SCHEMA_CTYPE_KEYSPACE_END:
            case SCHEMA_CTYPE_KEYSPACE:
                Tcl_Panic ("Keyspace constraint child of INTERLEAVE");
                break;

            }
            if (!thismayskip && minOne (cp->quants[i])) mayskip = 0;
        }
        if (mayskip) break;
        if (recover (interp, sdata, MISSING_ELEMENT_MATCH_START, name,
                     namespace, NULL, cp->nc)) {
            if (sdata->recoverFlags & RECOVER_FLAG_IGNORE) {
                /* We mark the first so far not matched mandatory
                 * interleave child cp as matched */
                for (i = 0; i < cp->nc; i++) {
                    if (!se->interleaveState[i]) {
                        if (minOne (cp->quants[i])) {
                            se->interleaveState[i] = 1;
                            break;
                        }
                    }
                }
            }
            return 1;
        }
        return 0;
    }
    return -1;
}

static void *
getNamespacePtr (
    SchemaData *sdata,
    char *ns
    )
{
    Tcl_HashEntry *h;
    int hnew;

    if (!ns) return NULL;
    if (ns[0] == '\0') return NULL;
    h = Tcl_FindHashEntry (&sdata->prefix, ns);
    if (h) {
        return Tcl_GetHashValue (h);
    }
    h = Tcl_CreateHashEntry (&sdata->namespace, ns, &hnew);
    return Tcl_GetHashKey (&sdata->namespace, h);
}

int
tDOM_probeElement (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *name,
    void *namespace
    )
{
    Tcl_HashEntry *h;
    void *namespacePtr, *namePtr;
    SchemaCP *pattern;
    int rc = 1, reportError;

    if (sdata->skipDeep) {
        sdata->skipDeep++;
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }

    DBG(
        fprintf (stderr, "tDOM_probeElement: look if '%s' in ns '%s' match\n",
                 name, (char *)namespace);
        );

    if (namespace) {
        h = Tcl_FindHashEntry (&sdata->namespace, namespace);
    } else {
        h = NULL;
    }
    if (h) {
        namespacePtr = Tcl_GetHashKey (&sdata->namespace, h);
    } else {
        if (namespace) {
            /* This namespace isn't known at all by the schema; this
             * element may only match an any condition. If it does we
             * know only later. So we use namePtr and namespacePtr
             * both NULL that match nothing else in the schema and
             * will be able to look if there is such a possible any
             * match in the schema. */
            rc = 0;
            /* If there isn't a matching any cp this is a validation
             * error. To have the node name/namespace information
             * available in case of recovering we prefill the sdata
             * struct here.*/
            sdata->vname = name;
            sdata->vns = namespace;
            namespacePtr = (void *) unknownNS;
        } else {
            namespacePtr = NULL;
        }
    }
    if (!rc) {
        /* Already the provided namespace isn't known to the schema,
         * so the name in that namespace of course also. */
        namePtr = NULL;
    } else {
        h = Tcl_FindHashEntry (&sdata->element, name);
        if (h) {
            namePtr = Tcl_GetHashKey (&sdata->element, h);
        } else {
            namePtr = NULL;
            /* Prefill in case of validation error. See above.  */
            sdata->vname = name;
        }
    }
    
    if (sdata->validationState == VALIDATION_READY) {
        /* The root of the tree to check. */
        if (sdata->start) {
            if (strcmp (name, sdata->start) != 0) {
                if (recover (interp, sdata, UNEXPECTED_ROOT_ELEMENT, name,
                             namespace, NULL, 0)) {
                    sdata->validationState = VALIDATION_FINISHED;
                    return TCL_OK;
                }
                SetResult ("Root element doesn't match");
                return TCL_ERROR;
            }
            if (namespace) {
                if (!sdata->startNamespace||
                    strcmp (namespace, sdata->startNamespace) != 0) {
                    if (recover (interp, sdata, UNEXPECTED_ROOT_ELEMENT, name,
                                 namespace, NULL, 0)) {
                        sdata->validationState = VALIDATION_FINISHED;
                        return TCL_OK;
                    }
                    SetResult ("Root element namespace doesn't match");
                    return TCL_ERROR;
                }
            } else {
                if (sdata->startNamespace) {
                    if (recover (interp, sdata, UNEXPECTED_ROOT_ELEMENT, name,
                                 namespace, NULL, 0)) {
                        sdata->validationState = VALIDATION_FINISHED;
                        return TCL_OK;
                    }
                    SetResult ("Root element namespace doesn't match");
                    return TCL_ERROR;
                }
            }
        }
        reportError = 0;
        if (h) {
            pattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (pattern) {
                if (pattern->namespace == namespacePtr) {
                    if (pattern->flags & PLACEHOLDER_PATTERN_DEF
                        || pattern->flags & FORWARD_PATTERN_DEF) {
                        reportError = 1;
                    }
                    break;
                }
                pattern = pattern->next;
            }
        } else {
            pattern = NULL;
        }
        sdata->validationState = VALIDATION_STARTED;
        if (reportError || pattern == NULL) {
            if (recover (interp, sdata, UNKNOWN_ROOT_ELEMENT, name, namespace,
                         NULL, 0)) {
                sdata->skipDeep = 1;
                return TCL_OK;
            }
            SetResult ("Unknown element");
            return TCL_ERROR;
        }
        pushToStack (sdata, pattern);
        return TCL_OK;
    }

    /* The normal case: we're inside the tree */
    /* In case of recovering and if the user wants a required cp to be
     * treated as matched (or in other words: that the validation
     * engine should ignore the mandatory state of the cp) we unwind
     * the call stack to have updated stack elements, to be able to
     * pretend, we have seen the mandatory cp. Now try to match the
     * open element from this stack state. */
    while (1) {
        rc = matchElementStart (interp, sdata, (char *) namePtr,
                                namespacePtr);
        while (rc == -1) {
            popStack (sdata);
            rc = matchElementStart (interp, sdata, (char *) namePtr,
                                    namespacePtr);
        };
        if (rc) {
            DBG(
                fprintf (stderr, "tDOM_probeElement: element '%s' match\n",
                         name);
                serializeStack (sdata);
                fprintf (stderr, "\n");
                );
            if (sdata->recoverFlags & RECOVER_FLAG_IGNORE) {
                sdata->recoverFlags &= ~RECOVER_FLAG_IGNORE;
                continue;
            }
            CHECK_REWIND;
            return TCL_OK;
        }
        break;
    }
    DBG(
        fprintf (stderr, "element '%s' DOESN'T match\n", name);
        serializeStack (sdata);
        fprintf (stderr, "\n");
        );
    if (!sdata->evalError) {
        SetResult ("Element \"");
        if (namespacePtr) {
            Tcl_AppendResult (interp, namespacePtr, ":", NULL);
        }
        Tcl_AppendResult (interp, name, "\" doesn't match", NULL);
    }
    return TCL_ERROR;
}

int probeAttribute (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *name,
    const char *ns,
    char *value,
    int *isrequired
    )
{
    unsigned int i;
    SchemaCP *cp;
    Tcl_HashTable *t;
    Tcl_HashEntry *h;
    SchemaAttr *attr;
    
    cp = sdata->stack->pattern;
    *isrequired = 0;
    if (cp->typedata) {
        t = (Tcl_HashTable *) cp->typedata;
        h = Tcl_FindHashEntry (t, name);
        if (!h) return 0;
        attr = (SchemaAttr *) Tcl_GetHashValue (h);
        while (attr && attr->namespace != ns) {
            attr = attr->next;
        }
        if (!attr) return 0;
        if (attr->cp) {
            if (!checkText (interp, attr->cp, value)) {
                if (!recover (interp, sdata, INVALID_ATTRIBUTE_VALUE, name,
                              ns, value, 0)) {
                    SetResult3V ("Attribute value doesn't match for "
                                 "attribute '", name , "'");
                    return 0;
                }
            }
        }
        if (attr->required) *isrequired = 1;
        return 1;
    }
    for (i = 0; i < cp->numAttr; i++) {
        if (cp->attrs[i]->namespace == ns
            && cp->attrs[i]->name == name) {
            if (cp->attrs[i]->cp) {
                if (!checkText (interp, cp->attrs[i]->cp, value)) {
                    if (!recover (interp, sdata, INVALID_ATTRIBUTE_VALUE, name,
                                  ns, value, i)) {
                        SetResult3V ("Attribute value doesn't match for "
                                    "attribute '", name , "'");
                        return 0;
                    }
                }
            }
            if (cp->attrs[i]->required) *isrequired = 1;
            return 1;
        }
    }
    return 0;
}
    
int tDOM_probeAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char **attr
    )
{
    char   **atPtr, *ln, *namespace, *ns;
    int j, found, nsatt, req, reqAttr = 0;
    unsigned int i;
    SchemaCP *cp;
    Tcl_HashEntry *h;

    cp = sdata->stack->pattern;
    for (atPtr = (char **) attr; atPtr[0] && atPtr[1]; atPtr += 2) {
        found = 0;
        ln = atPtr[0];
        j = 0;
        while (*ln && *ln != '\xFF') {
            j++, ln++;
        }
        if (*ln == '\xFF') {
            namespace = atPtr[0];
            namespace[j] = '\0';
            ln++;
            nsatt = 1;
        } else {
            namespace = NULL;
            ln = atPtr[0];
            nsatt = 0;
        }
        h = Tcl_FindHashEntry (&sdata->attrNames, ln);
        if (!h) goto unknowncleanup;
        ln = Tcl_GetHashKey (&sdata->attrNames, h);
        ns = NULL;
        if (namespace) {
            h = Tcl_FindHashEntry (&sdata->namespace, namespace);
            if (!h) goto unknowncleanup;
            ns = Tcl_GetHashKey (&sdata->namespace, h);
        }
        found = probeAttribute (interp, sdata, ln, ns, atPtr[1], &req);
        reqAttr += req;
    unknowncleanup:
        if (!found) {
            if (!recover (interp, sdata, UNKNOWN_ATTRIBUTE, ln, namespace,
                          NULL, 0)) {
                if (!sdata->evalError) {
                    if (nsatt) {
                        SetResult ("Unknown attribute \"");
                        Tcl_AppendResult (interp, namespace, ":", ln, "\"",
                                          NULL);
                    } else {
                        SetResult3 ("Unknown attribute \"", ln, "\"");
                    }
                }
                if (nsatt) namespace[j] = '\xFF';
                return TCL_ERROR;
            }
        }
        if (nsatt) namespace[j] = '\xFF';
    }
    if (reqAttr != cp->numReqAttr) {
        /* Lookup the missing attribute(s) */
        if (!sdata->evalError) {
            SetResult ("Missing mandatory attribute(s):");
        }
        for (i = 0; i < cp->numAttr; i++) {
            if (!cp->attrs[i]->required) continue;
            found = 0;
            for (atPtr = (char **) attr; atPtr[0] && atPtr[1]; atPtr += 2) {
                ln = atPtr[0];
                if (cp->attrs[i]->namespace) {
                    j = 0;
                    while (*ln && *ln != '\xFF') {
                        j++, ln++;
                    }
                    if (*ln == '\xFF') {
                        namespace = atPtr[0];
                        namespace[j] = '\0';
                        ln++;
                        nsatt = 1;
                    } else {
                        continue;
                    }
                    if (strcmp (cp->attrs[i]->namespace, namespace) != 0) {
                        if (nsatt) namespace[j] = '\xFF';
                        continue;
                    }
                    if (nsatt) namespace[j] = '\xFF';
                }
                if (strcmp (ln, cp->attrs[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (!recover (interp, sdata, MISSING_ATTRIBUTE,
                              cp->attrs[i]->name, cp->attrs[i]->namespace,
                              NULL, i)) {
                    if (cp->attrs[i]->namespace) {
                        Tcl_AppendResult (interp, " ", cp->attrs[i]->namespace,
                                          ":", cp->attrs[i]->name, NULL);
                    } else {
                        Tcl_AppendResult (interp, " ", cp->attrs[i]->name, NULL);
                    }
                }
            }
        }
        if (!sdata->reportCmd) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

int tDOM_probeDomAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domAttrNode *attr
    )
{
    domAttrNode *atPtr;
    int found, req, reqAttr = 0;
    unsigned int i;
    const char *ns, *ln;
    SchemaCP *cp;
    Tcl_HashEntry *h;

    cp = sdata->stack->pattern;
    atPtr = attr;
    while (atPtr) {
        if (atPtr->nodeFlags & IS_NS_NODE) goto nextAttr;
        found = 0;
        if (atPtr->namespace) {
            ns = domNamespaceURI ((domNode *)atPtr);
            /* A namespaced attribute must always have a prefix */
            ln = atPtr->nodeName;
            while (*ln) {
                if (*ln == ':') {
                    ln++;
                    break;
                }
                ln++;
            }
        } else {
            ns = NULL;
            ln = atPtr->nodeName;
        }
        h = Tcl_FindHashEntry (&sdata->attrNames, ln);
        if (!h) goto unknown;
        ln = Tcl_GetHashKey (&sdata->attrNames, h);
        if (ns) {
            h = Tcl_FindHashEntry (&sdata->namespace, ns);
            if (!h) goto unknown;
            ns = Tcl_GetHashKey (&sdata->namespace, h);
        } else {
            ns = NULL;
        }
        found = probeAttribute (interp, sdata, ln, ns, atPtr->nodeValue, &req);
        reqAttr += req;
    unknown:
        if (!found) {
            if (!recover (interp, sdata, UNKNOWN_ATTRIBUTE, ln, ns, NULL, 0)) {
                if (!sdata->evalError) {
                    if (ns) {
                        SetResult ("Unknown attribute \"");
                        Tcl_AppendResult (interp, ns, ":", atPtr->nodeName,
                                          "\"", NULL);
                    } else {
                        SetResult3 ("Unknown attribute \"", atPtr->nodeName,
                                    "\"");
                    }
                    sdata->validationState = VALIDATION_ERROR;
                }
                return TCL_ERROR;
            }
        }
    nextAttr:
        atPtr = atPtr->nextSibling;
    }
    if (reqAttr != cp->numReqAttr) {
        /* Lookup the missing attribute(s) */
        if (!sdata->evalError) {
            SetResult ("Missing mandatory attribute(s):");
        }
        for (i = 0; i < cp->numAttr; i++) {
            if (!cp->attrs[i]->required) continue;
            found = 0;
            atPtr = attr;
            while (atPtr) {
                if (cp->attrs[i]->namespace) {
                    if (!atPtr->namespace) goto nextAttr2;
                    ns = domNamespaceURI ((domNode *)atPtr);
                    if (strcmp (ns, cp->attrs[i]->namespace) != 0) {
                        goto nextAttr2;
                    }
                    ln = atPtr->nodeName;
                    while (*ln) {
                        if (*ln == ':') {
                            ln++;
                            break;
                        }
                        ln++;
                    }
                } else {
                    if (atPtr->namespace) goto nextAttr2;
                    ln = atPtr->nodeName;
                }
                if (strcmp (ln, cp->attrs[i]->name) == 0) {
                    found = 1;
                    break;
                }
            nextAttr2:
                atPtr = atPtr->nextSibling;
            }
            if (!found) {
                if (!recover (interp, sdata, MISSING_ATTRIBUTE,
                              cp->attrs[i]->name, cp->attrs[i]->namespace,
                              NULL, i)) {
                    if (!sdata->evalError) {
                        if (cp->attrs[i]->namespace) {
                            Tcl_AppendResult (interp, " ",
                                              cp->attrs[i]->namespace, ":",
                                              cp->attrs[i]->name, NULL);
                        } else {
                            Tcl_AppendResult (interp, " ", cp->attrs[i]->name,
                                              NULL);
                        }
                    }
                }
            }
        }
        if (!sdata->reportCmd) {
            sdata->validationState = VALIDATION_ERROR;
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}
int probeEventAttribute (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *attr,
    int len
    )
{
    int i, found, req, reqAttr = 0;
    char *name, *ns;
    SchemaCP *cp;
    Tcl_HashEntry *h;
    Tcl_Obj *attname, *attns, *attvalue;

    cp = sdata->stack->pattern;
    for (i = 0; i < len; i += 2) {
        found = 0;
        ns = NULL;
        name = NULL;
        attns = NULL;
        Tcl_ListObjIndex (interp, attr, i, &attname);
        Tcl_ListObjIndex (interp, attr, i+1, &attvalue);
        if (Tcl_ListObjLength (interp, attname, &len) == TCL_OK) {
            if (len == 2) {
                Tcl_ListObjIndex (interp, attname, 1, &attns);
                Tcl_ListObjIndex (interp, attname, 0, &attname);
            }
        }
        h = Tcl_FindHashEntry (&sdata->attrNames, Tcl_GetString (attname));
        if (!h) goto unknown;
        name = Tcl_GetHashKey (&sdata->attrNames, h);
        if (attns) {
            h = Tcl_FindHashEntry (&sdata->namespace, Tcl_GetString (attns));
            if (!h) goto unknown;
            ns = Tcl_GetHashKey (&sdata->namespace, h);
        }
        found = probeAttribute (interp, sdata, name, ns,
                                Tcl_GetString (attvalue), &req);
        reqAttr += req;
    unknown:
        if (!found) {
            if (!recover (interp, sdata, UNKNOWN_ATTRIBUTE,
                          Tcl_GetString (attname), Tcl_GetString (attns),
                          NULL, 0)) {
                if (ns) {
                    SetResult ("Unknown attribute \"");
                    Tcl_AppendResult (interp, ns, ":", name, "\"", NULL);
                } else {
                    SetResult3 ("Unknown attribute \"", name, "\"");
                }
                sdata->validationState = VALIDATION_ERROR;
                return TCL_ERROR;
            }
        }
    }
    if (reqAttr != cp->numReqAttr) {
        SetResult ("Missing mandatory attribute(s)");
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* Returns either -1, 0, 1, 2

   -1 means a pattern or an interleave ended may end, look further at
   parents next sibling.

   0 means rewind with validation error.

   1 means element content may end here.

   2 means recovering requested further error reporting about missing children
   in the current element. To be able to answer a [info expected] on
   the occasion of the next error, we update the stack in this case
   and let tDOM_probeElementEnd restart checkElementEnd again with this
   stack state.
*/
static int checkElementEnd (
    Tcl_Interp *interp,
    SchemaData *sdata
    )
{
    SchemaValidationStack *se;
    SchemaCP *cp, *ic;
    int hm, thismayskip, mayskip = 0, rc;
    unsigned int ac, i;
    int isName = 0;

    DBG(fprintf (stderr, "checkElementEnd:\n");
        serializeStack(sdata););
    se = sdata->stack;
    getContext (cp, ac, hm);

    if (cp->type == SCHEMA_CTYPE_INTERLEAVE) {
        ac = 0; hm = 0; mayskip = 1;
    }
    switch (cp->type) {
    case SCHEMA_CTYPE_NAME:
        isName = 1;
        /* Fall through */
    case SCHEMA_CTYPE_INTERLEAVE:
    case SCHEMA_CTYPE_PATTERN:
        if (ac < cp->nc && (hasMatched (cp->quants[ac], hm))) {
            DBG(fprintf (stderr, "ac %d has matched, skiping to next ac\n", ac));
            ac++; hm = 0;
        }
        while (ac < cp->nc) {
            DBG(fprintf (stderr, "ac %d hm %d mayMiss: %d\n",
                         ac, hm, mayMiss (cp->quants[ac])));
            if (se->interleaveState && se->interleaveState[ac]) {
                ac++; continue;
            }
            if (mayMiss (cp->quants[ac])) {
                ac++; continue;
            }
            switch (cp->content[ac]->type) {
            case SCHEMA_CTYPE_KEYSPACE_END:
                /* Don't happen as INTERLEAVE child */
                cp->content[ac]->keySpace->active--;
                if (!cp->content[ac]->keySpace->active) {
                    if (cp->content[ac]->keySpace->unknownIDrefs) {
                        if (!recover (interp, sdata, INVALID_KEYREF_MATCH_END,
                                      NULL, NULL,
                                      cp->content[ac]->keySpace->name, 0)) {
                            return 0;
                        }
                        cp->content[ac]->keySpace->unknownIDrefs = 0;
                    }
                    Tcl_DeleteHashTable (&cp->content[ac]->keySpace->ids);
                }
                break;

            case SCHEMA_CTYPE_KEYSPACE:
                /* Don't happen as INTERLEAVE child */
                if (!cp->content[ac]->keySpace->active) {
                    Tcl_InitHashTable (&cp->content[ac]->keySpace->ids,
                                       TCL_STRING_KEYS);
                    cp->content[ac]->keySpace->active = 1;
                    cp->content[ac]->keySpace->unknownIDrefs = 0;
                } else {
                    cp->content[ac]->keySpace->active++;
                }
                break;
                
            case SCHEMA_CTYPE_TEXT:
                if (cp->content[ac]->nc) {
                    if (!checkText (interp, cp->content[ac], "")) {
                        if (recover (interp, sdata, MISSING_TEXT_MATCH_END,
                                     NULL, NULL, NULL, ac)) {
                            break;
                        }
                        return 0;
                    }
                }
                break;

            case SCHEMA_CTYPE_CHOICE:
                /* Don't happen as INTERLEAVE child */
                thismayskip = 0;
                for (i = 0; i < cp->content[ac]->nc; i++) {
                    if (mayMiss (cp->content[ac]->quants[i])) {
                        thismayskip = 1;
                        break;
                    }
                    ic = cp->content[ac]->content[i];
                    switch (ic->type) {
                    case SCHEMA_CTYPE_TEXT:
                        if (ic->nc) {
                            if (!checkText (interp, ic, "")) {
                                continue;
                            }
                        }
                        thismayskip = 1;
                        break;

                    case SCHEMA_CTYPE_NAME:
                    case SCHEMA_CTYPE_ANY:
                        continue;
                        
                    case SCHEMA_CTYPE_PATTERN:
                        if (recursivePattern (se, ic)) {
                            thismayskip = 1;
                            break;
                        }
                        /* fall throu */
                    case SCHEMA_CTYPE_INTERLEAVE:
                        pushToStack (sdata, ic);
                        if (checkElementEnd (interp, sdata) == -1) {
                            thismayskip = 1;
                        }
                        popStack (sdata);
                        break;
                        
                    case SCHEMA_CTYPE_KEYSPACE_END:
                    case SCHEMA_CTYPE_KEYSPACE:
                    case SCHEMA_CTYPE_VIRTUAL:
                    case SCHEMA_CTYPE_CHOICE:
                        Tcl_Panic ("Invalid CTYPE in MIXED or CHOICE");
                        
                    }
                    if (thismayskip) break;
                }
                if (thismayskip) break;
                if (!recover (interp, sdata, MISSING_ELEMENT_MATCH_END, NULL,
                              NULL, NULL, 0)) {
                    return 0;
                }
                if (sdata->recoverFlags & RECOVER_FLAG_MATCH_END_CONTINUE) {
                    updateStack (sdata, se, ac);
                    return 2;
                }
                break;
                
            case SCHEMA_CTYPE_VIRTUAL:
                if (evalVirtual (interp, sdata, ac)) break;
                else return 0;
                
            case SCHEMA_CTYPE_PATTERN:
                if (recursivePattern (se, cp->content[ac])) {
                    break;
                }
                /* fall throu */
            case SCHEMA_CTYPE_INTERLEAVE:
                pushToStack (sdata, cp->content[ac]);
                rc = checkElementEnd (interp, sdata);
                if (rc == 0) {
                    popStack (sdata);
                    if (sdata->stack->pattern->type == SCHEMA_CTYPE_NAME
                        || sdata->stack->activeChild
                        || sdata->stack->hasMatched) {
                        if (recover (interp, sdata, MISSING_ELEMENT_MATCH_END,
                                     NULL, NULL, NULL, 0)) {
                            if (sdata->recoverFlags &
                                RECOVER_FLAG_MATCH_END_CONTINUE) {
                                updateStack (sdata, se, ac);
                                return 2;
                            }
                            break;
                        }
                    }
                    return 0;
                }
                if (rc == 2) {
                    updateStack (sdata, se, ac);
                    return 2;
                }
                popStack (sdata);
                break;
                
            case SCHEMA_CTYPE_ANY:
            case SCHEMA_CTYPE_NAME:
                if (recover (interp, sdata, MISSING_ELEMENT_MATCH_END,
                             NULL, NULL, NULL, 0)) {
                    if (sdata->recoverFlags & RECOVER_FLAG_MATCH_END_CONTINUE) {
                        updateStack (sdata, se, ac);
                        return 2;
                    }
                    break;
                }
                return 0;
            }
            ac++;
        }
        if (se->interleaveState) {
            if (!mayskip) return 0;
        }
        if (isName) return 1;
        return -1;

    case SCHEMA_CTYPE_KEYSPACE_END:
    case SCHEMA_CTYPE_KEYSPACE:
    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");

    }
    /* Not reached */
    return 0;
}

static int
checkDocKeys (
    Tcl_Interp *interp,
    SchemaData *sdata
    ) 
{
    Tcl_HashEntry *h, *h1;
    Tcl_HashSearch search, search1;
    int haveErrMsg = 0;
    SchemaDocKey *dk;

    if (sdata->evalError) return 0;
    if (sdata->unknownIDrefs) {
        if (!recover (interp, sdata, UNKNOWN_ID, NULL, NULL, NULL, 0)) {
            haveErrMsg = 1;
            SetResult ("References to unknown IDs:");
            for (h = Tcl_FirstHashEntry (&sdata->ids, &search);
                 h != NULL;
                 h = Tcl_NextHashEntry (&search)) {
                if (Tcl_GetHashValue (h) == 0) {
                    Tcl_AppendResult (interp, " '",
                                      Tcl_GetHashKey (&sdata->ids, h),
                                      "'", NULL);
                }
            }
        }
    }
    if (sdata->idTables.numEntries) {
        for (h = Tcl_FirstHashEntry (&sdata->idTables, &search);
             h != NULL;
             h = Tcl_NextHashEntry (&search)) {
            dk = Tcl_GetHashValue (h);
            if (dk->unknownIDrefs) {
                if (!recover (interp, sdata, UNKNOWN_GLOBAL_ID, NULL, NULL,
                              NULL, 0)) {
                    if (haveErrMsg) {
                        Tcl_AppendResult (interp, "\n", NULL);
                    } else {
                        haveErrMsg = 1;
                    }
                    Tcl_AppendResult (interp,
                                      "References to unknown IDs in ID space '",
                                      Tcl_GetHashKey (&sdata->idTables, h),
                                      "':", NULL);
                    for (h1 = Tcl_FirstHashEntry (&dk->ids, &search1);
                         h1 != NULL;
                         h1 = Tcl_NextHashEntry (&search1)) {
                        if (Tcl_GetHashValue (h1) == 0) {
                            Tcl_AppendResult (interp, " '",
                                              Tcl_GetHashKey (&dk->ids, h1),
                                              "'", NULL);
                        }
                    }
                }
            }
        }
    }
    if (haveErrMsg) {
        sdata->validationState = VALIDATION_ERROR;
        return 0;
    }
    return 1;
}

int
tDOM_probeElementEnd (
    Tcl_Interp *interp,
    SchemaData *sdata
    )
{
    int rc;
    
    DBG(if (sdata->stack) {
        fprintf (stderr, "tDOM_probeElementEnd: look if current stack top can "
                 " end name: '%s' deep: %d\n",
                 sdata->stack->pattern->name, getDeep (sdata->stack));
        } else {fprintf (stderr, "stack is NULL\n");}
        );

    if (sdata->skipDeep) {
        sdata->skipDeep--;
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_ERROR) {
        return TCL_ERROR;
    }

    while (1) {
        rc = checkElementEnd (interp, sdata);
        while (rc == -1) {
            popStack (sdata);
            rc = checkElementEnd (interp, sdata);
        }
        sdata->recoverFlags &= ~RECOVER_FLAG_DONT_REPORT;
        if (rc == 2) {
            sdata->recoverFlags &= ~RECOVER_FLAG_MATCH_END_CONTINUE;
            continue;
        }
        if (rc == 1) {
            popStack (sdata);
            if (sdata->stack == NULL) {
                /* End of the first pattern (the tree root) without error. */
                /* Check for unknown ID references */
                if (!checkDocKeys (interp, sdata)) {
                    return TCL_ERROR;
                }
                /*  We have successfully finished validation */
                sdata->validationState = VALIDATION_FINISHED;
            }
            DBG(
                fprintf(stderr, "tDOM_probeElementEnd: _CAN_ end here.\n");
                serializeStack (sdata);
                );
            return TCL_OK;
        }
        break;
    }
    SetResultV ("Missing mandatory content");
    sdata->validationState = VALIDATION_ERROR;
    DBG(
        fprintf(stderr, "tDOM_probeElementEnd: CAN'T end here.\n");
        serializeStack (sdata);
        );
    return TCL_ERROR;
}

static int
matchText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    )
{
    SchemaCP *cp, *candidate, *ic;
    SchemaValidationStack *se;
    int hm, isName = 0, mayskip;
    unsigned int ac, i;
    
    DBG(fprintf (stderr, "matchText called with text '%s'\n", text));
    
    se = sdata->stack;
    getContext (cp, ac, hm);
    while (1) {
        switch (cp->type) {
        case SCHEMA_CTYPE_NAME:
            isName = 1;
            /* Fall through */
        case SCHEMA_CTYPE_PATTERN:
            while (ac < cp->nc) {
                candidate = cp->content[ac];
                switch (candidate->type) {
                case SCHEMA_CTYPE_TEXT:
                    if (checkText (interp, candidate, text)) {
                        updateStack (sdata, se, ac);
                        return 1;
                    }
                    if (sdata->evalError) return 0;
                    if (recover (interp, sdata, INVALID_VALUE, NULL, NULL,
                                 text, ac)) {
                        updateStack (sdata, se, ac);
                        return 1;
                    }
                    SetResult ("Invalid text content");
                    return 0;

                case SCHEMA_CTYPE_CHOICE:
                    if (candidate->flags & MIXED_CONTENT) {
                        updateStack (sdata, se, ac);
                        return 1;
                    }
                    for (i = 0; i < candidate->nc; i++) {
                        ic = candidate->content[i];
                        switch (ic->type) {
                        case SCHEMA_CTYPE_TEXT:
                            if (checkText (interp, ic, text)) {
                                updateStack (sdata, se, ac);
                                return 1;
                            }
                            break;

                        case SCHEMA_CTYPE_NAME:
                        case SCHEMA_CTYPE_ANY:
                            break;

                        case SCHEMA_CTYPE_PATTERN:
                            if (recursivePattern (se, ic)) {
                                break;
                            }
                            /* fall throu */
                        case SCHEMA_CTYPE_INTERLEAVE:
                            pushToStack (sdata, ic);
                            if (matchText (interp, sdata, text)) {
                                updateStack (sdata, se, ac);
                                return 1;
                            }
                            popStack (sdata);
                            break;

                        case SCHEMA_CTYPE_VIRTUAL:
                            Tcl_Panic ("Virtual constrain in MIXED or"
                                       " CHOICE");
                            
                        case SCHEMA_CTYPE_CHOICE:
                            Tcl_Panic ("MIXED or CHOICE child of MIXED or"
                                       " CHOICE");

                        case SCHEMA_CTYPE_KEYSPACE_END:
                        case SCHEMA_CTYPE_KEYSPACE:
                            Tcl_Panic ("Keyspace constrain in MIXED or"
                                       " CHOICE");
                            
                        }
                    }
                    if (mustMatch (cp->quants[ac], hm)) {
                        if (recover (interp, sdata, UNEXPECTED_TEXT,
                                     NULL, NULL, text, 0)) {
                            return 1;
                        }
                        SetResultV ("Unexpected text content");
                        return 0;
                    }
                    break;

                case SCHEMA_CTYPE_PATTERN:
                    if (recursivePattern (se, candidate)) {
                        break;
                    }
                    /* fall throu */
                case SCHEMA_CTYPE_INTERLEAVE:
                    pushToStack (sdata, candidate);
                    if (matchText (interp, sdata, text)) {
                        updateStack (sdata, se, ac);
                        return 1;
                    }
                    popStack (sdata);
                    if (mustMatch (cp->quants[ac], hm)) {
                        if (recover (interp, sdata, UNEXPECTED_TEXT,
                                     NULL, NULL, text, 0)) {
                            return 1;
                        }
                        SetResultV ("Unexpected text content");
                        return 0;
                    }
                    break;

                case SCHEMA_CTYPE_VIRTUAL:
                    if (!evalVirtual (interp, sdata, ac)) return 0;
                    break;

                case SCHEMA_CTYPE_KEYSPACE:
                    if (!cp->content[ac]->keySpace->active) {
                        Tcl_InitHashTable (&cp->content[ac]->keySpace->ids,
                                           TCL_STRING_KEYS);
                        cp->content[ac]->keySpace->active = 1;
                        cp->content[ac]->keySpace->unknownIDrefs = 0;
                    } else {
                        cp->content[ac]->keySpace->active++;
                    }
                    break;
                    
                case SCHEMA_CTYPE_KEYSPACE_END:
                    cp->content[ac]->keySpace->active--;
                    if (!cp->content[ac]->keySpace->active) {
                        if (cp->content[ac]->keySpace->unknownIDrefs) {
                            if (!recover (interp, sdata,
                                          INVALID_KEYREF_MATCH_TEXT, NULL,
                                          NULL, text, ac)) {
                                return 0;
                            }
                            cp->content[ac]->keySpace->unknownIDrefs = 0;
                        }
                        Tcl_DeleteHashTable (&cp->content[ac]->keySpace->ids);
                    }
                    break;
                    
                case SCHEMA_CTYPE_NAME:
                case SCHEMA_CTYPE_ANY:
                    if (mustMatch (cp->quants[ac], hm)) {
                        if (recover (interp, sdata, UNEXPECTED_TEXT,
                                     NULL, NULL, text, ac)) {
                            return 1;
                        }
                        SetResultV ("Unexpected text content");
                        return 0;
                    }
                    break;

                }
                ac++;
            }
            if (isName) {
                if (recover (interp, sdata, UNEXPECTED_TEXT, NULL,
                             NULL, text, 0)) {
                    return 1;
                }
                SetResultV ("Unexpected text content");
                return 0;
            }
            popStack (sdata);
            se = sdata->stack;
            getContext (cp, ac, hm);
            ac++;
            continue;

        case SCHEMA_CTYPE_KEYSPACE:
        case SCHEMA_CTYPE_KEYSPACE_END:
        case SCHEMA_CTYPE_VIRTUAL:
        case SCHEMA_CTYPE_CHOICE:
        case SCHEMA_CTYPE_TEXT:
        case SCHEMA_CTYPE_ANY:
            /* Never pushed onto stack */
            Tcl_Panic ("Invalid CTYPE onto the validation stack!");
            break;

        case SCHEMA_CTYPE_INTERLEAVE:
            mayskip = 1;
            for (i = 0; i < cp->nc; i++) {
                if (se->interleaveState[i]) {
                    if (maxOne (cp->quants[i])) continue;
                } else {
                    if (minOne (cp->quants[i])) mayskip = 0;
                }
                ic = cp->content[i];
                switch (ic->type) {
                case SCHEMA_CTYPE_TEXT:
                    if (checkText (interp, ic, text)) {
                        if (!(sdata->recoverFlags & RECOVER_FLAG_REWIND)) {
                            se->hasMatched = 1;
                            se->interleaveState[i] = 1;
                        }
                        return 1;
                    }
                    break;

                case SCHEMA_CTYPE_NAME:
                case SCHEMA_CTYPE_ANY:
                    break;

                case SCHEMA_CTYPE_PATTERN:
                    if (recursivePattern (se, ic)) {
                        break;
                    }
                    /* fall throu */
                case SCHEMA_CTYPE_INTERLEAVE:
                    pushToStack (sdata, ic);
                    if (matchText (interp, sdata, text)) {
                        updateStack (sdata, se, ac);
                        return 1;
                    }
                    popStack (sdata);
                    break;

                case SCHEMA_CTYPE_CHOICE:
                    Tcl_Panic ("MIXED or CHOICE child of INTERLEAVE");

                case SCHEMA_CTYPE_KEYSPACE_END:
                case SCHEMA_CTYPE_KEYSPACE:
                    Tcl_Panic ("Keyspace child of INTERLEAVE");

                case SCHEMA_CTYPE_VIRTUAL:
                    break;
                    
                }
            }
            if (!mayskip) {
                if (recover (interp, sdata, UNEXPECTED_TEXT, NULL, NULL, text,
                             ac)) {
                    return 1;
                }
                SetResultV ("Unexpected text content");
                return 0;
            }
            popStack (sdata);
            se = sdata->stack;
            getContext (cp, ac, hm);
            ac++;
            continue;
        }
        /* Not reached, but this is inside a while (1) {} loop ...*/
        break;
    }
    /* Not reached, but at least makes the compiler happy. */
    return 0;
}

int
tDOM_probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text,
    int *only_whites
    )
{
    int myonly_whites;
    char *pc;

    DBG(fprintf (stderr, "tDOM_probeText started, text: '%s'\n", text);)
    if (sdata->skipDeep) {
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }

    if (sdata->stack->pattern->flags & CONSTRAINT_TEXT_CHILD) {
        if (!*text && sdata->stack->pattern->nc == 0) {
            return TCL_OK;
        }
        if (matchText (interp, sdata, text)) {
            CHECK_REWIND;
            return TCL_OK;
        }
    } else {
        if (only_whites) {
            myonly_whites = *only_whites;
        } else {
            myonly_whites = 1;
            pc = text;
            while (SPACE (*pc)) pc++;
            if (*pc) myonly_whites = 0;
        }
        if (myonly_whites)  return TCL_OK;
        if (matchText (interp, sdata, text)) {
            CHECK_REWIND;
            return TCL_OK;
        }
    }
    if (!sdata->evalError) {
        SetResult ("Text content doesn't match");
    }
    return TCL_ERROR;
}

static void
startElement(
    void         *userData,
    const char   *name,
    const char  **atts
)
{
    ValidateMethodData *vdata = (ValidateMethodData *) userData;
    SchemaData *sdata;
    char *namespace;
    const char *s;
    int i = 0;

    DBG(fprintf (stderr, "startElement: '%s'\n", name);)
    sdata = vdata->sdata;
    if (!sdata->skipDeep && sdata->stack && Tcl_DStringLength (vdata->cdata)) {
        if (tDOM_probeText (vdata->interp, sdata,
                       Tcl_DStringValue (vdata->cdata), NULL) != TCL_OK) {
            sdata->validationState = VALIDATION_ERROR;
            XML_StopParser (vdata->parser, 0);
            Tcl_DStringSetLength (vdata->cdata, 0);
            vdata->onlyWhiteSpace = 1;
            return;
        }
        Tcl_DStringSetLength (vdata->cdata, 0);
        vdata->onlyWhiteSpace = 1;
    }
    s = name;
    while (*s && *s != '\xFF') {
        i++; s++;
    }
    namespace = NULL;
    if (*s == '\xFF') {
        s++;
        if (i) {
            if (i >= vdata->maxUriLen - 1) {
                vdata->uri = (char *) REALLOC (vdata->uri, vdata->maxUriLen * 2);
                vdata->maxUriLen *= 2;
            }
            memcpy (vdata->uri, name, i);
            vdata->uri[i] = '\0';
            namespace = vdata->uri;
        }
    } else {
        s = name;
    }

    if (tDOM_probeElement (vdata->interp, sdata, s, namespace)
        != TCL_OK) {
        sdata->validationState = VALIDATION_ERROR;
        XML_StopParser (vdata->parser, 0);
        return;
    }
    if (sdata->skipDeep == 0
        && (atts[0] || (sdata->stack && sdata->stack->pattern->attrs))) {
        if (tDOM_probeAttributes (vdata->interp, sdata, atts)
            != TCL_OK) {
            sdata->validationState = VALIDATION_ERROR;
            XML_StopParser (vdata->parser, 0);
        }
    }
}

static void
endElement (
    void        *userData,
    const char  *name
)
{
    ValidateMethodData *vdata = (ValidateMethodData *) userData;
    SchemaData *sdata;
    
    DBG(fprintf (stderr, "endElement: '%s'\n", name);)
    sdata = vdata->sdata;
    if (sdata->validationState == VALIDATION_ERROR) {
        return;
    }
    if (!sdata->skipDeep && sdata->stack && Tcl_DStringLength (vdata->cdata)) {
        if (tDOM_probeText (vdata->interp, sdata,
                       Tcl_DStringValue (vdata->cdata), NULL) != TCL_OK) {
            sdata->validationState = VALIDATION_ERROR;
            XML_StopParser (vdata->parser, 0);
            Tcl_DStringSetLength (vdata->cdata, 0);
            vdata->onlyWhiteSpace = 1;
            return;
        }
    }
    if (Tcl_DStringLength (vdata->cdata)) {
        Tcl_DStringSetLength (vdata->cdata, 0);
        vdata->onlyWhiteSpace = 1;
    }
    if (tDOM_probeElementEnd (vdata->interp, sdata)
        != TCL_OK) {
        sdata->validationState = VALIDATION_ERROR;
        XML_StopParser (vdata->parser, 0);
    }
}

static void
characterDataHandler (
    void        *userData,
    const char  *s,
    int          len
)
{
    ValidateMethodData *vdata = (ValidateMethodData *) userData;
    const char *pc;
    
    if (vdata->onlyWhiteSpace) {
        int i = 0;
        pc = s;
        while (i < len) {
            if ( (*pc == ' ')  ||
                 (*pc == '\n') ||
                 (*pc == '\r') ||
                 (*pc == '\t') ) {
                pc++;
                i++;
                continue;
            }
            vdata->onlyWhiteSpace = 0;
            break;
        }
    }
    Tcl_DStringAppend (vdata->cdata, s, len);
}

static void
schemaxpathRSFree (
    xpathResultSet *rs
    )
{
    if (rs->type == StringResult) FREE (rs->string);
    FREE (rs->nodes);
}

static int
checkdomKeyConstraints (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domNode    *node
    )
{
    xpathResultSet nodeList, rs, frs;
    domKeyConstraint *kc;
    domNode *n;
    domAttrNode *attr;
    int rc, i, j, hnew, len, skip, first;
    char *errMsg = NULL, *keystr, *efsv;
    Tcl_HashTable htable;
    Tcl_DString dStr;
    xpathCBs cbs;

    kc = sdata->stack->pattern->domKeys;
    memset (&nodeList, 0, sizeof (xpathResultSet));
    nodeList.type = EmptyResult;
    memset (&rs, 0, sizeof (xpathResultSet));
    rs.type = EmptyResult;
    memset (&frs, 0, sizeof (xpathResultSet));
    frs.type = EmptyResult;
    Tcl_DStringInit (&dStr);
    xpathRSReset (&nodeList, node);
    cbs.funcCB         = tcldom_xpathFuncCallBack;
    cbs.funcClientData = interp;
    cbs.varCB          = NULL;
    cbs.varClientData  = NULL;
    while (kc) {
        xpathRSReset (&rs, NULL);
        rc = xpathEvalAst (kc->selector, &nodeList, node, &cbs, &rs, &errMsg);
        if (rc) {
            SetResult (errMsg);
            goto booleanErrorCleanup;
        }
        if (kc->flags & DKC_FLAG_BOOLEAN) {
            i = xpathFuncBoolean (&rs);
            if (!i) {
                if (!recover (interp, sdata, DOM_XPATH_BOOLEAN, kc->name,
                              NULL, NULL, 0)) {
                    SetResultV ("INVALID_DOM_XPATH_BOOLEAN");
                    goto booleanErrorCleanup;
                }
            }
            kc = kc->next;
            continue;
        }
        if (rs.type == EmptyResult) goto nextConstraint;
        if (rs.type != xNodeSetResult) {
            SetResult ("INVALID_DOM_KEYCONSTRAINT");
            goto errorCleanup;
        }
        Tcl_InitHashTable (&htable, TCL_STRING_KEYS);
        for (i = 0; i < rs.nr_nodes; i++) {
            n = rs.nodes[i];
            if (n->nodeType != ELEMENT_NODE) {
                SetResult ("INVALID_DOM_KEYCONSTRAINT");
                goto errorCleanup;
            }
            xpathRSReset (&nodeList, n);
            if (kc->nrFields == 1) {
                xpathRSReset (&frs, NULL);
                rc = xpathEvalAst (kc->fields[0], &nodeList, n, &cbs, &frs,
                                   &errMsg);
                if (rc) {
                    SetResult (errMsg);
                    goto errorCleanup;
                }
                if (frs.type != xNodeSetResult && frs.type != EmptyResult) {
                    SetResult ("INVALID_DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.type == EmptyResult || frs.nr_nodes == 0) {
                    if (kc->flags & DKC_FLAG_IGNORE_EMPTY_FIELD_SET) {
                        continue;
                    }
                    efsv = "";
                    if (kc->emptyFieldSetValue) {
                        efsv = kc->emptyFieldSetValue;
                    }
                    Tcl_CreateHashEntry (&htable, efsv, &hnew);
                    if (!hnew) {
                        if (recover (interp, sdata, DOM_KEYCONSTRAINT,
                                     kc->name, NULL, efsv, 0)) {
                            break;
                        }
                        SetResultV ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    continue;
                }
                if (frs.nr_nodes != 1) {
                    if (recover (interp, sdata, DOM_KEYCONSTRAINT, kc->name,
                                 NULL, NULL, 0)) {
                        break;
                    }
                    SetResultV ("DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.nodes[0]->nodeType != ELEMENT_NODE
                    && frs.nodes[0]->nodeType != ATTRIBUTE_NODE) {
                    SetResult ("INVALID_DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                    attr = (domAttrNode *) frs.nodes[0];
                    Tcl_CreateHashEntry (&htable, attr->nodeValue, &hnew);
                    if (!hnew) {
                        if (recover (interp, sdata, DOM_KEYCONSTRAINT,
                                     kc->name, NULL, attr->nodeValue, 0)) {
                            break;
                        }
                        SetResultV ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                } else {
                    keystr = xpathGetStringValue (frs.nodes[0], &len);
                    Tcl_CreateHashEntry (&htable, keystr, &hnew);
                    if (!hnew) {
                        if (recover (interp, sdata, DOM_KEYCONSTRAINT,
                                     kc->name, NULL, keystr, 0)) {
                            FREE(keystr);
                            break;
                        }
                        FREE(keystr);
                        SetResultV ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    FREE(keystr);
                }
            } else {
                Tcl_DStringSetLength (&dStr, 0);
                skip = 0;
                first = 1;
                for (j = 0; j < kc->nrFields; j++) {
                    xpathRSReset (&frs, NULL);
                    rc = xpathEvalAst (kc->fields[j], &nodeList, n, &cbs,
                                       &frs, &errMsg);
                    if (rc) {
                        SetResult (errMsg);
                        goto errorCleanup;
                    }
                    if (frs.type != xNodeSetResult
                        && frs.type != EmptyResult) {
                        SetResult ("INVALID_DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (frs.type == EmptyResult || frs.nr_nodes == 0) {
                        if (kc->flags & DKC_FLAG_IGNORE_EMPTY_FIELD_SET) {
                            continue;
                        }
                        if (kc->emptyFieldSetValue) {
                            if (first) first = 0;
                            else Tcl_DStringAppend (&dStr, ":", 1);
                            Tcl_DStringAppend (&dStr, kc->emptyFieldSetValue,
                                               kc->efsv_len);
                        } else {
                            if (first) first = 0;
                            else Tcl_DStringAppend (&dStr, ":", 1);
                        }
                        continue;
                    }
                    if (frs.nr_nodes != 1) {
                        if (recover (interp, sdata, DOM_KEYCONSTRAINT,
                                     kc->name, NULL, NULL, 0)) {
                            skip = 1;
                            break;
                        }
                        SetResultV ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (frs.nodes[0]->nodeType != ELEMENT_NODE
                        && frs.nodes[0]->nodeType != ATTRIBUTE_NODE) {
                        SetResult ("INVALID_DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (first) first = 0;
                    else Tcl_DStringAppend (&dStr, ":", 1);
                    if (frs.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                        attr = (domAttrNode *) frs.nodes[0];
                        Tcl_DStringAppend (&dStr, attr->nodeValue,
                                           attr->valueLength);
                    } else {
                        keystr = xpathGetStringValue (frs.nodes[0], &len);
                        Tcl_DStringAppend (&dStr, keystr, len);
                        FREE (keystr);
                    }
                }
                if (skip) break;
                Tcl_CreateHashEntry (&htable, Tcl_DStringValue (&dStr), &hnew);
                if (!hnew) {
                    if (recover (interp, sdata, DOM_KEYCONSTRAINT, 
                                 kc->name, NULL, Tcl_DStringValue (&dStr),
                                 0)) {
                        break;
                    }
                    SetResultV ("DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
            }
        }
        Tcl_DeleteHashTable (&htable);
    nextConstraint:
        kc = kc->next;
    }
    schemaxpathRSFree (&frs);
    schemaxpathRSFree (&rs);
    schemaxpathRSFree (&nodeList);
    return TCL_OK;

errorCleanup:
    Tcl_DeleteHashTable (&htable);
booleanErrorCleanup:
    schemaxpathRSFree (&frs);
    schemaxpathRSFree (&rs);
    schemaxpathRSFree (&nodeList);
    return TCL_ERROR;
}

static int
validateDOM (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domNode    *node
    )
{
    char *ln;
    domNode *savednode, *savedinsideNode;

    if (node->namespace) {
        if (node->ownerDocument->namespaces[node->namespace-1]->prefix[0] == '\0') {
            ln = node->nodeName;
        } else {
            ln = node->nodeName;
            while (*ln && (*ln != ':')) {
                ln++;
            }
            if (*ln == ':') {
                ln++;
            } else {
                /* Ups? */
                ln = node->nodeName;
            }
        }
    } else {
        ln = node->nodeName;
    }
    savednode = sdata->node;
    sdata->node = node;
    if (tDOM_probeElement (interp, sdata, ln,
                      node->namespace ?
                      node->ownerDocument->namespaces[node->namespace-1]->uri
                      : NULL)
        != TCL_OK) {
        return TCL_ERROR;
    }
    /* In case of UNKNOWN_ROOT_ELEMENT and reportCmd is set
     * sdata->stack is NULL. */
    if (!sdata->stack) return TCL_OK;
    if (sdata->skipDeep == 0) {
        if (node->firstAttr) {
            if (tDOM_probeDomAttributes (interp, sdata, node->firstAttr)
                != TCL_OK) {
                return TCL_ERROR;
            }
        } else {
            if (sdata->stack->pattern->numReqAttr) {
                /* tDOM_probeDomAttributes fills interp result with a
                 * msg which required attributes are missing in case
                 * of no reportCmd. In case of reportCmd
                 * tDOM_probeDomAttributes() returns only error in the
                 * case of error in called scripts. */
                if (tDOM_probeDomAttributes (interp, sdata, NULL) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
        }
    }

    if (sdata->stack->pattern->domKeys) {
        if (checkdomKeyConstraints (interp, sdata, node) != TCL_OK)
            return TCL_ERROR;
    }

    savedinsideNode = sdata->insideNode;
    sdata->insideNode = node;
    node = node->firstChild;
    while (node) {
        switch (node->nodeType) {
        case ELEMENT_NODE:
            if (Tcl_DStringLength (sdata->cdata)) {
                if (tDOM_probeText (interp, sdata,
                               Tcl_DStringValue (sdata->cdata), NULL) != TCL_OK)
                    return TCL_ERROR;
                Tcl_DStringSetLength (sdata->cdata, 0);
            }
            if (validateDOM (interp, sdata, node) != TCL_OK) return TCL_ERROR;
            break;

        case TEXT_NODE:
        case CDATA_SECTION_NODE:
            Tcl_DStringAppend (sdata->cdata,
                               ((domTextNode *) node)->nodeValue,
                               ((domTextNode *) node)->valueLength);
            if (tDOM_probeText (interp, sdata,
                                Tcl_DStringValue (sdata->cdata), NULL) != TCL_OK) {
                Tcl_DStringSetLength (sdata->cdata, 0);
                return TCL_ERROR;
            }
            Tcl_DStringSetLength (sdata->cdata, 0);
            break;

        case COMMENT_NODE:
        case PROCESSING_INSTRUCTION_NODE:
            /* They are just ignored by validation. */
            break;

        default:
            SetResult ("Unexpected node type in validateDOM!");
            return TCL_ERROR;
        }
        node = node->nextSibling;
    }
    if (tDOM_probeElementEnd (interp, sdata) != TCL_OK) return TCL_ERROR;
    sdata->node = savednode;
    sdata->insideNode = savedinsideNode;
    return TCL_OK;
}

static void
schemaReset (
    SchemaData *sdata
    )
{
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaDocKey *dk;
    SchemaKeySpace *ks;

    while (sdata->stack) popStack (sdata);
    while (sdata->lastMatchse) popFromStack (sdata, &sdata->lastMatchse);
    sdata->recoverFlags = 0;
    sdata->validationState = VALIDATION_READY;
    sdata->skipDeep = 0;
    sdata->evalError = 0;
    sdata->vaction = 0;
    sdata->vname = NULL;
    sdata->vns = NULL;
    sdata->vtext = NULL;
    Tcl_DStringSetLength (sdata->cdata, 0);
    if (sdata->ids.numEntries) {
        Tcl_DeleteHashTable (&sdata->ids);
        Tcl_InitHashTable (&sdata->ids, TCL_STRING_KEYS);
        sdata->unknownIDrefs = 0;
    }
    if (sdata->idTables.numEntries) {
        for (h = Tcl_FirstHashEntry (&sdata->idTables, &search);
             h != NULL;
             h = Tcl_NextHashEntry (&search)) {
            dk = Tcl_GetHashValue (h);
            if ((&dk->ids)->numEntries) {
                Tcl_DeleteHashTable (&dk->ids);
                Tcl_InitHashTable (&dk->ids, TCL_STRING_KEYS);
                dk->unknownIDrefs = 0;
            }
        }
    }
    if (sdata->keySpaces.numEntries) {
        for (h = Tcl_FirstHashEntry (&sdata->keySpaces, &search);
             h != NULL;
             h = Tcl_NextHashEntry (&search)) {
            ks = Tcl_GetHashValue (h);
            if (ks->active && ks->ids.numEntries) {
                Tcl_DeleteHashTable (&ks->ids);
                Tcl_InitHashTable (&ks->ids, TCL_STRING_KEYS);
            }
            ks->unknownIDrefs = 0;
            ks->active = 0;
        }
    }
    sdata->parser = NULL;
    sdata->node = NULL;
    sdata->insideNode = NULL;
}

void
tDOM_schemaReset (
    SchemaData *sdata,
    int lookforCleanup
    )
{
    if (lookforCleanup) {
        if (sdata->cleanupAfterUse && sdata->inuse == 0
            && sdata->currentEvals == 0) {
            schemaInstanceDelete (sdata);
            return;
        }
    }
    schemaReset (sdata);
}

static int
evalConstraints (
    Tcl_Interp *interp,
    SchemaData *sdata,
    SchemaCP *cp,
    Tcl_Obj *script
    )
{
    int result, savedIsTextConstraint;
    SchemaCP *savedCP;
    unsigned int savedContenSize;

    /* Save some state of sdata .. */
    savedCP = sdata->cp;
    savedContenSize = sdata->contentSize;
    savedIsTextConstraint = sdata->isTextConstraint;
    /* ... and prepare sdata for definition evaluation. */
    sdata->cp = cp;
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
    sdata->isTextConstraint = 1;
    sdata->textStub[3] = script;
    sdata->currentEvals++;
    result = Tcl_EvalObjv (interp, 4, sdata->textStub, TCL_EVAL_GLOBAL);
    sdata->currentEvals--;
    /* ... and restore the previously saved sdata states  */
    sdata->isTextConstraint = savedIsTextConstraint;
    sdata->cp = savedCP;
    sdata->contentSize = savedContenSize;
    return result;
}

/* cp must be of type SCHEMA_CTYPE_NAME for useful results */
static Tcl_Obj*
serializeElementName (
    Tcl_Interp *interp,
    SchemaCP *cp
    )
{
    Tcl_Obj *rObj;

    rObj = Tcl_NewObj();
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewStringObj (cp->name, -1));
    if (cp->namespace) {
        Tcl_ListObjAppendElement (interp, rObj,
                                  Tcl_NewStringObj (cp->namespace, -1));
    }
    return rObj;
}

static Tcl_Obj*
serializeElementTypeName (
    Tcl_Interp *interp,
    SchemaCP *cp
    )
{
    Tcl_Obj *rObj;

    rObj = Tcl_NewObj();
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewStringObj (cp->name, -1));
    if (cp->namespace) {
        Tcl_ListObjAppendElement (interp, rObj,
                                  Tcl_NewStringObj (cp->namespace, -1));
    }
    return rObj;
}

/* cp must be of type SCHEMA_CTYPE_ANY for useful results */
static Tcl_Obj*
serializeAnyCP (
    Tcl_Interp *interp,
    SchemaCP *cp
    )
{
    Tcl_Obj *rObj, *nslistLObj;
    Tcl_HashTable *t;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;

    rObj = Tcl_NewObj();
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewStringObj ("<any>", 5));
    if (cp->namespace || cp->typedata) {
        nslistLObj = Tcl_NewObj();
        if (cp->namespace) {
            Tcl_ListObjAppendElement (interp, nslistLObj,
                                      Tcl_NewStringObj (cp->namespace, -1));
        }
        if (cp->typedata) {
            t = (Tcl_HashTable *)cp->typedata;
            for (h = Tcl_FirstHashEntry (t, &search); h != NULL;
                 h = Tcl_NextHashEntry (&search)) {
                Tcl_ListObjAppendElement (
                    interp, nslistLObj,
                    Tcl_NewStringObj (Tcl_GetHashKey (t, h), -1)
                    );
            }
        }
        Tcl_ListObjAppendElement (interp, rObj, nslistLObj);
    } else {
        Tcl_ListObjAppendElement (interp, rObj, Tcl_NewObj());
    }
    return rObj;
}

/* The cp argument may be NULL. If it isn't NULL cp must be of type
 * SCHEMA_CTYPE_TEXT for useful results */
static Tcl_Obj*
serializeTextCP (
    Tcl_Interp *interp,
    SchemaCP *cp
    )
{
    Tcl_Obj *rObj;

    rObj = Tcl_NewObj();
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewStringObj ("#text", 5));
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewObj());
    return rObj;
}

static Tcl_Obj*
serializeElementEnd (
    Tcl_Interp *interp
    )
{
    Tcl_Obj *rObj;

    rObj = Tcl_NewObj();
    Tcl_ListObjAppendElement (interp, rObj,
                              Tcl_NewStringObj ("<elementend>", 12));
    Tcl_ListObjAppendElement (interp, rObj, Tcl_NewObj());
    return rObj;
}

static void
definedElements (
    Tcl_HashTable *htable,
    Tcl_Interp *interp
    )
{
    Tcl_Obj *rObj, *elmObj;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaCP *cp;
    
    rObj = Tcl_GetObjResult (interp);
    for (h = Tcl_FirstHashEntry (htable, &search);
         h != NULL;
         h = Tcl_NextHashEntry (&search)) {
        cp = (SchemaCP *) Tcl_GetHashValue (h);
        while (cp) {
            if (cp->flags & FORWARD_PATTERN_DEF
                || cp->flags & PLACEHOLDER_PATTERN_DEF) {
                cp = cp->next;
                continue;
            }
            elmObj = serializeElementName (interp, cp);
            Tcl_ListObjAppendElement (interp, rObj, elmObj);
            cp = cp->next;
        }
    }
}

static void
definedElementtypes (
    SchemaData *sdata,
    Tcl_Interp *interp
    )
{
    Tcl_Obj *rObj, *elmObj;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaCP *cp;
    
    rObj = Tcl_GetObjResult (interp);
    for (h = Tcl_FirstHashEntry (&sdata->elementType, &search);
         h != NULL;
         h = Tcl_NextHashEntry (&search)) {
        cp = (SchemaCP *) Tcl_GetHashValue (h);
        while (cp) {
            if (cp->flags & FORWARD_PATTERN_DEF
                || cp->flags & PLACEHOLDER_PATTERN_DEF) {
                cp = cp->next;
                continue;
            }
            if (cp->flags & FORWARD_PATTERN_DEF
                || cp->flags & PLACEHOLDER_PATTERN_DEF) continue;
            elmObj = serializeElementTypeName (interp, cp);
            Tcl_ListObjAppendElement (interp, rObj, elmObj);
            cp = cp->next;
        }
    }
}

static int
getNextExpectedWorker (
    SchemaData *sdata,
    SchemaValidationStack *se,
    Tcl_Interp *interp,
    Tcl_HashTable *seenCPs,
    Tcl_Obj *rObj,
    int expectedFlags
    )
{
    int hm, hnew, mustMatch, mayskip, rc = 1, probeMayskip = 0;
    unsigned int ac, i;
    SchemaCP *cp, *ic, *jc;
    SchemaValidationStack *se1;

    if (expectedFlags & EXPECTED_PROBE_MAYSKIP) {
        probeMayskip = 1;
    }
    getContext (cp, ac, hm);
    if ((expectedFlags & EXPECTED_IGNORE_MATCHED
         || expectedFlags & EXPECTED_ONLY_MANDATORY)
        && hm) {
        ac++;
        hm = 0;
    } else {
        if (hm && maxOne(cp->quants[ac])) {
            ac++;
            hm = 0;
        }
    }
    switch (cp->type) {
    case SCHEMA_CTYPE_INTERLEAVE:
        ac = 0;
        mustMatch = 0;
        /* Fall through */
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
        while (ac < cp->nc) {
            if (se->interleaveState
                && se->interleaveState[ac]
                && maxOne (cp->quants[ac])) {
                ac++;
                hm = 0;
                continue;
            }
            if (expectedFlags & EXPECTED_ONLY_MANDATORY
                && !(mustMatch (cp->quants[ac], hm))) {
                ac++;
                hm = 0;
                continue;
            }
            ic = cp->content[ac];
            mayskip = 0;
            switch (ic->type) {
            case SCHEMA_CTYPE_NAME:
                if (probeMayskip) break;
                Tcl_ListObjAppendElement (interp, rObj,
                                          serializeElementName (interp, ic));
                break;
            case SCHEMA_CTYPE_PATTERN:
                if (recursivePattern (se, ic)) {
                    break;
                }
                /* Fall through */
            case SCHEMA_CTYPE_INTERLEAVE:
                if (expectedFlags & EXPECTED_ONLY_MANDATORY
                    && !se->hasMatched) {
                    expectedFlags |= EXPECTED_PROBE_MAYSKIP;
                    se1 = getStackElement (sdata, ic);
                    mayskip = getNextExpectedWorker (sdata, se1, interp,
                                                     seenCPs, rObj,
                                                     expectedFlags);
                    repoolStackElement (sdata, se1);
                    if (!probeMayskip) {
                        expectedFlags &= ~EXPECTED_PROBE_MAYSKIP;
                    }
                    if (mayskip) break;
                }
                if (probeMayskip) break;
                Tcl_CreateHashEntry (seenCPs, ic, &hnew);
                if (hnew) {
                    se1 = getStackElement (sdata, ic);
                    mayskip = getNextExpectedWorker (sdata, se1, interp,
                                                     seenCPs, rObj,
                                                     expectedFlags);
                    repoolStackElement (sdata, se1);
                }
                break;

            case SCHEMA_CTYPE_ANY:
                if (probeMayskip) break;
                if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)
                    || minOne (cp->quants[ac])) {
                    Tcl_ListObjAppendElement (interp, rObj,
                                              serializeAnyCP (interp, ic));
                }
                break;

            case SCHEMA_CTYPE_TEXT:
                if (ic->nc == 0 || checkText (interp, ic, "")) {
                    mayskip = 1;
                }
                if (probeMayskip) break;
                if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)
                    || mayskip == 0) {
                    Tcl_ListObjAppendElement (interp, rObj,
                                              serializeTextCP (interp, ic));
                }
                break;
                
            case SCHEMA_CTYPE_CHOICE:
                if (probeMayskip) {
                    for (i = 0; i < ic->nc; i++) {
                        if (mayMiss (ic->quants[i])) {
                            mayskip = 1;
                            break;
                        }
                        jc = ic->content[i];
                        switch (jc->type) {
                        case SCHEMA_CTYPE_PATTERN:
                            if (recursivePattern (se, ic)) {
                                mayskip = 1;
                                break;
                            }
                            /* fall throu */
                        case SCHEMA_CTYPE_INTERLEAVE:
                            se1 = getStackElement (sdata, ic);
                            mayskip = getNextExpectedWorker (
                                sdata, se1, interp, seenCPs, rObj,
                                expectedFlags
                                );
                            repoolStackElement (sdata, se1);
                            break;
                        case SCHEMA_CTYPE_TEXT:
                            if (ic->nc == 0 || checkText (interp, ic, "")) {
                                mayskip = 1;
                            }
                            break;
                        default:
                            break;
                        }
                        if (mayskip) break;
                    }
                    break;
                }
                if (ic->flags & MIXED_CONTENT) {
                    if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)) {
                        Tcl_ListObjAppendElement (
                            interp, rObj, serializeTextCP (interp, NULL));
                    }
                }
                for (i = 0; i < ic->nc; i++) {
                    jc = ic->content[i];
                    switch (jc->type) {
                    case SCHEMA_CTYPE_NAME:
                        if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)
                            || minOne (cp->quants[i])) {
                            Tcl_ListObjAppendElement (
                                interp, rObj, serializeElementName (interp, jc)
                                );
                        }
                        break;
                    case SCHEMA_CTYPE_PATTERN:
                        if (recursivePattern (se, jc)) {
                            break;
                        }
                        /* Fall through */
                    case SCHEMA_CTYPE_INTERLEAVE:
                        Tcl_CreateHashEntry (seenCPs, jc, &hnew);
                        if (hnew) {
                            se1 = getStackElement (sdata, jc);
                            mayskip = getNextExpectedWorker (
                                sdata, se1, interp, seenCPs, rObj,
                                expectedFlags
                                );
                            repoolStackElement (sdata, se1);
                        }
                        break;
                    case SCHEMA_CTYPE_ANY:
                        if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)
                            || minOne (cp->quants[i])) {
                            Tcl_ListObjAppendElement (
                                interp, rObj, serializeAnyCP (interp, jc)
                                );
                        }
                        break;
                    case SCHEMA_CTYPE_TEXT:
                        if (!(expectedFlags & EXPECTED_ONLY_MANDATORY)
                            || minOne (cp->quants[i])) {
                            Tcl_ListObjAppendElement (
                                interp, rObj, serializeTextCP (interp, jc)
                                );
                        }
                        break;
                    case SCHEMA_CTYPE_CHOICE:
                        Tcl_Panic ("MIXED or CHOICE child of MIXED or CHOICE");

                    case SCHEMA_CTYPE_VIRTUAL:
                    case SCHEMA_CTYPE_KEYSPACE:
                    case SCHEMA_CTYPE_KEYSPACE_END:
                        break;
                    }
                }
                break;

            case SCHEMA_CTYPE_VIRTUAL:
            case SCHEMA_CTYPE_KEYSPACE:
            case SCHEMA_CTYPE_KEYSPACE_END:
                mayskip = 1;
                break;
            }
            if (cp->type == SCHEMA_CTYPE_INTERLEAVE) {
                if (!mustMatch && minOne(cp->quants[ac])) mustMatch = 1;
            } else {
                if (!mayskip && !hm && minOne (cp->quants[ac])) break;
            }
            ac++;
            hm = 0;
        }
        if (cp->type == SCHEMA_CTYPE_NAME) {
            if (ac == cp->nc) {
                /* The currently open element can end here, no
                 * mandatory elements missing.
                 * The element end is always mandatory.*/
                Tcl_ListObjAppendElement (
                    interp, rObj, serializeElementEnd (interp)
                    );
            }
            rc = 0;
        } else if (cp->type == SCHEMA_CTYPE_INTERLEAVE) {
            if (mustMatch) rc = 0;
        } else {
            /* SCHEMA_CTYPE_PATTERN */
            if (ac < cp->nc) rc = 0;
        }
        break;
        
    case SCHEMA_CTYPE_ANY:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_KEYSPACE:
    case SCHEMA_CTYPE_KEYSPACE_END:
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
    }
    return rc;
}

static Tcl_Obj *
unifyMatchList (
    Tcl_Interp *interp,
    Tcl_HashTable *htable,
    Tcl_Obj *list
    )
{
    int len, i, hnew;
    Tcl_HashEntry *h;
    Tcl_Obj *rObj, *thisObj;
    Tcl_HashSearch search;
    
    rObj = Tcl_NewObj();
    Tcl_ListObjLength (interp, list, &len);
    if (len == 0) return rObj;
    if (len == 1) {
        Tcl_ListObjIndex (interp, list, 0, &thisObj);
        Tcl_ListObjAppendElement (interp, rObj, thisObj);
        return rObj;
    }
    Tcl_InitHashTable (htable, TCL_STRING_KEYS);
    for (i = 0; i < len; i++) {
        Tcl_ListObjIndex (interp, list, i, &thisObj);
        h = Tcl_CreateHashEntry (htable, Tcl_GetString (thisObj), &hnew);
        if (hnew) {
            Tcl_SetHashValue (h, thisObj);
        }
    }
    for (h = Tcl_FirstHashEntry (htable, &search);
         h != NULL;
         h = Tcl_NextHashEntry (&search)) {
        Tcl_ListObjAppendElement (interp, rObj, Tcl_GetHashValue (h));
    }
    Tcl_DeleteHashTable (htable);
    return rObj;
}

static void
getNextExpected (
    SchemaData *sdata,
    Tcl_Interp *interp,
    int         expectedFlags
    )
{
    int remainingLastMatch, count, rc;
    Tcl_Obj *rObj;
    Tcl_HashTable localHash;
    SchemaValidationStack *se;

    rObj = Tcl_NewObj();
    Tcl_InitHashTable (&localHash, TCL_ONE_WORD_KEYS);
    remainingLastMatch = 0;
    if (sdata->lastMatchse) {
        se = sdata->lastMatchse;
        while (se->down) {
            remainingLastMatch++;
            se = se->down;
        }
        while (se && getNextExpectedWorker (sdata, se, interp, &localHash, rObj,
                   expectedFlags)) {
            if (remainingLastMatch) {
                count = 1;
                se = sdata->lastMatchse;
                while (count < remainingLastMatch) {
                    se = se->down;
                    count++;
                }
                remainingLastMatch--;
            } else break;
        }
    }
    
    se = sdata->stack;
    while (se) {
        if (!se->hasMatched && se->pattern->type != SCHEMA_CTYPE_NAME) {
            se = se->down;
            continue;
        }
        rc = getNextExpectedWorker (sdata, se, interp, &localHash, rObj,
                                    expectedFlags);
        if (se->pattern->type == SCHEMA_CTYPE_NAME) break;
        se = se->down;
        if (!rc) break;
    }
    Tcl_DeleteHashTable (&localHash);
    Tcl_SetObjResult (interp, unifyMatchList (interp, &localHash, rObj));
    Tcl_DecrRefCount (rObj);
}

static int
schemaInstanceInfoCmd (
    SchemaData *sdata,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int methodIndex, expectedFlags;
    long line, column;
    Tcl_HashEntry *h;
    SchemaCP *cp;
    SchemaValidationStack *se;
    void *ns;
    Tcl_Obj *rObj;
    
    static const char *schemaInstanceInfoMethods[] = {
        "validationstate", "vstate", "definedElements", "stack", "toplevel",
        "expected", "definition", "validationaction", "vaction", "line",
        "column", "domNode", "nrForwardDefinitions", "typedefinition",
        "definedElementtypes", "patterndefinition", "definedPatterns", NULL
    };
    enum schemaInstanceInfoMethod {
        m_validationstate, m_vstate, m_definedElements, m_stack, m_toplevel,
        m_expected, m_definition, m_validationaction, m_vaction, m_line,
        m_column, m_domNode, m_nrForwardDefinitions, m_typedefinition,
        m_definedElementtypes, m_patterndefinition, m_definedPatterns
    };

    static const char *schemaInstanceInfoStackMethods[] = {
        "top", "inside", "associated", NULL
    };
    enum schemaInstanceInfoStackMethod {
        m_top, m_inside, m_associated
    };

    static const char *schemaInstanceInfoVactionMethods[] = {
        "name", "namespace", "text", NULL
    };
    enum schemaInstanceInfoVactionMethod {
        m_name, m_namespace, m_text
    };

    static const char *schemaInstanceInfoExpectedOptions[] = {
        "-ignorematched", "-onlymandatory", NULL
    };
    enum schemaInstanceInfoExpectedOption 
    {
        o_ignorematched, o_onlymandatory
    };
            
    if (objc < 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?arguments?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj (interp, objv[1], schemaInstanceInfoMethods,
                             "method", 0, &methodIndex)
        != TCL_OK) {
        return TCL_ERROR;
    }
    
    Tcl_ResetResult (interp);
    switch ((enum schemaInstanceInfoMethod) methodIndex) {
    case m_validationstate:
    case m_vstate:
        switch (sdata->validationState) {
        case VALIDATION_READY:
            SetResult ("READY");
            break;
        case VALIDATION_STARTED:
            SetResult ("VALIDATING");
            break;
        case VALIDATION_FINISHED:
            SetResult ("FINISHED");
            break;
        default:
            SetResult ("Internal error: Invalid validation state");
            return TCL_ERROR;
        }
        break;
        
    case m_definedElements:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 1, objv, "definedElements");
            return TCL_ERROR;
        }
        definedElements (&sdata->element, interp);
        break;

    case m_definedPatterns:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 1, objv, "definedPatterns");
            return TCL_ERROR;
        }
        definedElements (&sdata->pattern, interp);
        break;
        
    case m_definedElementtypes:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 1, objv, "definedElementtypes");
            return TCL_ERROR;
        }
        definedElementtypes (sdata, interp);
        break;

    case m_stack:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "top|inside");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObj (interp, objv[2],
                                 schemaInstanceInfoStackMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        if (!sdata->stack) {
            return TCL_OK;
        }
        se = sdata->stack;
        switch ((enum schemaInstanceInfoStackMethod) methodIndex) {
        case m_inside:
            rObj = Tcl_NewObj();
            while (se) {
                if (se->pattern->type == SCHEMA_CTYPE_NAME) {
                    Tcl_ListObjAppendElement (interp, rObj,
                        serializeElementName (interp, se->pattern));
                }
                se = se->down;
            }
            Tcl_SetObjResult (interp, rObj);
            return TCL_OK;
            
        case m_top:
            while (se->pattern->type != SCHEMA_CTYPE_NAME) {
                se = se->down;
            }
            rObj = serializeElementName (interp, se->pattern);
            Tcl_SetObjResult (interp, rObj);
            return TCL_OK;

        case m_associated:
            if (!se->pattern->associated) {
                return TCL_OK;
            }
            Tcl_SetObjResult (interp, se->pattern->associated);
            return TCL_OK;
        }
        
    case m_toplevel:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        if (!sdata->currentEvals) {
            SetResult ("not called while schema evaluation");
            return TCL_ERROR;
        }
        if (!sdata->defineToplevel && sdata->currentEvals > 1) {
            SetBooleanResult (0);
        } else {
            SetBooleanResult (1);
        }
        return TCL_OK;

    case m_expected:
        if (objc < 2 || objc > 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "?-ignorematched? ?-onlymandatory?");
            return TCL_ERROR;
        }
        if (sdata->validationState == VALIDATION_ERROR
            || sdata->validationState == VALIDATION_FINISHED) {
            return TCL_OK;
        }
        expectedFlags = 0;
        while (objc > 2) {
            if (Tcl_GetIndexFromObj (interp, objv[2],
                                     schemaInstanceInfoExpectedOptions,
                                     "option", 0, &methodIndex)
                != TCL_OK) {
                return TCL_ERROR;
            }
            switch ((enum schemaInstanceInfoExpectedOption) methodIndex) {
            case o_ignorematched:
                expectedFlags |= EXPECTED_IGNORE_MATCHED;
                break;
            case o_onlymandatory:
                expectedFlags |= EXPECTED_ONLY_MANDATORY;
                break;
            }
            objv++;
            objc--;
        }
        if (!sdata->stack) {
            if (sdata->start) {
                Tcl_AppendElement (interp, sdata->start);
                if (sdata->startNamespace) {
                    Tcl_AppendElement (interp, sdata->startNamespace);
                }
            } else {
                definedElements (&sdata->element, interp);
            }
        } else {
            getNextExpected (sdata, interp, expectedFlags);
        }
        break;
        
    case m_definition:
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 1, objv, "name ?namespace?");
            return TCL_ERROR;
        }
        h = Tcl_FindHashEntry (&sdata->element, Tcl_GetString (objv[2]));
        if (!h) {
            SetResult ("Unknown element definition");
            return TCL_ERROR;
        }
        cp = Tcl_GetHashValue (h);
        ns = NULL;
        if (objc == 4) {
            ns = getNamespacePtr (sdata, Tcl_GetString (objv[3]));
        }
        while (cp && cp->namespace != ns) {
            cp = cp->next;
        }
        if (!cp
            || cp->flags & LOCAL_DEFINED_ELEMENT
            || cp->flags & PLACEHOLDER_PATTERN_DEF) {
            SetResult ("Unknown element definition");
            return TCL_ERROR;
        }
        Tcl_AppendElement (interp, "defelement");
        Tcl_AppendElement (interp, cp->name);
        if (cp->namespace) {
            Tcl_AppendElement (interp, cp->namespace);
        }
        if (cp->defScript) {
            Tcl_AppendElement (interp, Tcl_GetString (cp->defScript));
        }
        break;

    case m_patterndefinition:
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 1, objv, "name ?namespace?");
            return TCL_ERROR;
        }
        h = Tcl_FindHashEntry (&sdata->pattern, Tcl_GetString (objv[2]));
        if (!h) {
            SetResult ("Unknown pattern definition");
            return TCL_ERROR;
        }
        cp = Tcl_GetHashValue (h);
        ns = NULL;
        if (objc == 4) {
            ns = getNamespacePtr (sdata, Tcl_GetString (objv[3]));
        }
        while (cp && cp->namespace != ns) {
            cp = cp->next;
        }
        if (!cp || cp->flags & PLACEHOLDER_PATTERN_DEF) {
            SetResult ("Unknown pattern definition");
            return TCL_ERROR;
        }
        Tcl_AppendElement (interp, "defpattern");
        Tcl_AppendElement (interp, cp->name);
        if (cp->namespace) {
            Tcl_AppendElement (interp, cp->namespace);
        }
        if (cp->defScript) {
            Tcl_AppendElement (interp, Tcl_GetString (cp->defScript));
        }
        break;
        
    case m_typedefinition:
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "name ?namespace?");
            return TCL_ERROR;
        }
        h = Tcl_FindHashEntry (&sdata->elementType, Tcl_GetString (objv[2]));
        if (!h) {
            SetResult ("Unknown elementtype definition");
            return TCL_ERROR;
        }
        cp = Tcl_GetHashValue (h);
        ns = NULL;
        if (objc == 4) {
            ns = getNamespacePtr (sdata, Tcl_GetString (objv[3]));
        }
        while (cp && cp->namespace != ns) {
            cp = cp->next;
        }
        if (!cp || cp->flags & PLACEHOLDER_PATTERN_DEF) {
            SetResult ("Unknown elementtype definition");
            return TCL_ERROR;
        }
        Tcl_AppendElement (interp, "defelementtype");
        Tcl_AppendElement (interp, cp->name);
        if (cp->namespace) {
            Tcl_AppendElement (interp, cp->namespace);
        }
        if (cp->defScript) {
            Tcl_AppendElement (interp, Tcl_GetString (cp->defScript));
        }
        break;

    case m_vaction:
    case m_validationaction:
        if (sdata->validationState != VALIDATION_STARTED
            || sdata->currentEvals == 0) {
            SetResult ("NONE");
            break;
        }
        if (objc == 2) {
            SetResult (ValidationAction2str[sdata->vaction]);
            break;
        }
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "?name|namespace|text?");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObj (interp, objv[2],
                                 schemaInstanceInfoVactionMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum schemaInstanceInfoVactionMethod) methodIndex) {
        case m_name:
            SetResult (sdata->vname);
            break;
        case m_namespace:
            SetResult (sdata->vns);
            break;
        case m_text:
            SetResult (sdata->vtext);
            break;
        }
        break;

    case m_line:
        if (!sdata->parser && !sdata->node) break;
        if (sdata->parser) {
            SetLongResult (XML_GetCurrentLineNumber (sdata->parser));
            break;
        }
        if (domGetLineColumn(sdata->node, &line, &column) < 0) break;
        SetLongResult (line);
        break;
        
    case m_column:
        if (!sdata->parser && !sdata->node) break;
        if (sdata->parser) {
            SetLongResult (XML_GetCurrentColumnNumber (sdata->parser));
            break;
        }
        if (domGetLineColumn(sdata->node, &line, &column) < 0) break;
        SetLongResult (column);
        break;

    case m_domNode:
        if (!sdata->node) break;
        /* We distinguish between calls from reportCmd and others
         * (from scripts called with the tcl cmd). */
        if (sdata->vaction) {
            /* This is the case: called from reportCmd. */
            return tcldom_setInterpAndReturnVar (interp, sdata->node, 0, NULL);
        } else {
            /* This is the case: called from a with tcl called script. */
            return tcldom_setInterpAndReturnVar (interp, sdata->insideNode, 0, NULL);
        }
        break;
        
    case m_nrForwardDefinitions:
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        SetIntResult(sdata->forwardPatternDefs);
        break;

    }
    return TCL_OK;
}

static void
attributeLookupPreparation (
    SchemaData *sdata,
    SchemaCP   *cp
    )
{
    Tcl_HashTable *t;
    unsigned int i;
    int hnew;
    Tcl_HashEntry *h;
    SchemaAttr *attr;
    
    if (cp->numAttr <= sdata->attributeHashThreshold) return;
    t = TMALLOC (Tcl_HashTable);
    Tcl_InitHashTable (t, TCL_STRING_KEYS);
    for (i = 0; i < cp->numAttr; i++) {
        h = Tcl_CreateHashEntry (t, cp->attrs[i]->name, &hnew);
        if (hnew) {
            Tcl_SetHashValue (h, cp->attrs[i]);
        } else {
            attr = (SchemaAttr *) Tcl_GetHashValue (h);
            cp->attrs[i]->next = attr->next;
            attr->next = cp->attrs[i];
        }
    }
    cp->typedata = (void *)t;
}

static void validateReportError (
    Tcl_Interp *interp,
    SchemaData *sdata,
    XML_Parser parser
    )
{
    Tcl_Obj *resultObj;
    char sl[50], sc[50];

    resultObj = Tcl_NewObj ();
    sprintf(sl, "%ld", XML_GetCurrentLineNumber(parser));
    sprintf(sc, "%ld", XML_GetCurrentColumnNumber(parser));
    if (sdata->validationState == VALIDATION_ERROR) {
        Tcl_AppendStringsToObj (resultObj, "error \"",
                                Tcl_GetStringResult (interp),
                                "\" at line ", sl, " character ", sc, NULL);
    } else {
        Tcl_AppendStringsToObj (resultObj, "error \"",
                                XML_ErrorString(XML_GetErrorCode(parser)),
                                "\" at line ", sl, " character ", sc, NULL);
    }
    Tcl_SetObjResult (interp, resultObj);
}
    
static int
externalEntityRefHandler (
    XML_Parser  parser,
    const char *openEntityNames,
    const char *base,
    const char *systemId,
    const char *publicId
)
{
    ValidateMethodData *vdata;
    Tcl_Obj *cmdPtr, *resultObj, *resultTypeObj, *extbaseObj, *xmlstringObj;
    Tcl_Obj *channelIdObj;
    int result, mode, done, byteIndex, i;
    int keepresult = 0;
    size_t len;
    int tclLen;
    XML_Parser extparser, oldparser = NULL;
    char buf[4096], *resultType, *extbase, *xmlstring, *channelId, s[50];
    Tcl_Channel chan = (Tcl_Channel) NULL;
    enum XML_Status status;
    const char *interpResult;
    
    vdata = (ValidateMethodData *) XML_GetUserData (parser);
    if (vdata->externalentitycommandObj == NULL) {
        Tcl_AppendResult (vdata->interp, "Can't read external entity \"",
                          systemId, "\": No -externalentitycommand given",
                          NULL);
        return 0;
    }

    cmdPtr = Tcl_NewStringObj(Tcl_GetString(vdata->externalentitycommandObj), -1);
    Tcl_IncrRefCount(cmdPtr);

    if (base) {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewStringObj(base, strlen(base)));
    } else {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewObj());
    }

    /* For a document with doctype declaration, the systemId is always
       != NULL. But if the document doesn't have a doctype declaration
       and the user uses -useForeignDTD 1, the externalEntityRefHandler
       will be called with a systemId (and publicId and openEntityNames)
       == NULL. */
    if (systemId) {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewStringObj(systemId, strlen(systemId)));
    } else {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewObj());
    }

    if (publicId) {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewStringObj(publicId, strlen(publicId)));
    } else {
        Tcl_ListObjAppendElement(vdata->interp, cmdPtr,
                                 Tcl_NewObj());
    }

 
    result = Tcl_EvalObjEx (vdata->interp, cmdPtr, 
                            TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);

    Tcl_DecrRefCount(cmdPtr);

    if (result != TCL_OK) {
        vdata->sdata->evalError = 1;
        return 0;
    }

    extparser = XML_ExternalEntityParserCreate (parser, openEntityNames, 0);

    resultObj = Tcl_GetObjResult (vdata->interp);
    Tcl_IncrRefCount (resultObj);

    result = Tcl_ListObjLength (vdata->interp, resultObj, &tclLen);
    if ((result != TCL_OK) || (tclLen != 3)) {
        goto wrongScriptResult;
    }
    result = Tcl_ListObjIndex (vdata->interp, resultObj, 0, &resultTypeObj);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    resultType = Tcl_GetString(resultTypeObj);

    if (strcmp (resultType, "string") == 0) {
        result = Tcl_ListObjIndex (vdata->interp, resultObj, 2, &xmlstringObj);
        xmlstring = Tcl_GetStringFromObj (xmlstringObj, (int*)&len);
    } else if (strcmp (resultType, "channel") == 0) {
        xmlstring = NULL;
        len = 0;
        result = Tcl_ListObjIndex (vdata->interp, resultObj, 2, &channelIdObj);
        channelId = Tcl_GetString(channelIdObj);
        chan = Tcl_GetChannel (vdata->interp, channelId, &mode);
        if (chan == (Tcl_Channel) NULL) {
            goto wrongScriptResult;
        }
        if ((mode & TCL_READABLE) == 0) {
            return 0;
        }
    } else if (strcmp (resultType, "filename") == 0) {
        /* result type "filename" not yet implemented */
        return 0;
    } else {
        goto wrongScriptResult;
    }

    result = Tcl_ListObjIndex (vdata->interp, resultObj, 1, &extbaseObj);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    extbase = Tcl_GetString(extbaseObj);

    /* TODO: what to do, if this document was already parsed before ? */

    if (!extparser) {
        Tcl_DecrRefCount (resultObj);
        Tcl_SetResult (vdata->interp,
                       "unable to create expat external entity parser",
                       NULL);
        return 0;
    }

    oldparser = vdata->parser;
    vdata->parser = extparser;
    XML_SetBase (extparser, extbase);

    Tcl_ResetResult (vdata->interp);
    result = 1;
    if (chan == NULL) {
        status = XML_Parse(extparser, xmlstring, strlen (xmlstring), 1);
        switch (status) {
        case XML_STATUS_ERROR:
            interpResult = Tcl_GetStringResult(vdata->interp);
            sprintf(s, "%ld", XML_GetCurrentLineNumber(extparser));
            if (interpResult[0] == '\0') {
                Tcl_ResetResult (vdata->interp);
                Tcl_AppendResult(vdata->interp, "error \"",
                                 XML_ErrorString(XML_GetErrorCode(extparser)),
                                 "\" in entity \"", systemId,
                                 "\" at line ", s, " character ", NULL);
                sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                Tcl_AppendResult(vdata->interp, s, NULL);
                byteIndex = XML_GetCurrentByteIndex(extparser);
                if (byteIndex != -1) {
                    Tcl_AppendResult(vdata->interp, "\n\"", NULL);
                    s[1] = '\0';
                    for (i=-20; i < 40; i++) {
                        if ((byteIndex+i)>=0) {
                            if (xmlstring[byteIndex+i]) {
                                s[0] = xmlstring[byteIndex+i];
                                Tcl_AppendResult(vdata->interp, s, NULL);
                                if (i==0) {
                                    Tcl_AppendResult(vdata->interp,
                                                     " <--Error-- ", NULL);
                                }
                            } else {
                                break;
                            }
                        }
                    }
                    Tcl_AppendResult(vdata->interp, "\"",NULL);
                }
            } else {
                Tcl_AppendResult(vdata->interp, ", referenced in entity \"",
                                 systemId, 
                                 "\" at line ", s, " character ", NULL);
                sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                Tcl_AppendResult(vdata->interp, s, NULL);
            }
            keepresult = 1;
            result = 0;
            break;
        case XML_STATUS_SUSPENDED:
            XML_StopParser (oldparser, 1);
            keepresult = 1;
            break;
        default:
            break;
        }
    } else {
        do {
            len = Tcl_Read (chan, buf, sizeof(buf));
            done = len < sizeof(buf);
            status = XML_Parse (extparser, buf, len, done);
            switch (status) {
            case XML_STATUS_ERROR:
                interpResult = Tcl_GetStringResult(vdata->interp);
                sprintf(s, "%ld", XML_GetCurrentLineNumber(extparser));
                if (interpResult[0] == '\0') {
                    Tcl_ResetResult (vdata->interp);
                    Tcl_AppendResult(vdata->interp, "error \"",
                                     XML_ErrorString(XML_GetErrorCode(extparser)),
                                     "\" in entity \"", systemId,
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                    Tcl_AppendResult(vdata->interp, s, NULL);
                } else {
                    Tcl_AppendResult(vdata->interp, ", referenced in entity \"",
                                     systemId, 
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                    Tcl_AppendResult(vdata->interp, s, NULL);
                }
                result = 0;
                keepresult = 1;
                done = 1;
                break;
            case XML_STATUS_SUSPENDED:
                XML_StopParser (oldparser, 1);
                keepresult = 1;
                done = 1;
                break;
            default:
                break;
            }
        } while (!done);
    }

    if (!keepresult) {
        Tcl_ResetResult (vdata->interp);
    }
    XML_ParserFree (extparser);
    vdata->parser = oldparser;
    Tcl_DecrRefCount (resultObj);
    return result;

 wrongScriptResult:
    Tcl_DecrRefCount (resultObj);
    Tcl_ResetResult (vdata->interp);
    XML_ParserFree (extparser);
    if (oldparser) {
        vdata->parser = oldparser;
    }
    vdata->sdata->evalError = 1;
    Tcl_AppendResult (vdata->interp, "The -externalentitycommand script "
                      "has to return a Tcl list with 3 elements.\n"
                      "Syntax: {string|channel|filename <baseurl> <data>}\n",
                      NULL);
    return 0;
}

static int validateSource (
    ValidationInput source,
    SchemaData *sdata,
    ValidateMethodData *vdata,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    XML_Parser parser;
    char sep = '\xFF';
    Tcl_DString cdata;
    Tcl_Obj *bufObj;
    char *xmlstr, *filename, *str, *baseurl = NULL;
    int result, len, fd, mode, done, tclLen, rc, value, useForeignDTD = 0;
    int forrest = 0;
    int paramEntityParsing = XML_PARAM_ENTITY_PARSING_ALWAYS;
    Tcl_DString translatedFilename;
    int optionIndex;
    Tcl_Channel channel;
    
    static const char *validateOptions[] = {
        "-baseurl", "-externalentitycommand", "-paramentityparsing",
        "-useForeignDTD", "-forrest", NULL
    };
    enum validateOption {
        o_baseurl, o_externalentitycommand, o_paramentityparsing,
        o_useForeignDTD, o_forrest
    };

    static const char *paramEntityParsingValues[] = {
        "always",
        "never",
        "notstandalone",
        (char *) NULL
    };
    enum paramEntityParsingValue {
        EXPAT_PARAMENTITYPARSINGALWAYS,
        EXPAT_PARAMENTITYPARSINGNEVER,
        EXPAT_PARAMENTITYPARSINGNOTSTANDALONE
    };

    if (objc < 3) {
        Tcl_WrongNumArgs (interp, 2, objv, "?-baseurl <baseurl>? "
                          "?-externalentitycommand <cmd>? "
                          "?-paramentityparsing (always|never|standalone? "
                          "<xml> ?resultVarName?");
        return TCL_ERROR;
    }
    if (sdata->validationState != VALIDATION_READY) {
        SetResult ("The schema command is busy");
        return TCL_ERROR;
    }
    objc -= 2;
    objv += 2;

    memset (vdata, 0, sizeof (ValidateMethodData));
    vdata->externalentitycommandObj =
        Tcl_NewStringObj ("::tdom::extRefHandler", 21);
    Tcl_IncrRefCount (vdata->externalentitycommandObj);
    while (objc > 2) {
        if (Tcl_GetIndexFromObj (interp, objv[0], validateOptions,
                                 "option", 0, &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum validateOption) optionIndex) {
        case o_baseurl:
            baseurl = Tcl_GetString (objv[1]);
            break;
        case o_externalentitycommand:
            if (vdata->externalentitycommandObj) 
                Tcl_DecrRefCount (vdata->externalentitycommandObj);
            Tcl_GetStringFromObj (objv[1], &len);
            if (len) {
                vdata->externalentitycommandObj = objv[1];
                Tcl_IncrRefCount (objv[1]);
            } else {
                vdata->externalentitycommandObj = NULL;
            }
            break;
        case o_paramentityparsing:
            if (Tcl_GetIndexFromObj(interp, objv[1], 
                                    paramEntityParsingValues, "value", 0, 
                                    &value) != TCL_OK) {
                Tcl_DecrRefCount (vdata->externalentitycommandObj);
                return TCL_ERROR;
            }
            switch ((enum paramEntityParsingValue) value) {
            case EXPAT_PARAMENTITYPARSINGALWAYS:
                paramEntityParsing = XML_PARAM_ENTITY_PARSING_ALWAYS;
                break;
            case EXPAT_PARAMENTITYPARSINGNEVER:
                paramEntityParsing = XML_PARAM_ENTITY_PARSING_NEVER;
                break;
            case EXPAT_PARAMENTITYPARSINGNOTSTANDALONE:
                paramEntityParsing = XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE;
                break;
            }
        case o_useForeignDTD:
            if (Tcl_GetBooleanFromObj(interp, objv[1], &useForeignDTD)
                != TCL_OK) {
                Tcl_DecrRefCount (vdata->externalentitycommandObj);
                return TCL_ERROR;
            }
        case o_forrest:
            if (Tcl_GetBooleanFromObj(interp, objv[1], &forrest)
                != TCL_OK) {
                Tcl_DecrRefCount (vdata->externalentitycommandObj);
                return TCL_ERROR;
            }
            objc -= 1;
            objv += 1;
            continue;
            
        }
        objc -= 2;
        objv += 2;
    }

    parser = XML_ParserCreate_MM (NULL, MEM_SUITE, &sep);
    vdata->interp = interp;
    vdata->sdata = sdata;
    vdata->parser = parser;
    sdata->parser = parser;
    Tcl_DStringInit (&cdata);
    vdata->cdata = &cdata;
    vdata->onlyWhiteSpace = 1;
    vdata->uri = (char *) MALLOC (URI_BUFFER_LEN_INIT);
    vdata->maxUriLen = URI_BUFFER_LEN_INIT;
    XML_SetUserData (parser, vdata);
    XML_SetBase (parser, baseurl);
    XML_SetElementHandler (parser, startElement, endElement);
    XML_SetCharacterDataHandler (parser, characterDataHandler);
    if (vdata->externalentitycommandObj) {
        XML_SetExternalEntityRefHandler (parser, externalEntityRefHandler);
    }
    XML_UseForeignDTD (parser, (unsigned char) useForeignDTD);
    XML_SetParamEntityParsing (parser, paramEntityParsing);

    switch (source) {
    case VALIDATE_STRING:
        xmlstr = Tcl_GetStringFromObj (objv[0], &len);
        if (XML_Parse (parser, xmlstr, len, 1) != XML_STATUS_OK
            || sdata->validationState == VALIDATION_ERROR) {
            validateReportError (interp, sdata, parser);
            result = TCL_ERROR;
        } else {
            result = TCL_OK;
        }
        break;
        
    case VALIDATE_FILENAME:
        filename = Tcl_TranslateFileName (interp, Tcl_GetString (objv[0]),
                                          &translatedFilename);
        if (filename == NULL) {
            result = TCL_ERROR;
            goto cleanup;
        }
        fd = open(filename, O_BINARY|O_RDONLY);
        if (fd < 0) {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "error opening file \"",
                              filename, "\"", (char *) NULL);
            result = TCL_ERROR;
            goto cleanup;
        }
        for (;;) {
            int nread;
            char *fbuf = XML_GetBuffer (parser, TDOM_EXPAT_READ_SIZE);
            if (!fbuf) {
                close (fd);
                Tcl_ResetResult (interp);
                Tcl_SetResult (interp, "Out of memory\n", NULL);
                result = TCL_ERROR;
                goto cleanup;
            }
            nread = read(fd, fbuf, TDOM_EXPAT_READ_SIZE);
            if (nread < 0) {
                close (fd);
                Tcl_ResetResult (interp);
                Tcl_AppendResult (interp, "error reading from file \"",
                                  filename, "\"", (char *) NULL);
                result = TCL_ERROR;
                goto cleanup;
            }
            result = XML_ParseBuffer (parser, nread, nread == 0);
            if (result != XML_STATUS_OK || !nread
                || sdata->validationState == VALIDATION_ERROR) {
                close (fd);
                break;
            }
        }
        if (result != XML_STATUS_OK
            || sdata->validationState == VALIDATION_ERROR) {
            validateReportError (interp, sdata, parser);
            result = TCL_ERROR;
        } else {
            result = TCL_OK;
        }
    cleanup:
        Tcl_DStringFree (&translatedFilename);
        break;
        
    case VALIDATE_CHANNEL:
        channel = Tcl_GetChannel(interp, Tcl_GetString (objv[0]), &mode);
        if (channel == NULL) {
            SetResult ("The channel argument isn't a tcl channel");
            result = TCL_ERROR;
            break;
        }
        bufObj = Tcl_NewObj();
        Tcl_SetObjLength (bufObj, 6144);
        result = TCL_OK;
        do {
            len = Tcl_ReadChars (channel, bufObj, 1024, 0);
            done = (len < 1024);
            str = Tcl_GetStringFromObj(bufObj, &tclLen);
            rc = XML_Parse (parser, str, tclLen, done);
            if (rc != XML_STATUS_OK 
                || sdata->validationState == VALIDATION_ERROR) {
                validateReportError (interp, sdata, parser);
                result = TCL_ERROR;
                break;
            }
        } while (!done);
        Tcl_DecrRefCount (bufObj);
        break;
    }
    
    XML_ParserFree (parser);
    sdata->parser = NULL;
    FREE (vdata->uri);
    Tcl_DStringFree (&cdata);
    Tcl_DecrRefCount (vdata->externalentitycommandObj);
    
    if (sdata->evalError) {
        result = TCL_ERROR;
    } else {
        if (result == TCL_OK) {
            SetBooleanResult (1);
        } else {
            if (objc == 2) {
                Tcl_SetVar (interp, Tcl_GetString (objv[1]),
                            Tcl_GetStringResult (interp), 0);
            }
            result = TCL_OK;
            SetBooleanResult (0);
        }
    }
    schemaReset (sdata);
    return result;
}

/* This implements the script interface to the created schema commands.

   Since validation may call out to Tcl scripts those scripts may
   delete the schema command (which just validates input by calling
   out to a Tcl script). This is handled by Tcl evaluation level
   counting and postponing the schema data deletion until back on top.

   After any code by this function that may have called out to a Tcl
   script it is important not to return locally but to signal the
   return value with the result variable and ensure to reach the code
   at the end of tDOM_schemaInstanceCmd.
 */
int
tDOM_schemaInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, keywordIndex, hnew, patternIndex;
    int            result = TCL_OK, forwardDef = 0, j;
    int            savedDefineToplevel, type, len, n;
    unsigned int   i = 0, savedNumPatternList, nrTypeInstances, typeInstancesLen;
    SchemaData    *savedsdata = NULL, *sdata = (SchemaData *) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *h, *h1;
    SchemaCP      *pattern, *current = NULL;
    void          *namespacePtr, *savedNamespacePtr;
    char          *errMsg;
    domDocument   *doc;
    domNode       *node;
    Tcl_Obj       *attData;
    SchemaCP     **typeInstances;
    ValidateMethodData vdata;

    static const char *schemaInstanceMethods[] = {
        "defelement", "defpattern", "start",    "event",        "delete",
        "reset",      "define",     "validate", "domvalidate",  "deftexttype",
        "info",       "reportcmd",  "prefixns", "validatefile",
        "validatechannel",          "defelementtype",           "set",
        NULL
    };
    enum schemaInstanceMethod {
        m_defelement, m_defpattern, m_start,    m_event,        m_delete,
        m_reset,      m_define,     m_validate, m_domvalidate,  m_deftexttype,
        m_info,       m_reportcmd,  m_prefixns, m_validatefile,
        m_validatechannel,          m_defelementtype,           m_set
    };

    static const char *eventKeywords[] = {
        "start", "end", "text", NULL
    };
    enum eventKeyword
    {
        k_elementstart, k_elementend, k_text
    };

    static const char *setKeywords[] = {
        "choiceHashThreshold", "attributeHashThreshold", NULL
    };
    enum setKeyword
    {
        s_choiceHashThreshold, s_attributeHashThreshold
    };
        
    if (sdata == NULL) {
        /* Inline defined defelement, defelementtype, defpattern,
         * deftexttype, start or prefixns */
        sdata = GETASI;
        CHECK_SI;
        if (!sdata->defineToplevel && sdata->currentEvals > 1) {
            SetResult ("Command not allowed in nested schema define script");
            return TCL_ERROR;
        }
        i = 1;
    }
    if (objc + i < 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?arguments?");
        return TCL_ERROR;
    }


    if (Tcl_GetIndexFromObj (interp, objv[1-i], schemaInstanceMethods,
                             "method", 0, &methodIndex)
        != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_ResetResult (interp);
    switch ((enum schemaInstanceMethod) methodIndex) {
    case m_defelement:
    case m_defpattern:
        CHECK_RECURSIVE_CALL
        if (objc != 4-i && objc != 5-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<name>"
                 " ?<namespace>? pattern");
            return TCL_ERROR;
        }
        if ((enum schemaInstanceMethod) methodIndex == m_defelement) {
            hashTable = &sdata->element;
            type = SCHEMA_CTYPE_NAME;
        } else {
            hashTable = &sdata->pattern;
            type = SCHEMA_CTYPE_PATTERN;
        }
        savedNumPatternList = sdata->numPatternList;
        namespacePtr = NULL;
        patternIndex = 3-i;
        if (objc == 5-i) {
            patternIndex = 4-i;
            namespacePtr = getNamespacePtr (sdata, Tcl_GetString (objv[3-i]));
        }
        h = Tcl_CreateHashEntry (hashTable, Tcl_GetString (objv[2-i]), &hnew);
        pattern = NULL;
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (pattern) {
                if (pattern->namespace == namespacePtr) {
                    if (pattern->flags & FORWARD_PATTERN_DEF
                        || pattern->flags & PLACEHOLDER_PATTERN_DEF) {
                        forwardDef = 1;
                        break;
                    }
                    if ((enum schemaInstanceMethod) methodIndex
                        == m_defelement) {
                        SetResult ("Element already defined "
                                   "in this namespace");
                    } else {
                        SetResult ("Pattern already defined "
                                   "in this namespace");
                    }
                    return TCL_ERROR;
                }
                pattern = pattern->next;
            }
        }
        if (pattern == NULL) {
            pattern = initSchemaCP (type, namespacePtr,
                                       Tcl_GetHashKey (hashTable, h));
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (h);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (h, pattern);
        }

        SETASI(sdata);
        savedDefineToplevel = sdata->defineToplevel;
        savedNamespacePtr = sdata->currentNamespace;
        sdata->defineToplevel = 0;
        sdata->currentNamespace = namespacePtr;
        sdata->cp = pattern;
        sdata->numAttr = 0;
        sdata->numReqAttr = 0;
        sdata->currentAttrs = NULL;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        sdata->evalStub[3] = objv[patternIndex];
        sdata->currentEvals++;
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub, TCL_EVAL_GLOBAL);
        sdata->currentEvals--;
        sdata->currentNamespace = NULL;
        pattern->attrs = sdata->currentAttrs;
        pattern->numAttr = sdata->numAttr;
        pattern->numReqAttr = sdata->numReqAttr;
        if (result == TCL_OK) {
            if (pattern->numAttr) {
                attributeLookupPreparation (sdata, pattern);
            }
            if (forwardDef) {
                if (pattern->flags & FORWARD_PATTERN_DEF) {
                    sdata->forwardPatternDefs--;
                    pattern->flags &= ~FORWARD_PATTERN_DEF;
                }
                pattern->flags &= ~PLACEHOLDER_PATTERN_DEF;
            }
            pattern->defScript = objv[patternIndex];
            Tcl_IncrRefCount (pattern->defScript);
        } else {
            if (forwardDef) {
                pattern->nc = 0;
            }
            cleanupLastPattern (sdata, savedNumPatternList);
        }
        sdata->defineToplevel = savedDefineToplevel;
        sdata->currentNamespace = savedNamespacePtr;
        if (!savedDefineToplevel) {
            SETASI(savedsdata);
        }
        break;

    case m_define:
        CHECK_EVAL
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<definition commands>");
            return TCL_ERROR;
        }
        if (clientData) {
            savedsdata = GETASI;
            if (savedsdata == sdata) {
                SetResult ("Recursive call of schema command is not allowed");
                return TCL_ERROR;
            }
        }
        SETASI(sdata);
        savedNumPatternList = sdata->numPatternList;
        sdata->currentNamespace = 0;
        sdata->cp = NULL;
        sdata->contentSize = 0;
        sdata->defineToplevel = 1;
        sdata->evalStub[3] = objv[2];
        sdata->currentEvals++;
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub, TCL_EVAL_GLOBAL);
        sdata->currentEvals--;
        if (result != TCL_OK) {
            cleanupLastPattern (sdata, savedNumPatternList);
        }
        sdata->defineToplevel = 0;
        SETASI(savedsdata);
        break;

    case m_deftexttype:
        CHECK_RECURSIVE_CALL
        if (objc !=  4-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<name>"
                              " <constraints script>");
            return TCL_ERROR;
        }
        h = Tcl_CreateHashEntry (&sdata->textDef, Tcl_GetString (objv[2-i]),
                                 &hnew);
        if (!hnew) {
            pattern = Tcl_GetHashValue (h);
            if (pattern->flags & FORWARD_PATTERN_DEF) {
                forwardDef = 1;
            } else {
                SetResult ("There is already a text type definition with "
                           "this name");
                return TCL_ERROR;
            }
        } else {
            pattern = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
            pattern->type = SCHEMA_CTYPE_TEXT;
            REMEMBER_PATTERN (pattern);
        }
        SETASI(sdata);
        savedDefineToplevel = sdata->defineToplevel;
        result = evalConstraints (interp, sdata, pattern, objv[3-i]);
        sdata->defineToplevel = savedDefineToplevel;
        if (!savedDefineToplevel) {
            SETASI(savedsdata);
        }
        if (result == TCL_OK) {
            if (forwardDef) {
                pattern->flags &= ~FORWARD_PATTERN_DEF;
                sdata->forwardPatternDefs--;
            } else {
                Tcl_SetHashValue (h, pattern);
            }
        } else {
            if (!forwardDef) {
                Tcl_DeleteHashEntry (h);
            }
        }
        break;
        
    case m_start:
        CHECK_RECURSIVE_CALL
        if (objc < 3-i || objc > 4-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<documentElement>"
                              " ?<namespace>?");
            return TCL_ERROR;
        }
        if (sdata->start) {
            FREE (sdata->start);
        }
        if (objc == 3-i && strcmp (Tcl_GetString (objv[2-i]), "") == 0) {
            sdata->startNamespace = NULL;
            sdata->start = NULL;
            break;
        }
        sdata->start = tdomstrdup (Tcl_GetString (objv[2-i]));
        if (objc == 4-i) {
            sdata->startNamespace =
                getNamespacePtr (sdata, Tcl_GetString (objv[3-i]));
        }
        break;

    case m_event:
        CHECK_EVAL
        if (objc < 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<eventType>"
                              " ?<type specific data>?");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObj (interp, objv[2], eventKeywords,
                                 "keyword", 0, &keywordIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum eventKeyword) keywordIndex) {
        case k_elementstart:
            if (objc < 4 || objc > 6) {
                Tcl_WrongNumArgs (interp, 3, objv, "<elementname>"
                    "?<attInfo>? ?<namespace>?");
                return TCL_ERROR;
            }
            namespacePtr = NULL;
            len = 0;
            attData = NULL;
            if (objc == 6) {
                namespacePtr = getNamespacePtr (sdata,
                                                Tcl_GetString (objv[5]));
            }
            if (objc >= 5) {
                if (Tcl_ListObjLength (interp, objv[4], &len) != TCL_OK) {
                    if (objc == 6) {
                        SetResult ("Invalid attribute information");
                        return TCL_ERROR;
                    } else {
                        namespacePtr =
                            getNamespacePtr (sdata, Tcl_GetString (objv[4]));
                        len = 0;
                    }
                } else {
                    if (len == 1) {
                        namespacePtr =
                            getNamespacePtr (sdata, Tcl_GetString (objv[4]));
                        len = 0;
                    } else if (len % 2 != 0) {
                        SetResult ("Invalid attribute information");
                        return TCL_ERROR;
                    } else {
                        attData = objv[4];
                    }
                }
            }
            result = tDOM_probeElement (interp, sdata, Tcl_GetString (objv[3]),
                                   namespacePtr);
            /* In case of UNKNOWN_ROOT_ELEMENT and reportCmd is set
             * sdata->stack is NULL. */
            if (!sdata->stack) break;
            if (sdata->skipDeep == 0 && result == TCL_OK) {
                result = probeEventAttribute (interp, sdata, attData, len);
            }
            break;
            
        case k_elementend:
            if (objc != 3) {
                Tcl_WrongNumArgs (interp, 3, objv, "No arguments expected.");
                return TCL_ERROR;
            }
            result = tDOM_probeElementEnd (interp, sdata);
            break;

        case k_text:
            if (objc != 4) {
                Tcl_WrongNumArgs (interp, 3, objv, "<text>");
                return TCL_ERROR;
            }
            result = tDOM_probeText (interp, sdata, Tcl_GetString (objv[3]), NULL);
            break;
        }
        break;

    case m_delete:
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        Tcl_DeleteCommand(interp, Tcl_GetString(objv[0]));
        /* We return immediately here to avoid clashes with postponed
           sdata cleanup at the end of the function. */
        return TCL_OK;

    case m_reset:
        CHECK_EVAL
        schemaReset (sdata);
        break;

    case m_validate:
        result = validateSource (VALIDATE_STRING, sdata, &vdata, interp,
                                 objc, objv);
        break;

    case m_validatefile:
        result = validateSource (VALIDATE_FILENAME, sdata, &vdata, interp,
                                 objc, objv);
        break;

    case m_validatechannel:
        result = validateSource (VALIDATE_CHANNEL, sdata, &vdata, interp,
                                 objc, objv);
        break;
        
    case m_domvalidate:
        CHECK_EVAL
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<xml> ?resultVarName?");
            return TCL_ERROR;
        }
        doc = tcldom_getDocumentFromName (interp, Tcl_GetString (objv[2]),
                                          &errMsg);
        if (doc) {
            node = doc->documentElement;
        } else {
            node = tcldom_getNodeFromObj (interp, objv[2]);
            if (!node) {
                SetResult ("The second argument must be either a "
                           "document or a element node");
                return TCL_ERROR;
            }
        }
        if (validateDOM (interp, sdata, node) == TCL_OK) {
            SetBooleanResult (1);
            if (objc == 4) {
                Tcl_SetVar (interp, Tcl_GetString (objv[3]), "", 0);
            }
        } else {
            if (objc == 4) {
                Tcl_SetVar (interp, Tcl_GetString (objv[3]),
                            Tcl_GetStringResult (interp), 0);
            }
            SetBooleanResult (0);
        }
        schemaReset (sdata);
        break;

    case m_info:
        objv++;
        objc--;
        result = schemaInstanceInfoCmd (sdata, interp, objc, objv);
        break;

    case m_reportcmd:
        if (objc == 2) {
            if (sdata->reportCmd) {
                Tcl_SetObjResult (interp, sdata->reportCmd);
            } else {
                Tcl_SetObjResult (interp, Tcl_NewObj());
            }
            break;
        }
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<tcl-cmd>");
            return TCL_ERROR;
        }
        if (sdata->reportCmd) {
            Tcl_DecrRefCount (sdata->reportCmd);
        }
        if (strlen (Tcl_GetString (objv[2])) == 0) {
            sdata->reportCmd = NULL;
        } else {
            sdata->reportCmd = objv[2];
            Tcl_IncrRefCount (sdata->reportCmd);
        }
        break;

    case m_prefixns:
        CHECK_RECURSIVE_CALL
        if (objc != 2-i && objc != 3-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "?prefixUriList?");
            return TCL_ERROR;
        }
        if (!i) {objc--; objv++;}
        result = tcldom_prefixNSlist (&sdata->prefixns, interp, objc, objv,
                                      "prefixns");
        if (sdata->prefix.numEntries) {
            Tcl_DeleteHashTable (&sdata->prefix);
            Tcl_InitHashTable (&sdata->prefix, TCL_STRING_KEYS);
        }
        if (result == TCL_OK && sdata->prefixns) {
            j = 0;
            while (sdata->prefixns[j]) {
                h1 = Tcl_CreateHashEntry (&sdata->prefix,
                                          sdata->prefixns[j], &hnew);
                /* This means: First prefix mapping wins */
                if (hnew) {
                    h = Tcl_CreateHashEntry (&sdata->namespace,
                                             sdata->prefixns[j+1], &hnew);
                    Tcl_SetHashValue (h1, Tcl_GetHashKey (&sdata->namespace,
                                                          h));
                }
                j += 2;
            }
        }
        break;

    case m_defelementtype:
        CHECK_RECURSIVE_CALL
        if (objc != 4-i && objc != 5-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<type_name> "
                 " ?<namespace>? pattern");
            return TCL_ERROR;
        }
        savedNumPatternList = sdata->numPatternList;
        namespacePtr = NULL;
        patternIndex = 3-i;
        if (objc == 5-i) {
            namespacePtr = getNamespacePtr (sdata, Tcl_GetString (objv[3-i]));
            patternIndex = 4-i;
        }
        h = Tcl_CreateHashEntry (&sdata->elementType , Tcl_GetString (objv[2-i]),
                                 &hnew);
        pattern = NULL;
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (pattern) {
                if (pattern->namespace == namespacePtr) {
                    if (pattern->flags & FORWARD_PATTERN_DEF) {
                        forwardDef = 1;
                        break;
                    }
                    SetResult ("Element type already defined in this "
                               "namespace");
                    return TCL_ERROR;
                }
                pattern = pattern->next;
            }
        }
        if (pattern == NULL) {
            pattern = initSchemaCP (SCHEMA_CTYPE_NAME, namespacePtr,
                                    Tcl_GetHashKey (&sdata->elementType, h));
            pattern->flags |= ELEMENTTYPE_DEF;
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (h);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (h, pattern);
        }
        if (forwardDef) {
            /* The type was already forward defined. Save the witht
             the forward defined pattern stored instance elements to
             be able to set the actual content of the instance pattern
             after the type content is eventually definded below. */
            typeInstances = pattern->content;
            nrTypeInstances = pattern->nc;
            typeInstancesLen = pattern->numAttr;
            pattern->content = (SchemaCP**) MALLOC (
                sizeof(SchemaCP*) * CONTENT_ARRAY_SIZE_INIT
                );
            pattern->numAttr = 0;
            pattern->nc = 0;
        }
        SETASI(sdata);
        savedDefineToplevel = sdata->defineToplevel;
        savedNamespacePtr = sdata->currentNamespace;
        sdata->defineToplevel = 0;
        sdata->currentNamespace = namespacePtr;
        sdata->cp = pattern;
        sdata->numAttr = 0;
        sdata->numReqAttr = 0;
        sdata->currentAttrs = NULL;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        sdata->evalStub[3] = objv[patternIndex];
        sdata->currentEvals++;
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub, TCL_EVAL_GLOBAL);
        sdata->currentEvals--;
        sdata->currentNamespace = NULL;
        pattern->attrs = sdata->currentAttrs;
        pattern->numAttr = sdata->numAttr;
        pattern->numReqAttr = sdata->numReqAttr;
        if (result == TCL_OK) {
            if (pattern->numAttr) {
                attributeLookupPreparation (sdata, pattern);
            }
            if (forwardDef) {
                sdata->forwardPatternDefs--;
                pattern->flags &= ~FORWARD_PATTERN_DEF;
                for (i = 0; i < nrTypeInstances; i++) {
                    typeInstances[i]->content = pattern->content;
                    typeInstances[i]->quants = pattern->quants;
                    typeInstances[i]->nc = pattern->nc;
                    typeInstances[i]->typedata = pattern->typedata;
                    typeInstances[i]->attrs = pattern->attrs;
                    typeInstances[i]->numAttr = pattern->numAttr;
                    typeInstances[i]->numReqAttr = pattern->numReqAttr;
                    typeInstances[i]->domKeys = pattern->domKeys;
                    typeInstances[i]->keySpace = pattern->keySpace;
                    /* TODO: decide what to do with associated */
                }
                FREE (typeInstances);
            }
            pattern->defScript = objv[patternIndex];
            Tcl_IncrRefCount (pattern->defScript);
        } else {
            if (forwardDef) {
                FREE (pattern->content);
                pattern->content = typeInstances;
                pattern->nc = nrTypeInstances;
                pattern->numAttr = typeInstancesLen;
            }
            cleanupLastPattern (sdata, savedNumPatternList);
        }
        sdata->defineToplevel = savedDefineToplevel;
        sdata->currentNamespace = savedNamespacePtr;
        if (!savedDefineToplevel) {
            SETASI(savedsdata);
        }
        break;
            
    case m_set:
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "setting ?value?");
            return TCL_ERROR;
        }        
        if (Tcl_GetIndexFromObj (interp, objv[2], setKeywords,
                                 "setting", 0, &keywordIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum setKeyword) keywordIndex) {
        case s_choiceHashThreshold:
            if (objc == 4) {
                if (Tcl_GetIntFromObj (interp, objv[3], &n) != TCL_OK) {
                    SetResult ("Invalid threshold value");
                    return TCL_ERROR;
                }
                if (n < 0) {
                    SetResult ("Invalid threshold value");
                    return TCL_ERROR;
                }
                sdata->choiceHashThreshold = n;
            }
            SetIntResult (sdata->choiceHashThreshold);
        case s_attributeHashThreshold:
            if (objc == 4) {
                if (Tcl_GetIntFromObj (interp, objv[3], &n) != TCL_OK) {
                    SetResult ("Invalid threshold value");
                    return TCL_ERROR;
                }
                if (n < 0) {
                    SetResult ("Invalid threshold value");
                    return TCL_ERROR;
                }
                sdata->attributeHashThreshold = n;
            }
            SetIntResult (sdata->attributeHashThreshold);
        }
        break;
        
    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        result = TCL_ERROR;
        break;

    }
    if (sdata->cleanupAfterUse && sdata->currentEvals == 0
        && !(sdata->inuse > 0)) {
        schemaInstanceDelete (sdata);
    }
    return result;
}


/*
 *----------------------------------------------------------------------------
 *
 * tDOM_SchemaObjCmd --
 *
 *	This procedure is invoked to process the "schema" command.
 *      See the user documentation for what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------------
 */

int
tDOM_SchemaObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, ind, result = TCL_OK;
    SchemaData  *sdata;

    static const char *schemaMethods[] = {
        "create", NULL
    };
    enum schemaMethod {
        m_create
    };

    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?argument?");
        return TCL_ERROR;
    }

    if (objc == 2) {
        methodIndex = m_create;
        ind = 1;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], schemaMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        ind = 2;
    }

    Tcl_ResetResult (interp);
    switch ((enum schemaMethod) methodIndex) {
    case m_create:
        sdata = initSchemaData (objv[ind]);
        Tcl_CreateObjCommand (interp, Tcl_GetString(objv[ind]),
                              tDOM_schemaInstanceCmd,
                              (ClientData) sdata,
                              schemaInstanceDelete);
        Tcl_SetObjResult (interp, objv[ind]);
        break;

    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        result = TCL_ERROR;
        break;

    }
    return result;
}

static SchemaQuant
getQuant (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *quantObj,
    int *n,
    int *m
    )
{
    char *quantStr;
    int len;
    Tcl_Obj *thisObj;

    *n = 0;
    *m = 0;
    if (!quantObj) {
        return SCHEMA_CQUANT_ONE;
    }
    quantStr = Tcl_GetStringFromObj (quantObj, &len);
    if (len == 1) {
        switch (quantStr[0]) {
        case '!':
            return SCHEMA_CQUANT_ONE;
        case '*':
            return SCHEMA_CQUANT_REP;
        case '?':
            return SCHEMA_CQUANT_OPT;
        case '+':
            return SCHEMA_CQUANT_PLUS;
        }
    }
    if (Tcl_ListObjLength (interp, quantObj, &len) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return SCHEMA_CQUANT_ERROR;
    }
    if (len != 1 && len != 2) {
        SetResult ("Invalid quant specifier");
        return SCHEMA_CQUANT_ERROR;
    }
    if (len == 1) {
        if (Tcl_GetIntFromObj (interp, quantObj, n) != TCL_OK) {
            SetResult ("Invalid quant specifier");
            return SCHEMA_CQUANT_ERROR;
        }
        if (*n < 1) {
            SetResult ("Invalid quant specifier");
            return SCHEMA_CQUANT_ERROR;
        }
        if (*n == 1) {
            return SCHEMA_CQUANT_ONE;
            *n = 0;
        }
        return SCHEMA_CQUANT_NM;
    }
    /* The "list-ness" of the quantObj is already checked by the
     * Tcl_ListObjLength() call above, no need to check result. */
    Tcl_ListObjIndex (interp, quantObj, 0, &thisObj);
    if (Tcl_GetIntFromObj (interp, thisObj, n) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return SCHEMA_CQUANT_ERROR;
    }
    if (*n < 0) {
        SetResult ("Invalid quant specifier");
        return SCHEMA_CQUANT_ERROR;
    }
    Tcl_ListObjIndex (interp, quantObj, 1, &thisObj);
    if (Tcl_GetIntFromObj (interp, thisObj, m) == TCL_OK) {
        if (*n > *m) {
            SetResult ("Invalid quant specifier");
            return SCHEMA_CQUANT_ERROR;
        }
        if (*n == 0 && *m == 1) {
            return SCHEMA_CQUANT_OPT;
        }
        if (*n == 1 && *m == 1) {
            return SCHEMA_CQUANT_ONE;
        }
        return SCHEMA_CQUANT_NM;
    } else {
        quantStr = Tcl_GetStringFromObj (thisObj, &len);
        if (len == 1 && quantStr[0] == '*') {
            if (*n == 0) {
                return SCHEMA_CQUANT_REP;
            }
            *m = -1;
            return SCHEMA_CQUANT_NM;
        } else {
            SetResult ("Invalid quant specifier");
            return SCHEMA_CQUANT_ERROR;
        }
    }
}

/* Implements the schema definition command "any" */
static int
AnyPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *pattern;
    SchemaQuant quant;
    char *ns = NULL, *ns1;
    int n, m, nrns, i, hnew, revert, optionIndex;
    Tcl_Obj *nsObj;
    Tcl_HashTable *t = NULL;

    static const char *anyOptions[] = {
        "-not", "--", NULL
    };
    enum anyOption {
        o_not, o_Last
    };
    
    CHECK_SI
    CHECK_TOPLEVEL
    revert = 0;
    while (objc > 1) {
        if (Tcl_GetIndexFromObj(interp, objv[1], anyOptions, "option", 0,
                                &optionIndex) != TCL_OK) {
            break;
        }
        switch ((enum anyOption) optionIndex) {
        case o_not:
            revert = 1;
            objv++;  objc--; continue;
        case o_Last:
            objv++;  objc--; break;
        }
        if ((enum anyOption) optionIndex == o_Last) break;
    }
    checkNrArgs (1,3,"(options? ?namespace list? ?quant?");
    
    quant = SCHEMA_CQUANT_ONE;
    if (objc == 1) {
        n = 0; m = 0;
        goto createpattern;
    } else if (objc == 2) {    
        quant = getQuant (interp, sdata, objv[1], &n, &m);
        if (quant != SCHEMA_CQUANT_ERROR) {
            goto createpattern;
        }
        quant = SCHEMA_CQUANT_ONE;
    } else {
        quant = getQuant (interp, sdata, objv[2], &n, &m);
        if (quant == SCHEMA_CQUANT_ERROR) {
            return TCL_ERROR;
        }
    }
    if (Tcl_ListObjLength (interp, objv[1], &nrns) != TCL_OK) {
        SetResult ("The <namespace list> argument must be a valid tcl list");
        return TCL_ERROR;
    }
    if (nrns == 1) {
        Tcl_ListObjIndex (interp, objv[1], 0, &nsObj);
        ns1 = Tcl_GetString (nsObj);
        if (ns1[0] == '\0') {
            ns = emptyStr;
        } else {
            ns = getNamespacePtr (sdata, Tcl_GetString (nsObj));
        }
    } else {
        t = TMALLOC (Tcl_HashTable);
        Tcl_InitHashTable (t, TCL_ONE_WORD_KEYS);
        for (i = 0; i < nrns; i++) {
            Tcl_ListObjIndex (interp, objv[1], i, &nsObj);
            ns1 = Tcl_GetString (nsObj);
            if (ns1[0] == '\0') {
                ns = emptyStr;
            } else {
                ns1 = getNamespacePtr (sdata, Tcl_GetString (nsObj));
                Tcl_CreateHashEntry (t, ns1, &hnew);
            }
        }
    }
createpattern:
    pattern = initSchemaCP (SCHEMA_CTYPE_ANY, ns, NULL);
    if (t) pattern->typedata = (void*)t;
    if (revert) pattern->flags |= ANY_NOT;
    REMEMBER_PATTERN (pattern)
    addToContent(sdata, pattern, quant, n, m);
    return TCL_OK;
}

static int
evalDefinition (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *definition,
    SchemaCP *pattern,
    SchemaQuant quant,
    int n,
    int m
    )
{
    SchemaCP *savedCP;
    SchemaAttr **savedCurrentAttrs;
    unsigned int savedContenSize, i;
    unsigned int savedAttrSize, savedNumAttr, savedNumReqAttr;
    int result, onlyName, hnew;
    Tcl_HashEntry *h;
    Tcl_HashTable *t;

    /* Save some state of sdata .. */
    savedCP = sdata->cp;
    savedContenSize = sdata->contentSize;
    savedNumAttr = sdata->numAttr;
    savedNumReqAttr = sdata->numReqAttr;
    savedAttrSize = sdata->attrSize;
    savedCurrentAttrs = sdata->currentAttrs;
    /* ... and prepare sdata for definition evaluation. */
    sdata->cp = pattern;
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
    sdata->numAttr = 0;
    sdata->numReqAttr = 0;
    sdata->currentAttrs = NULL;
    sdata->attrSize = 0;

    sdata->currentEvals++;
    result = Tcl_EvalObjEx (interp, definition, TCL_EVAL_DIRECT);
    sdata->currentEvals--;

    pattern->attrs = sdata->currentAttrs;
    pattern->numAttr = sdata->numAttr;
    pattern->numReqAttr = sdata->numReqAttr;
    /* ... and restore the previously saved sdata states  */
    sdata->cp = savedCP;
    sdata->contentSize = savedContenSize;
    sdata->numAttr = savedNumAttr;
    sdata->numReqAttr = savedNumReqAttr;
    sdata->currentAttrs = savedCurrentAttrs;
    sdata->attrSize = savedAttrSize;

    if (result != TCL_OK) {
        freeSchemaCP (pattern);
        return result;
    }

    REMEMBER_PATTERN (pattern);
    if (pattern->numAttr) {
        attributeLookupPreparation (sdata, pattern);
    }
    if (pattern->type == SCHEMA_CTYPE_CHOICE) {
        onlyName = 1;
        for (i = 0; i < pattern->nc; i++) {
            if (pattern->content[i]->type != SCHEMA_CTYPE_NAME
                && pattern->content[i]->type != SCHEMA_CTYPE_TEXT) {
                onlyName = 0;
                break;
            }
        }
        if (onlyName && pattern->nc > sdata->choiceHashThreshold) {
            t =  TMALLOC (Tcl_HashTable);
            Tcl_InitHashTable (t, TCL_ONE_WORD_KEYS);
            hnew = 1;
            for (i = 0; i < pattern->nc; i++) {
                if (pattern->content[i]->type != SCHEMA_CTYPE_NAME) {
                    continue;
                }
                h = Tcl_CreateHashEntry (t, pattern->content[i]->name, &hnew);
                if (!hnew) {
                    break;
                }
                Tcl_SetHashValue (h, pattern->content[i]);
            }
            if (hnew) {
                pattern->typedata = (void *)t;
            } else {
                /* No simple lookup possible because of more than one
                 * element with the same local name belong to the
                 * choices. Rewind. */
                Tcl_DeleteHashTable (t);
                FREE (t);
            }
        }
    }
    addToContent (sdata, pattern, quant, n, m);
    return TCL_OK;
}

/* Implements the schema definition commands "element" */
static int
ElementPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    Tcl_HashEntry *h;
    SchemaCP *pattern = NULL, *typePattern = NULL, *current;
    SchemaQuant quant;
    int hnew, n, m, typed = 0, ind = 3, localdef = 0;
    char *namePtr;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (2,5,"Expected: elementName ?quant? ?(pattern|\"type\" "
                 "typename)?");

    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2], &n, &m);
    if (quant == SCHEMA_CQUANT_ERROR) {
        /* May be default quant with local definition or type. */
        if (objc != 3 && objc != 4) {
            SetResult("Expected: elementName ?quant? ?(pattern|\"type\" "
                      "typename)?");
            return TCL_ERROR;
        }
        quant = SCHEMA_CQUANT_ONE;
        if (objc == 4) {
            /* Reference to type */
            typed = 1;
        } else {
            ind = 2;
            localdef = 1;
        }
    } else {
        if (objc == 5) {
            typed = 1;
            ind = 4;
        } else if (objc == 4) {
            localdef = 1;
        }
    }
    if (typed) {
            if (strcmp (Tcl_GetString (objv[ind-1]), "type") != 0) {
            SetResult("Expected: elementName ?quant? ?(pattern|\"type\" "
                      "typename)?");
            return TCL_ERROR;
        }
    }
    h = Tcl_CreateHashEntry (&sdata->element, Tcl_GetString(objv[1]), &hnew);
    namePtr = Tcl_GetHashKey (&sdata->element, h);
    if (hnew) {
        pattern = initSchemaCP( SCHEMA_CTYPE_NAME, sdata->currentNamespace,
                                namePtr);
        if (typed || localdef) {
            pattern->flags |= PLACEHOLDER_PATTERN_DEF;
        } else {
            pattern->flags |= FORWARD_PATTERN_DEF;
            sdata->forwardPatternDefs++;
        }
        Tcl_SetHashValue (h, pattern);
        REMEMBER_PATTERN (pattern);
    }
    if (typed) {
        pattern = NULL;
        h = Tcl_CreateHashEntry (&sdata->elementType,
                                 Tcl_GetString (objv[ind]), &hnew);
        if (!hnew) {
            typePattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (typePattern) {
                if (typePattern->namespace == sdata->currentNamespace) {
                    break;
                }
                typePattern = typePattern->next;
            }
        }
        if (!typePattern) {
            typePattern = initSchemaCP (
                SCHEMA_CTYPE_NAME,
                sdata->currentNamespace,
                Tcl_GetHashKey (&sdata->elementType, h));
            typePattern->flags |= (ELEMENTTYPE_DEF | FORWARD_PATTERN_DEF);
            sdata->forwardPatternDefs++;
            REMEMBER_PATTERN (typePattern);
            /* We (ab)use the numAttr for the allocated content length
             * for forward defined types, to be able to store the
             * instance pattern until the type pattern is eventually
             * defined. */
            typePattern->numAttr = CONTENT_ARRAY_SIZE_INIT;
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (h);
                typePattern->next = current;
            }
            Tcl_SetHashValue (h, typePattern);
        }
        h = Tcl_CreateHashEntry (&sdata->elementTypeInstance,
                                 namePtr, &hnew);
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (pattern) {
                if (pattern->namespace == sdata->currentNamespace
                    && pattern->typeptr == typePattern) {
                    break;
                }
                pattern = pattern->next;
            }
        }
        if (!pattern) {
            pattern = TMALLOC (SchemaCP);
            memset (pattern, 0, sizeof(SchemaCP));
            pattern->type = SCHEMA_CTYPE_NAME;
            pattern->namespace = sdata->currentNamespace;
            pattern->name = namePtr;
            pattern->flags |= TYPED_ELEMENT;
            REMEMBER_PATTERN (pattern);
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (h);
                pattern->next = current;
            }
            Tcl_SetHashValue (h, pattern);
            pattern->typeptr = typePattern;
        }
        if (typePattern->flags & FORWARD_PATTERN_DEF) {
            /* Remember the instance pattern with the type pattern
             * (a bit misusing struct members) to be able to set
             * the instance pattern to the actual content if the
             * type pattern is eventually defined. */
            if (typePattern->nc == typePattern->numAttr) {
                typePattern->content =
                    REALLOC (typePattern->content,
                             2 * typePattern->numAttr * sizeof (SchemaCP*));
                typePattern->numAttr *= 2;
            }
            typePattern->content[typePattern->nc] = pattern;
            typePattern->nc++;
        } else {
            pattern->content = typePattern->content;
            pattern->quants = typePattern->quants;
            pattern->nc = typePattern->nc;
            pattern->typedata = typePattern->typedata;
            pattern->attrs = typePattern->attrs;
            pattern->numAttr = typePattern->numAttr;
            pattern->numReqAttr = typePattern->numReqAttr;
            pattern->domKeys = typePattern->domKeys;
            pattern->keySpace = typePattern->keySpace;
            /* TODO: decide what to do with associated */
        }
        addToContent (sdata, pattern, quant, n, m);
    } else if (localdef) {
        pattern = initSchemaCP (SCHEMA_CTYPE_NAME, sdata->currentNamespace,
                                namePtr);
        pattern->flags |= LOCAL_DEFINED_ELEMENT;
        return evalDefinition (interp, sdata, objv[ind], pattern, quant, n, m);
    } else {
        /* Reference to an element. */
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (h);
            while (pattern) {
                if (pattern->namespace == sdata->currentNamespace) {
                    break;
                }
                pattern = pattern->next;
            }
            if (!pattern) {
                pattern = initSchemaCP (SCHEMA_CTYPE_NAME, sdata->currentNamespace,
                                        namePtr);
                pattern->flags |= FORWARD_PATTERN_DEF;
                sdata->forwardPatternDefs++;
                if (!hnew) {
                    current = (SchemaCP *) Tcl_GetHashValue (h);
                    pattern->next = current;
                }
                REMEMBER_PATTERN (pattern);
                Tcl_SetHashValue (h, pattern);
            }
        }
        addToContent (sdata, pattern, quant, n, m);
    }
    return TCL_OK;
}
    

/* Implements the schema definition commands "ref" */
static int
RefPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    Tcl_HashEntry *h;
    SchemaCP *pattern = NULL, *current;
    SchemaQuant quant;
    int hnew, n, m;

    CHECK_SI
    CHECK_TOPLEVEL
        
    checkNrArgs (2,3,"Expected: patternName ?quant?");

    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2], &n, &m);
    if (quant == SCHEMA_CQUANT_ERROR) {
        return TCL_ERROR;
    }
    h = Tcl_CreateHashEntry (&sdata->pattern, Tcl_GetString(objv[1]), &hnew);
    if (!hnew) {
        pattern = (SchemaCP *) Tcl_GetHashValue (h);
        while (pattern) {
            if (pattern->namespace == sdata->currentNamespace) {
                break;
            }
            pattern = pattern->next;
        }
    }
    if (!pattern) {
        pattern = initSchemaCP (
            SCHEMA_CTYPE_PATTERN,
            sdata->currentNamespace,
            Tcl_GetHashKey (&sdata->pattern, h)
            );
        pattern->flags |= FORWARD_PATTERN_DEF;
        sdata->forwardPatternDefs++;
        if (!hnew) {
            current = (SchemaCP *) Tcl_GetHashValue (h);
            pattern->next = current;
        }
        REMEMBER_PATTERN (pattern);
        Tcl_SetHashValue (h, pattern);
    }
    addToContent (sdata, pattern, quant, n, m);
    return TCL_OK;
}

/* Implements the schema definition commands "choice", "group",
 * "interleave" and "mixed" */
static int
AnonPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    Schema_CP_Type patternType;
    SchemaQuant quant;
    SchemaCP *pattern;
    int n, m;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (2,3,"Expected: ?quant? definition");
    
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1], &n, &m);
    if (quant == SCHEMA_CQUANT_ERROR) {
        return TCL_ERROR;
    }
    if (clientData == 0) {
        patternType = SCHEMA_CTYPE_CHOICE;
    } else if (clientData == (ClientData) 1) {
        patternType = SCHEMA_CTYPE_CHOICE;
        /* Default quant for mixed is * */
        if (objc == 2) {
            quant = SCHEMA_CQUANT_REP;
        }
    } else if (clientData == (ClientData) 2) {
        patternType = SCHEMA_CTYPE_INTERLEAVE;
    } else {
        patternType = SCHEMA_CTYPE_PATTERN;
    }

    pattern = initSchemaCP (patternType, NULL, NULL);
    if (clientData == (ClientData) 1) {
        pattern->flags |= MIXED_CONTENT;
    }
    return evalDefinition (interp, sdata, objc == 2 ? objv[1] : objv[2],
                           pattern, quant, n, m);
}

static int maybeAddAttr (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *nameObj,
    Tcl_Obj *namespaceObj,
    Tcl_Obj *scriptObj,
    int required,
    SchemaCP *type
    )
{
    Tcl_HashEntry *h;
    int hnew, result = TCL_OK;
    unsigned int i;
    char *name, *namespace = NULL;
    SchemaAttr *attr;
    SchemaCP *cp;

    if (namespaceObj) {
        namespace = getNamespacePtr (sdata,
                                     Tcl_GetString (namespaceObj));
    }
    h = Tcl_CreateHashEntry (&sdata->attrNames,
                             Tcl_GetString (nameObj), &hnew);
    name = Tcl_GetHashKey (&sdata->attrNames, h);
    if (!hnew) {
        /* Check, if there is already an attribute with this name
           and namespace */
        for (i = 0; i < sdata->numAttr; i++) {
            if (sdata->currentAttrs[i]->name == name
                && sdata->currentAttrs[i]->namespace == namespace) {
                /* Ignore the later attribute declaration */
                return TCL_OK;
            }
        }
    }
    attr = TMALLOC (SchemaAttr);
    attr->namespace = namespace;
    attr->name = name;
    attr->next = NULL;
    attr->required = required;
    if (scriptObj) {
        cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
        cp->type = SCHEMA_CTYPE_TEXT;
        REMEMBER_PATTERN (cp)
        sdata->isAttributeConstraint = 1;
        result = evalConstraints (interp, sdata, cp, scriptObj);
        sdata->isAttributeConstraint = 0;
        attr->cp = cp;
    } else if (type) {
        attr->cp = type;
    } else {
        attr->cp = NULL;
    }
    if (!sdata->currentAttrs) {
        sdata->currentAttrs = MALLOC (sizeof(SchemaAttr*)
                                      * ATTR_ARRAY_INIT);
        sdata->attrSize = ATTR_ARRAY_INIT;
    } else if (sdata->numAttr == sdata->attrSize) {
        sdata->currentAttrs =
            REALLOC (sdata->currentAttrs, 2 * sdata->attrSize
                     * sizeof (SchemaAttr));
        sdata->attrSize *= 2;
    }
    sdata->currentAttrs[sdata->numAttr] = attr;
    sdata->numAttr++;
    if (required) {
        sdata->numReqAttr++;
    }
    return result;
}

static int
AttributePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    char *str;
    int len, required = 1;
    Tcl_Obj *nsObj, *nameObj;
    Tcl_HashEntry *h;
    SchemaCP *type;

    CHECK_SI
    CHECK_TOPLEVEL

    if (sdata->cp->type != SCHEMA_CTYPE_NAME) {
        SetResult ("The commands attribute and nsattribute are only allowed toplevel in element definition scripts");
        return TCL_ERROR;
    }
    if (clientData) {
        checkNrArgs (3,6,"Expected:"
                     "  name namespace"
                     " | name namespace attquant"
                     " | name namespace ?attquant? <constraint script>"
                     " | name namespace ?attquant? \"type\" typename");
        nsObj = objv[2];
    } else {
        checkNrArgs (2,5,"Expected:"
                     "  name"
                     " | name attquant"
                     " | name ?attquant? <constraint script>"
                     " | name ?attquant? \"type\" typename");
        nsObj = NULL;
    }
    nameObj = objv[1];
    if (clientData) {
        objv++;
        objc--;
    }
    if (objc == 2) {
        return maybeAddAttr (interp, sdata, nameObj, nsObj, NULL, 1, NULL);
    }
    str = Tcl_GetStringFromObj (objv[2], &len);
    if (len == 1) {
        if (str[0] == '?') {
            required = 0;
        } else if (str[0] != '!') {
            SetResult ("Invalid attribute quant");
            return TCL_ERROR;
        }
        if (objc == 3) {
            return maybeAddAttr (interp, sdata, nameObj, nsObj, NULL,
                                 required, NULL);
        }
        objv++;
        objc--;
        str = Tcl_GetStringFromObj (objv[2], &len);
    }
    if (objc == 4) {
        if (len != 4
            || strcmp("type", str) != 0) {
            if (clientData) {
                SetResult ("Expected:"
                           "  name namespace"
                           " | name namespace attquant"
                           " | name namespace ?attquant? <constraint script>"
                           " | name namespace ?attquant? \"type\" typename");
            } else {
                SetResult ("Expected:"
                           "  name"
                           " | name attquant"
                           " | name ?attquant? <constraint script>"
                           " | name ?attquant? \"type\" typename");
            }
            return TCL_ERROR;
        }
        h = Tcl_FindHashEntry (&sdata->textDef, Tcl_GetString (objv[3]));
        if (!h) {
            SetResult3 ("Unknown text type \"", Tcl_GetString (objv[3]), "\"");
            return TCL_ERROR;
        }
        type = (SchemaCP *) Tcl_GetHashValue (h);
        return maybeAddAttr (interp, sdata, nameObj, nsObj, NULL,
                             required, type);        
    } else {
        return maybeAddAttr (interp, sdata, nameObj, nsObj, objv[2],
                             required, NULL);
    }
}

static int
NamespacePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    char *currentNamespace;
    int result;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (3,3,"Expected: namespace pattern");

    currentNamespace = sdata->currentNamespace;
    sdata->currentNamespace =
        getNamespacePtr (sdata, Tcl_GetString(objv[1]));
    sdata->currentEvals++;
    result = Tcl_EvalObjEx (interp, objv[2], TCL_EVAL_DIRECT);
    sdata->currentEvals--;
    sdata->currentNamespace = currentNamespace;
    return result;
}

static int
TextPatternObjCmd  (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaQuant quant = SCHEMA_CQUANT_OPT;
    SchemaCP *pattern;
    Tcl_HashEntry *h;
    int result = TCL_OK;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,3,"?<definition script>? | type <name>");
    if (objc == 1) {
        pattern = initSchemaCP (SCHEMA_CTYPE_TEXT, NULL, NULL);
    } else if (objc == 2) {
        quant = SCHEMA_CQUANT_ONE;
        pattern = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
        pattern->type = SCHEMA_CTYPE_TEXT;
    } else {
        if (strcmp("type", Tcl_GetString (objv[1])) != 0) {
            SetResult ("Expected: ?<definition script>? | type <name>");
            return TCL_ERROR;
        }
        h = Tcl_FindHashEntry (&sdata->textDef, Tcl_GetString (objv[2]));
        if (!h) {
            SetResult3 ("Unknown text type \"", Tcl_GetString (objv[2]), "\"");
            return TCL_ERROR;
        }
        quant = SCHEMA_CQUANT_ONE;
        pattern = (SchemaCP *) Tcl_GetHashValue (h);
    }
    if (objc == 2) {
        result = evalConstraints (interp, sdata, pattern, objv[1]);
    }
    if (result == TCL_OK) {
        if (objc < 3) {
            REMEMBER_PATTERN (pattern)
        }
        addToContent (sdata, pattern, quant, 0, 0);
    } else {
        freeSchemaCP (pattern);
    }
    return result;
}

static int
VirtualPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *pattern;
    int i;

    CHECK_SI
    CHECK_TOPLEVEL
    if (objc < 2) {
        SetResult ("Expected: <tclcmd> ?arg? ?arg? ...");
        return TCL_ERROR;
    }

    if (sdata->cp->type != SCHEMA_CTYPE_NAME
        && sdata->cp->type != SCHEMA_CTYPE_PATTERN) {
        SetResult ("The \"tcl\" schema definition command is only "
                   "allowed in sequential context (defelement, "
                   "element, group or defpattern)");
        return TCL_ERROR;
    }

    pattern = initSchemaCP (SCHEMA_CTYPE_VIRTUAL, NULL, NULL);
    REMEMBER_PATTERN (pattern)
    pattern->content = MALLOC (sizeof (Tcl_Obj*) * (objc-1));
    for (i = 0; i < objc-1; i++) {
        pattern->content[i] = (SchemaCP *) objv[i+1];
        Tcl_IncrRefCount (objv[i+1]);
    }
    pattern->nc = objc-1;
    addToContent (sdata, pattern, SCHEMA_CQUANT_ONE, 0, 0);
    return TCL_OK;
}

static int
SelfObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;

    CHECK_SI
    CHECK_TOPLEVEL
    if (objc != 1) {
        SetResult ("No argument expected");
        return TCL_ERROR;
    }
    Tcl_SetObjResult (interp, Tcl_DuplicateObj (sdata->self));
    return TCL_OK;
}

static int
domuniquePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    ast t;
    char *errMsg = NULL;
    domKeyConstraint *kc, *kc1;
    int i, nrFields, flags = 0;
    Tcl_Obj *elm;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (3, 6, "Expected: <selector> <fieldlist> ?<name>? ?\"IGNORE_EMPTY_FIELD_SET\"|(?\"EMPTY_FIELD_SET_VALUE\" <emptyFieldSetValue?)");
    if (sdata->cp->type != SCHEMA_CTYPE_NAME) {
        SetResult ("The domunique schema definition command is only "
                   "allowed as direct child of an element.");
    }
    if (Tcl_ListObjLength (interp, objv[2], &nrFields) != TCL_OK) {
        SetResult ("The <fieldlist> argument must be a valid tcl list");
        return TCL_ERROR;
    }
    if (nrFields == 0) {
        SetResult ("Non empty fieldlist argument expected.");
        return TCL_ERROR;
    }
    if (objc == 5) {
        if (strcmp (Tcl_GetString (objv[4]), "IGNORE_EMPTY_FIELD_SET") != 0) {
            SetResult3 ("Unknown flag '", Tcl_GetString (objv[4]), "'");
            return TCL_ERROR;
        }
        flags |= DKC_FLAG_IGNORE_EMPTY_FIELD_SET;
    }
    if (objc == 6) {
        if (strcmp (Tcl_GetString (objv[4]), "EMPTY_FIELD_SET_VALUE") != 0) {
            SetResult3 ("Unknown flag '", Tcl_GetString (objv[4]), "'");
            return TCL_ERROR;
        }
    }
    
    if (xpathParse (Tcl_GetString (objv[1]), NULL, XPATH_EXPR,
                    sdata->prefixns, NULL, &t, &errMsg) < 0) {
        SetResult3 ("Error in selector xpath: '", errMsg, "");
        FREE (errMsg);
        return TCL_ERROR;
    }
    
    kc = TMALLOC (domKeyConstraint);
    memset (kc, 0, sizeof (domKeyConstraint));
    kc->fields = MALLOC (sizeof (ast) * nrFields);
    memset (kc->fields, 0, sizeof (ast) * nrFields);
    kc->nrFields = nrFields;
    kc->selector = t;
    kc->flags = flags;
    for (i = 0; i < nrFields; i++) {
        Tcl_ListObjIndex (interp, objv[2], i, &elm);
        if (xpathParse (Tcl_GetString (elm), NULL, XPATH_EXPR,
                        sdata->prefixns, NULL, &t, &errMsg) < 0) {
            SetResult3 ("Error in field xpath: '", errMsg, "");
            FREE (errMsg);
            xpathFreeAst (t);
            freedomKeyConstraints (kc);
            return TCL_ERROR;
        }
        kc->fields[i] = t;
    }
    if (objc >= 4) {
        kc->name = tdomstrdup (Tcl_GetString (objv[3]));
    }
    if (objc == 6) {
        kc->emptyFieldSetValue = tdomstrdup (Tcl_GetString (objv[5]));
        kc->efsv_len = strlen (kc->emptyFieldSetValue);
    }
    /* Append to end so that the constraints are checked in
     * definition order */
    if (sdata->cp->domKeys) {
        kc1 = sdata->cp->domKeys;
        while (1) {
            if (kc1->next) kc1 = kc1->next;
            else break;
        }
        kc1->next = kc;
    } else {
        sdata->cp->domKeys = kc;
    }
    return TCL_OK;
}

static int
domxpathbooleanPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    ast t;
    char *errMsg = NULL;
    domKeyConstraint *kc, *kc1;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (2, 3, "Expected: <selector> ?<name>?");
    if (sdata->cp->type != SCHEMA_CTYPE_NAME) {
        SetResult ("The domxpathboolean schema definition command is only "
                   "allowed as direct child of an element.");
    }
    if (xpathParse (Tcl_GetString (objv[1]), NULL, XPATH_EXPR,
                    sdata->prefixns, NULL, &t, &errMsg) < 0) {
        SetResult3 ("Error in selector xpath: '", errMsg, "");
        FREE (errMsg);
        return TCL_ERROR;
    }

    kc = TMALLOC (domKeyConstraint);
    memset (kc, 0, sizeof (domKeyConstraint));
    kc->selector = t;
    kc->flags |= DKC_FLAG_BOOLEAN;
    if (objc == 3) {
        kc->name = tdomstrdup (Tcl_GetString (objv[2]));
    }
    /* Append to end so that the constraints are checked in
     * definition order */
    if (sdata->cp->domKeys) {
        kc1 = sdata->cp->domKeys;
        while (1) {
            if (kc1->next) kc1 = kc1->next;
            else break;
        }
        kc1->next = kc;
    } else {
        sdata->cp->domKeys = kc;
    }
    return TCL_OK;
}

static int
keyspacePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *pattern;
    int nrKeyspaces, i, hnew;
    Tcl_Obj *ksObj;
    SchemaKeySpace *ks;
    Tcl_HashEntry *h;
    
    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (3, 3, "Expected: <keyspace-name list> pattern");
    if (sdata->cp->type != SCHEMA_CTYPE_NAME
        && sdata->cp->type != SCHEMA_CTYPE_PATTERN) {
        SetResult ("The keyspace schema definition command is only "
                   "allowed in sequential context (defelement, "
                   "element or defpattern)");
        return TCL_ERROR;
    }
    if (Tcl_ListObjLength (interp, objv[1], &nrKeyspaces) != TCL_OK) {
        SetResult ("The <keyspace-name list> argument must be a valid tcl "
                   "list");
        return TCL_ERROR;
    }
    for (i = 0; i < nrKeyspaces; i++) {
        Tcl_ListObjIndex (interp, objv[1], i, &ksObj);
        h = Tcl_CreateHashEntry (&sdata->keySpaces,
                                 Tcl_GetString (ksObj), &hnew);
        if (hnew) {
            ks = TMALLOC (SchemaKeySpace);
            ks->name = Tcl_GetHashKey (&sdata->keySpaces, h);
            ks->active = 0;
            ks->unknownIDrefs = 0;
            Tcl_SetHashValue (h, ks);
        } else {
            ks = Tcl_GetHashValue (h);
        }
        pattern = initSchemaCP (SCHEMA_CTYPE_KEYSPACE,
                                Tcl_GetString (ksObj), NULL);
        pattern->keySpace = ks;
        REMEMBER_PATTERN (pattern);
        addToContent (sdata, pattern, SCHEMA_CQUANT_ONE, 0, 0);
    }
    sdata->currentEvals++;
    if (Tcl_EvalObjEx (interp, objv[2], TCL_EVAL_DIRECT) != TCL_OK) {
        return TCL_ERROR;
    }
    sdata->currentEvals--;
    for (i = 0; i < nrKeyspaces; i++) {
        Tcl_ListObjIndex (interp, objv[1], i, &ksObj);
        h = Tcl_FindHashEntry (&sdata->keySpaces, Tcl_GetString(ksObj));
        pattern = initSchemaCP (SCHEMA_CTYPE_KEYSPACE_END,
                                Tcl_GetString (ksObj), NULL);
        REMEMBER_PATTERN (pattern);
        pattern->keySpace = Tcl_GetHashValue (h);
        addToContent (sdata, pattern, SCHEMA_CQUANT_ONE, 0, 0);
    }
    return TCL_OK;
}

static int
associatePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    
    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (2, 2, "Expected: data");
    switch (sdata->cp->type) {
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
    case SCHEMA_CTYPE_INTERLEAVE:
        break;
    default:
        SetResult ("The associate schema definition command is only "
                   "allowed inside of global or local element, pattern or "
                   "interleval context");
        return TCL_ERROR;
    }
    if (sdata->cp->associated) {
        Tcl_DecrRefCount (sdata->cp->associated);
    }
    sdata->cp->associated = objv[1];
    Tcl_IncrRefCount (sdata->cp->associated);
    return TCL_OK;
}

static int
integerImplXsd (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *c = text;
    if (*c == 0) return 0;
    switch ((intptr_t)constraintData) {
    case 0:
        /* integer */
        if (*c == '-' || *c == '+') c++;
        break;
    case 1:
        /* negativeInteger */
        if (*c != '-') return 0;
        c++;
        while (*c == '0') c++;
        break;
    case 2:
        /* nonNegativeInteger */
        if (*c == '+') c++;
        else if (*c == '-') {
            c++;
            if (*c == '0') {
                c++;
                while (*c == '0') c++;
                if (*c == 0) return 1;
            }
            return 0;
        }
        break;
    case 3:
        /* nonPositiveInteger */
        if (*c == '-') c++;
        else {
            if (*c == '+') c++;
            if (*c == 0) return 0;
            while (*c == '0') c++;
            if (*c == 0) return 1;
            return 0;
        }
        break;
    case 4:
        /* positiveInteger */
        if (*c == '+') c++;
        while (*c == '0') c++;
        break;
    }
    if (*c == 0) return 0;
    while (isdigit(*c)) {
        c++;
    }
    if (*c != 0) return 0;
    return 1;
}

static int
integerImplTcl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int n;

    if (Tcl_GetInt (interp, text, &n) != TCL_OK) {
        return 0;
    }
    switch ((intptr_t)constraintData) {
    case 0:
        /* integer */
        break;
    case 1:
        /* negativeInteger */
        if (n >= 0) return 0;
        break;
    case 2:
        /* nonNegativeInteger */
        if (n < 0) return 0;
        break;
    case 3:
        /* nonPositiveInteger */
        if (n > 0) return 0;
        break;
    case 4:
        /* positiveInteger */
        if (n <= 0) return 0;
        break;
    }
    
    return 1;
}

static int
integerTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int type;

    static const char *types[] = {
        "xsd", "tcl", NULL
    };
    enum typeSyms {
        t_xsd, t_tcl
    };

    CHECK_TI
    checkNrArgs (1,2,"?xsd|tcl?");
    if (objc == 1) {
        type = t_xsd;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], types, "type", 0, &type)
            != TCL_OK) {
            return TCL_ERROR;
        }
    }
    ADD_CONSTRAINT (sdata, sc)
    switch ((enum typeSyms) type) {
    case t_xsd:
        sc->constraint = integerImplXsd;
        break;
    case t_tcl:
        sc->constraint = integerImplTcl;
        break;
    }
    sc->constraintData = clientData;
    return TCL_OK;
}

typedef struct
{
    int nrArg;
    Tcl_Obj **evalStub;
    SchemaData *sdata;
} tclTCData;

static void
tclImplFree (
    void *constraintData
    )
{
    tclTCData *tcdata = constraintData;
    int i;

    for (i = 0; i < tcdata->nrArg-1; i++) {
        Tcl_DecrRefCount (tcdata->evalStub[i]);
    }
    FREE (tcdata->evalStub);
    FREE (tcdata);
}

static int
tclImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    tclTCData *tcdata = constraintData;
    int result, bool;

    tcdata->evalStub[tcdata->nrArg-1] = Tcl_NewStringObj(text, -1);
    Tcl_IncrRefCount (tcdata->evalStub[tcdata->nrArg-1]);
    tcdata->sdata->currentEvals++;
    result = Tcl_EvalObjv (interp, tcdata->nrArg, tcdata->evalStub,
                           TCL_EVAL_GLOBAL);
    tcdata->sdata->currentEvals--;
    Tcl_DecrRefCount (tcdata->evalStub[tcdata->nrArg-1]);
    if (result != TCL_OK) {
        tcdata->sdata->evalError = 1;
        return 0;
    }
    result = Tcl_GetBooleanFromObj (interp, Tcl_GetObjResult (interp), &bool);
    if (result != TCL_OK) {
        return 0;
    }
    if (bool) {
        return 1;
    } 
    return 0;
}

static int
tclTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    tclTCData *tcdata;
    int i;

    CHECK_TI
    if (objc < 2) {
        SetResult ("Expected: tclcmd ?arg arg ...?");
        return TCL_ERROR;
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = tclImpl;
    sc->freeData = tclImplFree;
    tcdata = TMALLOC (tclTCData);
    tcdata->nrArg = objc;
    tcdata->evalStub = MALLOC (sizeof (Tcl_Obj*) * objc);
    for (i = 1; i < objc; i++) {
        tcdata->evalStub[i-1] = objv[i];
        Tcl_IncrRefCount (tcdata->evalStub[i-1]);
    }
    tcdata->sdata = sdata;
    sc->constraintData = tcdata;
    return TCL_OK;
}

static void
fixedImplFree (
    void *constraintData
    )
{
    FREE (constraintData);
}

static int
fixedImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    if (strcmp (text, (char *) constraintData) == 0) {
        return 1;
    }
    return 0;
}

static int
fixedTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <fixed value>");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = fixedImpl;
    sc->freeData = fixedImplFree;
    sc->constraintData = tdomstrdup (Tcl_GetString (objv[1]));
    return TCL_OK;
}

static void
enumerationImplFree (
    void *constraintData
    )
{
    Tcl_HashTable *values = (Tcl_HashTable *) constraintData;

    Tcl_DeleteHashTable (values);
    FREE (values);
}

static int
enumerationImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    Tcl_HashTable *values = (Tcl_HashTable *) constraintData;

    if (Tcl_FindHashEntry(values, text)) return 1;
    return 0;
}

static int
enumerationTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashTable *values;
    int len, i, hnew;
    Tcl_Obj *value;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <value list>");
    if (Tcl_ListObjLength (interp, objv[1], &len) != TCL_OK) {
        SetResult ("The argument must be a valid tcl list");
        return TCL_ERROR;
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = enumerationImpl;
    sc->freeData = enumerationImplFree;
    values = TMALLOC (Tcl_HashTable);
    Tcl_InitHashTable (values, TCL_STRING_KEYS);
    for (i = 0; i < len; i++) {
        Tcl_ListObjIndex (interp, objv[1], i, &value);
        Tcl_CreateHashEntry (values, Tcl_GetString (value), &hnew);
    }
    sc->constraintData = values;
    return TCL_OK;
}

static void
matchImplFree (
    void *constraintData
    )
{
    Tcl_DecrRefCount ((Tcl_Obj *) constraintData);
}

static int
matchImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    if (Tcl_StringCaseMatch (text, Tcl_GetString ((Tcl_Obj *) constraintData), 0))
        return 1;
    return 0;
}

static int
matchNocaseImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    if (Tcl_StringCaseMatch (text, Tcl_GetString ((Tcl_Obj *) constraintData),
            TCL_MATCH_NOCASE))
        return 1;
    return 0;
}

static int
matchTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (2,3,"Expected: ?-nocase? <match pattern>");
    if (objc == 3) {
        if (strcmp ("-nocase", Tcl_GetString (objv[1])) != 0) {
            SetResult ("Expected: ?-nocase? <match pattern>");
            return TCL_ERROR;
        }
        objv++;
    }
    ADD_CONSTRAINT (sdata, sc)
    if (objc == 2) {
        sc->constraint = matchImpl;
    } else {
        sc->constraint = matchNocaseImpl;
    }
    sc->freeData = matchImplFree;
    Tcl_IncrRefCount (objv[1]);
    sc->constraintData = objv[1];
    return TCL_OK;
}

static void
regexpImplFree (
    void *constraintData
    )
{
    Tcl_DecrRefCount ((Tcl_Obj *) constraintData);
}

static int
regexpImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    Tcl_Obj *textObj;
    int rc;


    textObj = Tcl_NewStringObj(text, -1);
    rc = Tcl_RegExpMatchObj (interp, textObj,  (Tcl_Obj *) constraintData);
    Tcl_DecrRefCount (textObj);
    /* rc may be 1, 0, -1 */
    if (rc == 1) {
        return 1;
    }
    return 0;
}

static int
regexpTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <regexp>");
    /* Compile it as syntax test (plus caches the complied regexp in
     * the internal value) */
    if (!Tcl_GetRegExpFromObj (interp, objv[1], TCL_REG_ADVANCED)) {
        return TCL_ERROR;
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = regexpImpl;
    sc->freeData = regexpImplFree;
    Tcl_IncrRefCount (objv[1]);
    sc->constraintData = objv[1];
    return TCL_OK;
}

static int
nmtokenImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *p;
    int clen, tokenSeen = 0;

    p = text;
    /* Skip leading space */
    while (*p && *p == ' ') {
        p++;
    }
    while (*p && *p != ' ') {
        clen = UTF8_CHAR_LEN (*p);
        if (!clen) {
            SetResult ("Invalid UTF-8 character");
            return 0;
        }
        if (!UTF8_GET_NAMING_NMTOKEN (p, clen)) {
            SetResult ("Attribute value isn't a NMTOKEN");
            return 0;
        }
        tokenSeen = 1;
        p += clen;
    }
    /* Skip following space */
    while (*p && *p == ' ') {
        p++;
    }
    if (*p) {
        SetResult ("Attribute value isn't a NMTOKEN");
        return 0;
    }
    if (!*p && !tokenSeen) {
        SetResult ("Missing NMTOKEN value");
        return 0;
    }
    return 1;
}

static int
nmtokenTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = nmtokenImpl;
    return TCL_OK;
}

static int
nmtokensImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *p;
    int clen, tokenSeen = 0;

    p = text;
    /* Skip leading space */
    while (*p && *p == ' ') {
        p++;
    }
    while (*p) {
        if (*p == ' ') {
            p++; continue;
        }
        clen = UTF8_CHAR_LEN (*p);
        if (!clen) {
            SetResult ("Invalid UTF-8 character");
            return 0;
        }
        if (!UTF8_GET_NAMING_NMTOKEN (p, clen)) {
            SetResult ("Invalid character: attribute value isn't a NMTOKENS");
            return 0;
        }
        tokenSeen = 1;
        p += clen;
    }
    /* Any following space is already skipped above */
    if (!tokenSeen) {
        SetResult ("Missing NMTOKENS value");
        return 0;
    }
    return 1;
}

static int
nmtokensTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = nmtokensImpl;
    return TCL_OK;
}

static int
numberImplXsd (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *c = text;
    if (!*c) return 0;
    if (*c == '-' || *c == '+') c++;
    while (isdigit(*c)) {
        c++;
    }
    if (*c == '.') c++;
    while (isdigit(*c)) {
        c++;
    }
    if (*c) return 0;
    return 1;
}

static int
numberImplTcl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    double d;

    if (Tcl_GetDouble (interp, text, &d) != TCL_OK) {
        return 0;
    }
    return 1;
}

static int
numberTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int type;

    static const char *types[] = {
        "xsd", "tcl", NULL
    };
    enum typeSyms {
        t_xsd, t_tcl
    };

    CHECK_TI
    checkNrArgs (1,2,"?xsd|tcl?");
    if (objc == 1) {
        type = t_xsd;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], types, "type", 0, &type)
            != TCL_OK) {
            return TCL_ERROR;
        }
    }
    ADD_CONSTRAINT (sdata, sc)
    switch ((enum typeSyms) type) {
    case t_xsd:
        sc->constraint = numberImplXsd;
        break;
    case t_tcl:
        sc->constraint = numberImplTcl;
        break;
    }
    return TCL_OK;
}

static int
booleanImplXsd (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *c = text;
    switch (*c) {
    case '0':
    case '1':
        c++;
        if (*c == 0) return 1;
        break;
    case 't':
        if (strcmp (text, "true") == 0) return 1;
        break;
    case 'f':
        if (strcmp (text, "false") == 0) return 1;
        break;
    }
    return 0;
}

static int
booleanImplTcl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int b;

    if (Tcl_GetBoolean (interp, text, &b) != TCL_OK) {
        return 0;
    }
    return 1;
}

static int
booleanTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int type;

    static const char *types[] = {
        "xsd", "tcl", NULL
    };
    enum typeSyms {
        t_xsd, t_tcl
    };
    
    CHECK_TI
    checkNrArgs (1,2,"?xsd|tcl?");
    if (objc == 1) {
        type = t_xsd;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], types, "type", 0, &type)
            != TCL_OK) {
            return TCL_ERROR;
        }
    }
    ADD_CONSTRAINT (sdata, sc)
    switch ((enum typeSyms) type) {
    case t_xsd:
        sc->constraint = booleanImplXsd;
        break;
    case t_tcl:
        sc->constraint = booleanImplTcl;
        break;
    }
    return TCL_OK;
}

static int
isodateImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int i, y, m, d, h, min, s, zh, zm, seenNonzero = 0;

    if (constraintData < (void *)2) {
        if (*text == '-') {
            /* A bce date */
            text++;
        }
        i = 1;
        /* Parse year */
        while (*text >= '0' && *text <= '9') {
            if (*text > '0' && !seenNonzero) seenNonzero = i;
            text++;
            i++;
        }
        /* Premature end */
        if (i < 5) return 0;
        if (i > 5) {
            /* The year has more than 4 digits. Only allowed if in fact
             * needed (no extra leading zeros). */
            if (seenNonzero > 1) return 0;
        }
        if (*text != '-') return 0;
        /* We only need to know the modulo of the year for 4, 100 and 400,
         * for this the 4 last letters are enough */
        y = atoi(text-4);
        /* There isn't a year 0. it's either 0001 or -0001 */
        if (!seenNonzero) return 0;
        text++;
        /* Parse month */
        for (i = 0; i < 2; i++) {
            if (*text < '0' || *text > '9') return 0;
            text++;
        }
        if (*text != '-') return 0;
        m = atoi(text-2);
        if (m < 1 || m > 12) return 0;
        text++;
        /* Parse day */
        for (i = 0; i < 2; i++) {
            if (*text < '0' || *text > '9') return 0;
            text++;
        }
        d = atoi(text-2);
        if (d < 1) return 0;
        switch (m) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            if (d > 31) return 0;
            break;
        case 4:
        case 6:
        case 9:
        case 11:
            if (d > 30) return 0;
            break;
        case 2:
            if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) {
                if (d > 29) return 0;
            } else {
                if (d > 28) return 0;
            }
            break;
        }
    }
    /* Date part end */
    if (constraintData) {
        if (constraintData == (void *)1) {
            /* Time part starts */
            if (*text != 'T') return 0;
            text++;
        }
        /* Parse hour part */
        if (*text < '0' || *text > '9') return 0;
        h = (*text - 48) * 10;
        text++;
        if (*text < '0' || *text > '9') return 0;
        h += (*text - 48);
        if (h > 24) return 0;
        text++;
        if (*text != ':') return 0;
        text++;
        /* Parse minute part */
        if (*text < '0' || *text > '9') return 0;
        min = (*text - 48) * 10;
        text++;
        if (*text < '0' || *text > '9') return 0;
        min += (*text - 48);
        if (min > 59) return 0;
        text++;
        if (*text != ':') return 0;
        text++;
        /* Parse seconds part */
        if (*text < '0' || *text > '9') return 0;
        s = (*text - 48) * 10;
        text++;
        if (*text < '0' || *text > '9') return 0;
        s += (*text - 48);
        if (s > 59) return 0;
        text++;
        /* Check for optional fraction seconds part */
        if (*text == '.') {
            if (h == 24) return 0;
            text++;
            /* Dangling decimal point is not allowed */
            if (*text < '0' || *text > '9') return 0;
            text++;
            while (*text >= '0' && *text <= '9') text++;
        }
        if (h == 24 && (min > 0 || s > 0)) return 0;
    }
    if (*text == '\0') return 1;
    /* Parse optional time zone part */
    switch (*text) {
    case 'Z':
        text++;
        if (*text != '\0') return 0;
        break;
    case '+':
    case '-':
        text++;
        for (i = 0; i < 2; i++) {
            if (*text < '0' || *text > '9') return 0;
            text++;
        }
        if (*text != ':') return 0;
        zh = atoi(text-2);
        if (zh > 14) return 0;
        text++;
        for (i = 0; i < 2; i++) {
            if (*text < '0' || *text > '9') return 0;
            text++;
        }
        if (*text != '\0') return 0;
        zm = atoi(text-2);
        if (zh < 14) {
            if (zm > 59) return 0;
        } else {
            if (zm != 0) return 0;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

static int
dateTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = isodateImpl;
    sc->constraintData = (void *) 0;
    return TCL_OK;
}

static int
dateTimeTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = isodateImpl;
    sc->constraintData = (void *) 1;
    return TCL_OK;
}

static int
timeTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = isodateImpl;
    sc->constraintData = (void *) 2;
    return TCL_OK;
}

static int
maxLengthImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    unsigned int len = 0, maxlen = PTR2UINT(constraintData);
    int clen;

    while (*text != '\0') {
        clen = UTF8_CHAR_LEN (*text);
        if (!clen) {
            SetResult ("Invalid UTF-8 character");
            return 0;
        }
        len++;
        if (len > maxlen) return 0;
        text += clen;
    }
    return 1;
}

static int
maxLengthTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int len;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <maximal length as integer>");
    if (Tcl_GetIntFromObj (interp, objv[1], &len) != TCL_OK) {
        SetResult ("Expected: <maximal length as integer>");
        return TCL_ERROR;
    }
    if (len < 1) {
        SetResult ("The maximum length must be at least 1");
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = maxLengthImpl;
    sc->constraintData = UINT2PTR(len);
    return TCL_OK;
}

static int
minLengthImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    unsigned int len = 0, minlen = PTR2UINT(constraintData);
    int clen;
    while (*text != '\0') {
        clen = UTF8_CHAR_LEN (*text);
        if (!clen) {
            SetResult ("Invalid UTF-8 character");
            return 0;
        }
        len++;
        if (len >= minlen) return 1;
        text += clen;
    }
    return 0;
}

static int
minLengthTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int len;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <minimum length as integer>");
    if (Tcl_GetIntFromObj (interp, objv[1], &len) != TCL_OK) {
        SetResult ("Expected: <minimum length as integer>");
        return TCL_ERROR;
    }
    if (len < 1) {
        SetResult ("The minimum length must be at least 1");
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = minLengthImpl;
    sc->constraintData = UINT2PTR(len);
    return TCL_OK;
}

static int
oneOfImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaCP *cp = (SchemaCP *) constraintData;
    SchemaConstraint *sc;
    unsigned int i;

    /* Look also at checkText */
    for (i = 0; i < cp->nc; i++) {
        sc = (SchemaConstraint *) cp->content[i];
        if ((sc->constraint) (interp, sc->constraintData, text)) {
            return 1;
        }
    }
    return 0;
}

static int
oneOfTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int rc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <text constraint script>");
    
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    rc = evalConstraints (interp, sdata, cp, objv[1]);
    if (rc == TCL_OK) {
        ADD_CONSTRAINT (sdata, sc)
        sc->constraint = oneOfImpl;
        sc->constraintData = (void *)cp;
        return TCL_OK;
    }
    return TCL_ERROR;
}

static int
allOfTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int rc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <text constraint script>");
    
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    rc = evalConstraints (interp, sdata, cp, objv[1]);
    if (rc == TCL_OK) {
        ADD_CONSTRAINT (sdata, sc)
        sc->constraint = checkText;
        sc->constraintData = (void *)cp;
        return TCL_OK;
    }
    return TCL_ERROR;
}

static int
stripImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaCP *cp = (SchemaCP *) constraintData;
    int rc, restore = 0;
    char *end, saved;

    while(SPACE((unsigned char)*text)) text++;
    if(*text != 0) {
        /* Not white space only */
        /* Trim trailing space */
        end = text + strlen(text) - 1;
        while(end > text && SPACE((unsigned char)*end)) end--;
        saved = end[1];
        restore = 1;
        end[1] = '\0';
    }
    rc = checkText (interp, cp, text);
    if (restore) end[1] = saved;
    return rc;
}

static int
stripTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int rc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <text constraint script>");
    
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    rc = evalConstraints (interp, sdata, cp, objv[1]);
    if (rc == TCL_OK) {
        ADD_CONSTRAINT (sdata, sc)
        sc->constraint = stripImpl;
        sc->constraintData = (void *)cp;
        return TCL_OK;
    }
    return TCL_ERROR;
}

static int
splitWhitespaceImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaCP *cp = (SchemaCP *) constraintData;
    int rc = 0;
    char *p, *end, saved = 0;
    
    p = text;
    while (*p != 0) {
        while(SPACE (*p)) p++;
        if (*p == 0) break;
        end = p; end++;
        while (*end != 0 && !SPACE(*end)) end++;
        saved = *end;
        *end = 0;
        rc = checkText (interp, cp, p);
        *end = saved;
        p = end;
        if (!rc) break;
    }
    return rc;
}

typedef struct
{
    int         nrArg;
    Tcl_Obj   **evalStub;
    SchemaData *sdata;
    SchemaCP   *cp;
} splitTclTCData;


static int
splitTclImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    splitTclTCData *tcdata = (splitTclTCData *) constraintData;
    int rc, listlen, i;
    Tcl_Obj *list, *listelm;

    tcdata->evalStub[tcdata->nrArg-1] = Tcl_NewStringObj(text, -1);
    Tcl_IncrRefCount (tcdata->evalStub[tcdata->nrArg-1]);
    tcdata->sdata->currentEvals++;
    rc = Tcl_EvalObjv (interp, tcdata->nrArg, tcdata->evalStub,
                       TCL_EVAL_GLOBAL);
    tcdata->sdata->currentEvals--;
    Tcl_DecrRefCount (tcdata->evalStub[tcdata->nrArg-1]);
    if (rc != TCL_OK) {
        tcdata->sdata->evalError = 1;
        return 0;
    }
    list = Tcl_GetObjResult (interp);
    Tcl_IncrRefCount (list);
    Tcl_ResetResult (interp);
    if (Tcl_ListObjLength (interp, list, &listlen) != TCL_OK) {
        Tcl_DecrRefCount (list);
        tcdata->sdata->evalError = 1;
        return 0;
    }
    rc = 0;
    for (i = 0; i < listlen; i++) {
        Tcl_ListObjIndex (interp, list, i, &listelm);
        rc = checkText (interp, tcdata->cp, Tcl_GetString (listelm));
        if (!rc) break;
    }
    Tcl_DecrRefCount (list);
    return rc;
}

static void
splitTclImplFree (
    void *constraintData
    )
{
    splitTclTCData *tcdata = constraintData;
    int i;

    for (i = 0; i < tcdata->nrArg-1; i++) {
        Tcl_DecrRefCount (tcdata->evalStub[i]);
    }
    FREE (tcdata->evalStub);
    FREE (tcdata);
}

static int
splitTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int methodIndex, rc, i;
    splitTclTCData *tcdata;
    
    static const char *methods[] = {
        "whitespace", "tcl", NULL
    };
    enum method {
        m_whitespace, m_tcl
    };

    CHECK_TI
    if (objc < 2) {
        SetResult("Expected: ?type ?args?? <text constraint script>");
        return TCL_ERROR;
    }
    
    if (objc == 2) {
        methodIndex = m_whitespace;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], methods, "type", 0,
                                 &methodIndex) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    switch ((enum method) methodIndex) {
    case m_whitespace:
        if (objc > 2) {
            SetResult ("Type whitespace expects no argument.");
            return TCL_ERROR;
        }
        break;
    case m_tcl:
        if (objc < 3) {
            SetResult ("Expected: tclcmd ?arg ...?.");
            return TCL_ERROR;
        }
    }
    
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    rc = evalConstraints (interp, sdata, cp, objv[objc-1]);
    if (rc != TCL_OK) {
        return TCL_ERROR;
    }
    ADD_CONSTRAINT (sdata, sc)
    switch ((enum method) methodIndex) {
    case m_whitespace:
        sc->constraint = splitWhitespaceImpl;
        sc->constraintData = cp;
        break;
    case m_tcl:
        sc->constraint = splitTclImpl;
        sc->freeData = splitTclImplFree;
        tcdata = TMALLOC (splitTclTCData);
        tcdata->nrArg = objc - 2;
        tcdata->evalStub = MALLOC (sizeof (Tcl_Obj*) * (objc-2));
        for (i = 2; i < objc -1; i++) {
            tcdata->evalStub[i-2] = objv[i];
            Tcl_IncrRefCount (tcdata->evalStub[i-2]);
        }
        tcdata->sdata = sdata;
        tcdata->cp = cp;
        sc->constraintData = tcdata;
    }
    return TCL_OK;
}

static int
idImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaData *sdata = (SchemaData *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    h = Tcl_CreateHashEntry (&sdata->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 1);
        return 1;
    }
    if (Tcl_GetHashValue (h) == 0) {
        Tcl_SetHashValue (h, 1);
        sdata->unknownIDrefs--;
        return 1;
    } else {
        /* Duplicate ID value */
        return 0;
    }
}

static int
docidImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaDocKey *dk = (SchemaDocKey *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    h = Tcl_CreateHashEntry (&dk->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 1);
        return 1;
    }
    if (Tcl_GetHashValue (h) == 0) {
        Tcl_SetHashValue (h, 1);
        dk->unknownIDrefs--;
        return 1;
    } 
    /* Duplicate ID value */
    return 0;
}

static int
idTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashEntry *h;
    int hnew;
    SchemaDocKey *dk;

    CHECK_TI
    checkNrArgs (1,2,"?key_space?");
    ADD_CONSTRAINT (sdata, sc)
    if (objc == 1) {
        sc->constraint = idImpl;
        sc->constraintData = (void *)sdata;
    } else {
        h = Tcl_CreateHashEntry (&sdata->idTables, Tcl_GetString (objv[1]),
                                 &hnew);
        if (hnew) {
            dk = TMALLOC (SchemaDocKey);
            Tcl_InitHashTable (&dk->ids, TCL_STRING_KEYS);
            dk->unknownIDrefs = 0;
            Tcl_SetHashValue (h, dk);
        } else {
            dk = Tcl_GetHashValue (h);
        }
        sc->constraint = docidImpl;
        sc->constraintData = (void *)dk;
    }
    return TCL_OK;
}

static int
idrefImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaData *sdata = (SchemaData *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    h = Tcl_CreateHashEntry (&sdata->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 0);
        sdata->unknownIDrefs++;
    }
    return 1;
}

static int
docidrefImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaDocKey *dk = (SchemaDocKey *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    h = Tcl_CreateHashEntry (&dk->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 0);
        dk->unknownIDrefs++;
    }
    return 1;
}

static int
idrefTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashEntry *h;
    int hnew;
    SchemaDocKey *dk;

    CHECK_TI
    checkNrArgs (1,2,"?key_space?");
    ADD_CONSTRAINT (sdata, sc)
    if (objc == 1) {
        sc->constraint = idrefImpl;
        sc->constraintData = (void *)sdata;
    } else {
        h = Tcl_CreateHashEntry (&sdata->idTables, Tcl_GetString (objv[1]),
                                 &hnew);
        if (hnew) {
            dk = TMALLOC (SchemaDocKey);
            Tcl_InitHashTable (&dk->ids, TCL_STRING_KEYS);
            dk->unknownIDrefs = 0;
            Tcl_SetHashValue (h, dk);
        } else {
            dk = Tcl_GetHashValue (h);
        }
        sc->constraint = docidrefImpl;
        sc->constraintData = (void *)dk;
    }
    return TCL_OK;
}

static int
keyImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaKeySpace *ks = (SchemaKeySpace *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    if (!ks->active) return 1;
    h = Tcl_CreateHashEntry (&ks->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 1);
        return 1;
    }
    if (Tcl_GetHashValue (h) == 0) {
        Tcl_SetHashValue (h, 1);
        ks->unknownIDrefs--;
        return 1;
    } else {
        /* Duplicate ID value */
        return 0;
    }
}

static int
keyTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashEntry *h;
    int hnew;
    SchemaKeySpace *ks;

    CHECK_TI
    checkNrArgs (2,2,"key_space");
    ADD_CONSTRAINT (sdata, sc)
    h = Tcl_CreateHashEntry (&sdata->keySpaces, Tcl_GetString (objv[1]), &hnew);
    if (hnew) {
        ks = TMALLOC (SchemaKeySpace);
        ks->active = 0;
        ks->unknownIDrefs = 0;
        Tcl_SetHashValue (h, ks);
    } else {
        ks = Tcl_GetHashValue (h);
    }
    sc->constraint = keyImpl;
    sc->constraintData = (void *) ks;
    return TCL_OK;
}

static int
keyrefImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaKeySpace *ks = (SchemaKeySpace *) constraintData;
    int hnew;
    Tcl_HashEntry *h;

    if (!ks->active) return 1;
    h = Tcl_CreateHashEntry (&ks->ids, text, &hnew);
    if (hnew) {
        Tcl_SetHashValue (h, 0);
        ks->unknownIDrefs++;
    }
    return 1;
}

static int
keyrefTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashEntry *h;
    int hnew;
    SchemaKeySpace *ks;

    CHECK_TI
    checkNrArgs (2,2,"key_space");
    ADD_CONSTRAINT (sdata, sc)
    h = Tcl_CreateHashEntry (&sdata->keySpaces, Tcl_GetString (objv[1]),
                             &hnew);
    if (hnew) {
        ks = TMALLOC (SchemaKeySpace);
        Tcl_InitHashTable (&ks->ids, TCL_STRING_KEYS);
        ks->unknownIDrefs = 0;
        Tcl_SetHashValue (h, ks);
    } else {
        ks = Tcl_GetHashValue (h);
    }
    sc->constraint = keyrefImpl;
    sc->constraintData = (void *)ks;
    return TCL_OK;
}

static int
base64Impl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int chars = 0, equals = 0;
    
    while (*text != '\0') {
        if (SPACE(*text)) {
            text++;
            continue;
        }
        if (   (*text >= 'A' && *text <= 'Z')
            || (*text >= 'a' && *text <= 'z')
            || (*text >= '0' && *text <= '9')
            || (*text = '+')
            || (*text = '/')) {
            chars++;
            text++;
            continue;
        }
        if (equals < 2 && *text == '=') {
            equals++;
            text++;
            continue;
        }
        break;
    }
    if (*text) {
        return 0;
    }
    if ((chars + equals) % 4 != 0) {
        return 0;
    }
    return 1;
}

static int
base64TCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = base64Impl;
    return TCL_OK;
}

static int
nameImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    return domIsNAME (text);
}

static int
nameTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = nameImpl;
    return TCL_OK;
}

static int
ncnameImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    return domIsNCNAME (text);
}

static int
ncnameTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = ncnameImpl;
    return TCL_OK;
}

static int
qnameImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    return domIsQNAME (text);
}

static int
qnameTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = qnameImpl;
    return TCL_OK;
}

static int
hexBinaryImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int count = 0;

    if (*text == 0) return 0;
    while (*text) {
        if ((*text >= '0' && *text <= '9')
            || (*text >= 'A' && *text <= 'F')
            || (*text >= 'a' && *text <= 'f')) {
            text++;
            count++;
        } else return 0;
    }
    if (count % 2 == 0) return 1;
    return 0;
}

static int
hexBinaryTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = hexBinaryImpl;
    return TCL_OK;
}

static int
unsignedIntTypesImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *c;
    int count = 0;
    int nrDigits[] = {3, 5, 10, 20};
    char *max[] = {
        "255",
        "65535",
        "4294967295",
        "18446744073709551615"
    };
    
    if (*text == '+') text++;
    if (*text == 0) return 0;
    if (*text == '0') {
        text++;
        while (*text == '0') text++;
        if (*text == 0) return 1;
    }
    c = text;
    while (*text) {
        if (*text >= '0' && *text <= '9') {
            text++;
            count++;
        } else return 0;
    }
    if (count < nrDigits[(intptr_t) constraintData]) return 1;
    if (count == nrDigits[(intptr_t) constraintData]) {
        if (strcmp (max[(intptr_t) constraintData], c) >= 0) {
            return 1;
        }
    }
    return 0;
}

static int
unsignedIntTypesTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = unsignedIntTypesImpl;
    sc->constraintData = clientData;
    return TCL_OK;
}

static int
intTypesImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *c;
    int count = 0;
    int nrDigits[] = {3, 5, 10, 20};
    char *compare;
    char *max[] = {
        "127",
        "32767",
        "2147483647",
        "9223372036854775807"
    };
    char *min[] = {
        "128",
        "32768",
        "2147483648",
        "9223372036854775808"
    };

    if (*text == '-') {
        compare = min[(intptr_t) constraintData];
    } else {
        compare = max[(intptr_t) constraintData];
    }
    if (*text == '+' || *text == '-') text++;
    if (*text == 0) return 0;
    if (*text == '0') {
        text++;
        while (*text == '0') text++;
        if (*text == 0) return 1;
    }
    c = text;
    while (*text) {
        if (*text >= '0' && *text <= '9') {
            text++;
            count++;
        } else return 0;
    }
    if (count < nrDigits[(intptr_t) constraintData]) return 1;
    if (count == nrDigits[(intptr_t) constraintData]) {
        if (strcmp (compare, c) >= 0) return 1;
    }
    return 0;
}

static int
intTypesTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = intTypesImpl;
    sc->constraintData = clientData;
    return TCL_OK;
}

static void
setvarImplFree (
    void *constraintData
    )
{
    FREE (constraintData);
}

static int
setvarImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    char *varName = (char *)constraintData;

    if (!Tcl_SetVar (interp, varName, text, TCL_LEAVE_ERR_MSG)) {
        return 0;
    }
    return 1;
}

static int
setvarTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (2,2,"<tcl variable name>");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = setvarImpl;
    sc->freeData = setvarImplFree;
    sc->constraintData = tdomstrdup (Tcl_GetString (objv[1]));
    return TCL_OK;
}

typedef struct
{
    SchemaCP *cp;
    SchemaData *sdata;
} WhitespaceTCData;

static void
whitespaceImplFree (
    void *constraintData
    )
{
    WhitespaceTCData *wsdata = (WhitespaceTCData *) constraintData;

    FREE (wsdata);
}

static int
whitespaceImplReplace (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    WhitespaceTCData *wsdata = (WhitespaceTCData *) constraintData;
    char *p, *c, *alloced;
    SchemaData *sdata;

    sdata = wsdata->sdata;
    p = text;
    c = sdata->wsbuf;
    alloced = sdata->wsbuf + sdata->wsbufLen;
    while (*p) {
        if (*p == '\t' || *p == '\n' || *p == '\r') {
            *c = ' ';
        } else {
            *c = *p;
        }
        c++;
        if (c == alloced) {
            sdata->wsbuf = REALLOC (sdata->wsbuf, 2 * sdata->wsbufLen);
            c = sdata->wsbuf + sdata->wsbufLen;
            sdata->wsbufLen *= 2;
            alloced = sdata->wsbuf + sdata->wsbufLen;
        }
        p++;
    }
    *c = '\0';
    return checkText (interp, wsdata->cp, sdata->wsbuf);
}

static int
whitespaceImplCollapse (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    WhitespaceTCData *wsdata = (WhitespaceTCData *) constraintData;
    char *p, *c, *alloced;
    SchemaData *sdata;

    sdata = wsdata->sdata;
    p = text;
    c = sdata->wsbuf;
    alloced = sdata->wsbuf + sdata->wsbufLen;
    while (SPACE(*p)) p++;
    while (*p) {
        if (SPACE (*p)) {
            *c = ' ';
            c++;
            if (c == alloced) {
                sdata->wsbuf = REALLOC (sdata->wsbuf, 2 * sdata->wsbufLen);
                c = sdata->wsbuf + sdata->wsbufLen;
                sdata->wsbufLen *= 2;
                alloced = sdata->wsbuf + sdata->wsbufLen;
            }
            p++;
            while (SPACE (*p)) p++;
            if (!*p) c--;
        } else {
            *c = *p;
            c++;
            if (c == alloced) {
                sdata->wsbuf = REALLOC (sdata->wsbuf, 2 * sdata->wsbufLen);
                c = sdata->wsbuf + sdata->wsbufLen;
                sdata->wsbufLen *= 2;
                alloced = sdata->wsbuf + sdata->wsbufLen;
            }
            p++;
        }
    }
    *c = '\0';
    return checkText (interp, wsdata->cp, wsdata->sdata->wsbuf);
}

static int
whitespaceTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int type;
    WhitespaceTCData *wsdata;

    static const char *types[] = {
        "preserve", "replace", "collapse", NULL
    };
    enum typeSyms {
        t_preserve, t_replace, t_collapse
    };
        
    CHECK_TI
    checkNrArgs (3,3,"(\"preserve\"|\"replace\"|\"collapse\") "
                 "<text constraint script>");
    if (Tcl_GetIndexFromObj (interp, objv[1], types, "type", 0, &type)
        != TCL_OK) {
        return TCL_ERROR;
    }
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    if (evalConstraints (interp, sdata, cp, objv[2]) != TCL_OK) {
        return TCL_ERROR;
    }
    if (type == t_preserve) {
        ADD_CONSTRAINT (sdata, sc)
        sc->constraint = checkText;
        sc->constraintData = (void *)cp;
        return TCL_OK;
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->freeData = whitespaceImplFree;
    if (sdata->wsbufLen == 0) {
        sdata->wsbuf = (char *) MALLOC (WHITESPACETC_BUFFER_LEN_INIT);
        sdata->wsbufLen = WHITESPACETC_BUFFER_LEN_INIT;
    }
    wsdata = TMALLOC (WhitespaceTCData);
    wsdata->sdata = sdata;
    wsdata->cp = cp;
    sc->constraintData = (void *)wsdata;
    if (type == t_replace) {
        sc->constraint = whitespaceImplReplace;
    } else {
        sc->constraint = whitespaceImplCollapse;
    }
    return TCL_OK;
}

static int
notImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    SchemaCP *cp = (SchemaCP *) constraintData;
    SchemaConstraint *sc;
    unsigned int i;

    /* Look also at checkText and oneOfImpl */
    for (i = 0; i < cp->nc; i++) {
        sc = (SchemaConstraint *) cp->content[i];
        if ((sc->constraint) (interp, sc->constraintData, text)) {
            return 0;
        }
    }
    return 1;
}

static int
notTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaCP *cp;
    SchemaConstraint *sc;
    int rc;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <text constraint script>");
    
    cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
    cp->type = SCHEMA_CTYPE_TEXT;
    REMEMBER_PATTERN (cp)
    rc = evalConstraints (interp, sdata, cp, objv[1]);
    if (rc == TCL_OK) {
        ADD_CONSTRAINT (sdata, sc)
        sc->constraint = notImpl;
        sc->constraintData = (void *)cp;
        return TCL_OK;
    }
    return TCL_ERROR;
}

static int
durationImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    /* PnYnMnDTnHnMnS */
    int p, n, seen = 0, seenT = 0;
    char des[9] = " YMDTHMS";
    
    if (*text == '-') {
        /* Negative duration is allowed */
        text++;
    }
    if (*text != 'P') return 0;
    text++;
    p = 0;
    while (*text) {
        n = 0;
        while (*text >= '0' && *text <= '9') {
            n++;
            text++;
        }
        if (!*text) return 0;
        if (*text == '.') {
            if (p < 4 || !n) return 0;
            text++;
            if (!*text) return 0;
            /* Ensure at least one digit after . */
            if (*text < '0' || *text > '9') return 0;
            text++;
            while (*text >= '0' && *text <= '9') text++;
            if (*text != 'S') return 0;
            text++;
            if (*text) return 0;
            return 1;
        }
        for (; p < 8; p++) {
            if (*text == des[p]) break;
        }
        if (p ==  4) {
            if (n) return 0;
            seenT = 1;
            text++;
            if (!*text) return 0;
            continue;
        } else {
            if (!n) return 0;
            seen = 1;
        }
        if (p > 4 && !seenT) return 0;
        if (p == 8 || !seen) return 0;
        text++;
    }
    if (!p) return 0;
    return 1;
}

static int
durationTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = durationImpl;
    return TCL_OK;
}

static int
lengthImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    unsigned int len = 0, length = PTR2UINT(constraintData);
    int clen;
    while (*text != '\0') {
        clen = UTF8_CHAR_LEN (*text);
        if (!clen) {
            SetResult ("Invalid UTF-8 character");
            return 0;
        }
        len++;
        if (len > length) return 0;
        text += clen;
    }
    if (len == length) return 1;
    return 0;
}

static int
lengthTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    int len;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <length as integer>");
    if (Tcl_GetIntFromObj (interp, objv[1], &len) != TCL_OK) {
        SetResult ("Expected: <length as integer>");
        return TCL_ERROR;
    }
    if (len < 0) {
        SetResult ("The length must be at least 0");
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = lengthImpl;
    sc->constraintData = UINT2PTR(len);
    return TCL_OK;
}

static int
typeImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    return checkText (interp, constraintData, text);
}

static int
typeTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;
    Tcl_HashEntry *h;
    int hnew;
    SchemaCP *pattern = NULL;

    CHECK_TI
    checkNrArgs (2,2,"Expected: <text type name>");
    h = Tcl_CreateHashEntry (&sdata->textDef, Tcl_GetString (objv[1]), &hnew);
    if (hnew) {
        pattern = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
        pattern->type = SCHEMA_CTYPE_TEXT;
        REMEMBER_PATTERN (pattern)
        pattern->flags |= FORWARD_PATTERN_DEF;
        sdata->forwardPatternDefs++;
        Tcl_SetHashValue (h, pattern);
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = typeImpl;
    sc->constraintData = Tcl_GetHashValue (h);
    return TCL_OK;
}

static int
dateObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    checkNrArgs (2,2,"<text>");
    Tcl_SetObjResult (interp,
                      Tcl_NewBooleanObj (
                          isodateImpl (interp, NULL,
                                       Tcl_GetString (objv[1]))));
    return TCL_OK;
}

static int
dateTimeObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    checkNrArgs (2,2,"<text>");
    Tcl_SetObjResult (interp,
                      Tcl_NewBooleanObj (
                          isodateImpl (interp, (void *) 1,
                                       Tcl_GetString (objv[1]))));
    return TCL_OK;
}

static int
timeObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    checkNrArgs (2,2,"<text>");
    Tcl_SetObjResult (interp,
                      Tcl_NewBooleanObj (
                          isodateImpl (interp, (void *) 2,
                                       Tcl_GetString (objv[1]))));
    return TCL_OK;
}

static int
durationObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    checkNrArgs (2,2,"<text>");
    Tcl_SetObjResult (interp,
                      Tcl_NewBooleanObj (
                          durationImpl (interp, NULL,
                                        Tcl_GetString (objv[1]))));
    return TCL_OK;
}

void
tDOM_SchemaInit (
    Tcl_Interp *interp
    )
{
    Tcl_CreateObjCommand (interp, "tdom::schema", tDOM_SchemaObjCmd,
                          NULL, NULL);

    /* Inline definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::defelement",
                          tDOM_schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::defelementtype",
                          tDOM_schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::defpattern",
                          tDOM_schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::deftexttype",
                          tDOM_schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::start",
                          tDOM_schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::prefixns",
                          tDOM_schemaInstanceCmd, NULL, NULL);

    /* The "any" definition command. */
    Tcl_CreateObjCommand (interp, "tdom::schema::any",
                          AnyPatternObjCmd, NULL, NULL);

    /* The named pattern commands "element" and "ref". */
    Tcl_CreateObjCommand (interp, "tdom::schema::element",
                          ElementPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::ref",
                          RefPatternObjCmd, NULL, NULL);

    /* The anonymous pattern commands "choise", "mixed", "interleave"
     * and "group". */
    Tcl_CreateObjCommand (interp, "tdom::schema::choice",
                          AnonPatternObjCmd, (ClientData) 0, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::mixed",
                          AnonPatternObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::interleave",
                          AnonPatternObjCmd, (ClientData) 2, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::group",
                          AnonPatternObjCmd, (ClientData) 3, NULL);

    /* The "attribute", "nsattribute", "namespace" and "text"
     * definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::nsattribute",
                          AttributePatternObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text",
                          TextPatternObjCmd, NULL, NULL);

    /* The 'virtual' "tcl" and the "self" definition command */
    Tcl_CreateObjCommand (interp, "tdom::schema::tcl",
                          VirtualPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::self",
                          SelfObjCmd, NULL, NULL);

    /* XPath constraints for DOM validation */
    Tcl_CreateObjCommand (interp,"tdom::schema::domunique",
                          domuniquePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::domxpathboolean",
                          domxpathbooleanPatternObjCmd, NULL, NULL);

    /* Local key constraints */
    Tcl_CreateObjCommand (interp, "tdom::schema::keyspace",
                          keyspacePatternObjCmd, NULL, NULL);
    
    /* The associate command */
    Tcl_CreateObjCommand (interp,"tdom::schema::associate",
                          associatePatternObjCmd, NULL, NULL);
    
    /* The text constraint commands */
    Tcl_CreateObjCommand (interp,"tdom::schema::text::integer",
                          integerTCObjCmd, (ClientData) 0, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::negativeInteger",
                          integerTCObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::nonNegativeInteger",
                          integerTCObjCmd, (ClientData) 2, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::nonPositiveInteger",
                          integerTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::positiveInteger",
                          integerTCObjCmd, (ClientData) 4, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::tcl",
                          tclTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::fixed",
                          fixedTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::enumeration",
                          enumerationTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::match",
                          matchTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::regexp",
                          regexpTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::nmtoken",
                          nmtokenTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::nmtokens",
                          nmtokensTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::number",
                          numberTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::boolean",
                          booleanTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::date",
                          dateTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::dateTime",
                          dateTimeTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::time",
                          timeTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::duration",
                          durationTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::maxLength",
                          maxLengthTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::minLength",
                          minLengthTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::oneOf",
                          oneOfTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::allOf",
                          allOfTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::strip",
                          stripTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::split",
                          splitTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::id",
                          idTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::idref",
                          idrefTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::base64",
                          base64TCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::key",
                          keyTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::keyref",
                          keyrefTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::name",
                          nameTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::ncname",
                          ncnameTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::qname",
                          qnameTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::hexBinary",
                          hexBinaryTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::unsignedByte",
                          unsignedIntTypesTCObjCmd, (ClientData) 0, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::unsignedShort",
                          unsignedIntTypesTCObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::unsignedInt",
                          unsignedIntTypesTCObjCmd, (ClientData) 2, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::unsignedLong",
                          unsignedIntTypesTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::byte",
                          intTypesTCObjCmd, (ClientData) 0, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::short",
                          intTypesTCObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::int",
                          intTypesTCObjCmd, (ClientData) 2, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::long",
                          intTypesTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::setvar",
                          setvarTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::whitespace",
                          whitespaceTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::not",
                          notTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::length",
                          lengthTCObjCmd, (ClientData) 3, NULL);
    Tcl_CreateObjCommand (interp,"tdom::schema::text::type",
                          typeTCObjCmd, NULL, NULL);

    /* Exposed text type commands */
    Tcl_CreateObjCommand (interp,"tdom::type::date",
                          dateObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::type::dateTime",
                          dateTimeObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::type::time",
                          timeObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp,"tdom::type::duration",
                          durationObjCmd, NULL, NULL);

}

#else   /* #ifndef TDOM_NO_SCHEMA */

SchemaData *
tdomGetSchemadata (void) 
{
    return 0;
}


#endif  /* #ifndef TDOM_NO_SCHEMA */
