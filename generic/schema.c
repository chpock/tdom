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
|   2018-2019
|
\---------------------------------------------------------------------------*/

#ifndef TDOM_NO_SCHEMA

#include <tdom.h>
#include <tcldom.h>
#include <domxpath.h>
#include <schema.h>

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
|   Initial buffer sizes
|
\---------------------------------------------------------------------------*/
#ifndef CONTENT_ARRAY_SIZE_INIT
#  define CONTENT_ARRAY_SIZE_INIT 20
#endif
#ifndef ANON_PATTERN_ARRAY_SIZE_INIT
#  define ANON_PATTERN_ARRAY_SIZE_INIT 256
#endif
#ifndef QUANTS_ARRAY_SIZE_INIT
#  define QUANTS_ARRAY_SIZE_INIT 8
#endif
#ifndef STACK_SIZE_INIT
#  define STACK_SIZE_INIT 16
#endif
#ifndef STACK_LIST_SIZE_INIT
#  define STACK_LIST_SIZE_INIT 64
#endif
#ifndef URI_BUFFER_LEN_INIT
#  define URI_BUFFER_LEN_INIT 128
#endif
#ifndef ATTR_ARRAY_INIT
#  define ATTR_ARRAY_INIT 4
#endif

/*----------------------------------------------------------------------------
|   Local typedefs
|
\---------------------------------------------------------------------------*/

typedef struct
{
    SchemaData *sdata;
    Tcl_Interp    *interp;
    XML_Parser     parser;
    Tcl_DString   *cdata;
    int            onlyWhiteSpace;
    char          *uri;
    int            maxUriLen;
} ValidateMethodData;

/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define TMALLOC(t) (t*)MALLOC(sizeof(t))

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)
#define SetResult3(str1,str2,str3) Tcl_ResetResult(interp);     \
                     Tcl_AppendResult(interp, (str1), (str2), (str3), NULL)
#define SetIntResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetIntObj(Tcl_GetObjResult(interp), (i))
#define SetBooleanResult(i) Tcl_ResetResult(interp); \
                     Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (i))

#define SPACE(c) ((c) == ' ' || (c) == '\n' || (c )== '\t' || (c) == '\r')
    
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
    "TEXT"
};
static char *Schema_Quant_Type2str[] = {
    "ONE",
    "OPT",
    "REP",
    "PLUS",
    "NM"
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
            SetResult ("This recursive call is not allowed"); \
            return TCL_ERROR;                                           \
        }                                                               \
    }
      
#define CHECK_EVAL                                                      \
    if (sdata->currentEvals) {                                          \
        SetResult ("Method not allowed in nested schema define script"); \
        return TCL_ERROR;                                               \
    }
    
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


#define getContext(cp, ac, hm)        \
    cp = se->pattern;                 \
    ac = se->activeChild;             \
    hm = se->hasMatched;


#define updateStack(se,cp,ac)                     \
    if (maxOne (cp->quants[ac])) {                \
        se->activeChild = ac + 1;                 \
        se->hasMatched = 0;                       \
    } else {                                      \
        se->activeChild = ac;                     \
        se->hasMatched = 1;                       \
    }

#define serializeElementName(rObj, cp)                  \
    rObj = Tcl_NewObj();                                \
    if (cp->namespace) {                                \
        Tcl_SetStringObj (rObj, cp->namespace, -1);     \
        Tcl_AppendToObj (rObj, ":", 1);                 \
    }                                                   \
    Tcl_AppendToObj (rObj, cp->name, -1);

#define S(str)  str, sizeof (str) -1

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
    case SCHEMA_CTYPE_PATTERN:
        pattern->namespace = (char *)namespace;
        pattern->name = name;
        /* Fall thru. */
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
    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_ANY:
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
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
        fprintf (stderr, "\tName: '%s' Namespace: '%s'\n",
                 pattern->name,pattern->namespace);
        if (pattern->flags & FORWARD_PATTERN_DEF) {
            fprintf (stderr, "\tAnonymously defined NAME\n");
        }
        if (pattern->flags & PLACEHOLDER_PATTERN_DEF) {
            fprintf (stderr, "\tAs placeholder defined NAME\n");
        }
        if (pattern->flags & LOCAL_DEFINED_ELEMENT) {
            fprintf (stderr, "\tAs placeholder defined NAME\n");
        }
        /* Fall thru. */
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_INTERLEAVE:
        fprintf (stderr, "\t%d childs\n", pattern->nc);
        break;
    case SCHEMA_CTYPE_ANY:
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
    int i;
    SchemaConstraint *sc;

    switch (pattern->type) {
    case SCHEMA_CTYPE_ANY:
        /* do nothing */
        break;
    case SCHEMA_CTYPE_VIRTUAL:
        for (i = 0; i < pattern->nc - 1; i++) {
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
        FREE (pattern->content);
        FREE (pattern->quants);
        if (pattern->attrs) {
            for (i = 0; i < pattern->numAttr; i++) {
                FREE (pattern->attrs[i]);
            }
            FREE (pattern->attrs);
        }
        freedomKeyConstraints (pattern->domKeys);
        break;
    }
    FREE (pattern);
}

static SchemaData*
initSchemaData (
    Tcl_Obj *cmdNameObj)
{
    SchemaData *sdata;
    int hnew, len;
    char *name;

    sdata = TMALLOC (SchemaData);
    memset (sdata, 0, sizeof(SchemaData));
    name = Tcl_GetStringFromObj (cmdNameObj, &len);
    sdata->self = Tcl_NewStringObj (name, len);
    Tcl_IncrRefCount (sdata->self);
    Tcl_InitHashTable (&sdata->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->prefix, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->attrNames, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->textDef, TCL_STRING_KEYS);
    sdata->emptyNamespace = Tcl_CreateHashEntry (
        &sdata->namespace, "", &hnew);
    sdata->patternList = (SchemaCP **) MALLOC (
        sizeof (SchemaCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->patternListSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    sdata->quants = (SchemaQuant *) MALLOC (
        sizeof (SchemaQuant) * QUANTS_ARRAY_SIZE_INIT);
    sdata->quantsSize = QUANTS_ARRAY_SIZE_INIT;
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

    /* Protect the clientData to be freed inside (even nested)
     * Tcl_Eval*() calls to avoid invalid mem access and postpone the
     * cleanup until the Tcl_Eval*() calls are finished (done in
     * schemaInstanceCmd(). */
    if (sdata->currentEvals) {
        sdata->cleanupAfterEval = 1;
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
    Tcl_DeleteHashTable (&sdata->prefix);
    Tcl_DeleteHashTable (&sdata->pattern);
    Tcl_DeleteHashTable (&sdata->attrNames);
    Tcl_DeleteHashTable (&sdata->textDef);
    for (i = 0; i < sdata->numPatternList; i++) {
        freeSchemaCP (sdata->patternList[i]);
    }
    FREE (sdata->patternList);
    if (sdata->numQuants) {
        FREE (sdata->quants);
    }
    FREE (sdata->quants);
    while (sdata->stack) {
        down = sdata->stack->down;
        FREE (sdata->stack);
        sdata->stack = down;
    }
    while (sdata->stackPool) {
        down = sdata->stackPool->down;
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
    FREE (sdata);
}

static void
cleanupLastPattern (
    SchemaData *sdata,
    unsigned int from
    )
{
    unsigned int i;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;

    SchemaCP *this, *previous, *current;

    for (i = from; i < sdata->numPatternList; i++) {
        this = sdata->patternList[i];
        hashTable = NULL;
        if (this->type == SCHEMA_CTYPE_NAME) {
            hashTable = &sdata->element;
        }
        if (this->type == SCHEMA_CTYPE_PATTERN) {
            hashTable = &sdata->pattern;
        }
        if (this->name && hashTable) {
            if (this->flags & FORWARD_PATTERN_DEF) {
                sdata->forwardPatternDefs--;
            }
            entryPtr = Tcl_FindHashEntry (hashTable, this->name);
            previous = NULL;
            current = Tcl_GetHashValue (entryPtr);
            while (current != NULL && current != this) {
                previous = current;
                current = current->next;
            }
            if (previous) {
                if (current->next) {
                    previous->next = current->next;
                } else {
                    previous->next = NULL;
                }
            } else {
                if (current) {
                    Tcl_SetHashValue (entryPtr, current->next);
                } else {
                    Tcl_DeleteHashEntry (entryPtr);
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
        int i;
        int newChilds = (n >= m) ? n : m;
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
            sdata->cp->quants[sdata->cp->nc+i] = SCHEMA_CQUANT_OPT;
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

static void
pushToStack (
    SchemaData *sdata,
    SchemaCP *pattern
    )
{
    SchemaValidationStack *stackElm, *se;

    DBG(fprintf(stderr, "push to Stack:\n");serializeCP(pattern));
    if (sdata->stackPool) {
        stackElm = sdata->stackPool;
        sdata->stackPool = stackElm->down;
    } else {
        stackElm = TMALLOC (SchemaValidationStack);
    }
    memset (stackElm, 0, sizeof (SchemaValidationStack));
    se = sdata->stack;
    stackElm->down = se;
    stackElm->pattern = pattern;
    if (pattern->type == SCHEMA_CTYPE_INTERLEAVE) {
        stackElm->interleaveState = MALLOC (sizeof (int) * pattern->nc);
        memset (stackElm->interleaveState, 0, sizeof (int) * pattern->nc);
    }
    sdata->stack = stackElm;
}

static void
popStack (
    SchemaData *sdata
    )
{
    SchemaValidationStack *se;
    DBG(fprintf(stderr, "pop from Stack:\n");serializeCP(sdata->stack->pattern));
    if (sdata->stack->interleaveState) {
        FREE (sdata->stack->interleaveState);
        sdata->stack->interleaveState = NULL;
    }
    se = sdata->stack->down;
    sdata->stack->down = sdata->stackPool;
    sdata->stackPool = sdata->stack;
    sdata->stack = se;
}

static int 
recover (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *errType,
    int len
    )
{
    Tcl_Obj *cmdPtr;
    int rc;

    if (!sdata->reportCmd) return 0;
    cmdPtr = Tcl_DuplicateObj (sdata->reportCmd);
    Tcl_IncrRefCount(cmdPtr);
    Tcl_ListObjAppendElement (interp, cmdPtr,
                              sdata->self);
    Tcl_ListObjAppendElement (
        interp, cmdPtr,
        Tcl_NewStringObj (errType, len)
        );
    sdata->currentEvals++;
    rc = Tcl_EvalObjEx (interp, cmdPtr,
                        TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    sdata->currentEvals--;
    Tcl_DecrRefCount (cmdPtr);
    if (rc != TCL_OK) {
        sdata->evalError = 1;
        return 0;
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
    int i;
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

static int
evalVirtual (
    Tcl_Interp *interp,
    SchemaData *sdata,
    SchemaCP *cp
    )
{
    int rc;

    cp->content[cp->nc-1] = (SchemaCP *) sdata->self;
    rc = Tcl_EvalObjv (interp, cp->nc, (Tcl_Obj **) cp->content,
                       TCL_EVAL_GLOBAL);
    if (rc != TCL_OK) {
        sdata->evalError = 1;
        return 0;
    }
    return 1;
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
    int hm, ac, i, mayskip, rc;
    int isName = 0;
    SchemaValidationStack *se;

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
                        if (recover (interp, sdata, S("MISSING_TEXT"))) {
                            mayskip = 1;
                            break;
                        }                        
                        return 0;
                    }
                }
                break;

            case SCHEMA_CTYPE_ANY:
                updateStack (se, cp, ac);
                sdata->skipDeep = 1;
                return 1;

            case SCHEMA_CTYPE_NAME:
                DBG(fprintf (stderr, "name: %s ns: %s candidate name: %s "
                             "candidate ns: %s\n", name, namespace,
                             candidate->name, candidate->namespace));
                if (candidate->name == name
                    && candidate->namespace == namespace) {
                    updateStack (se, cp, ac);
                    pushToStack (sdata, candidate);
                    return 1;
                }
                break;

            case SCHEMA_CTYPE_CHOICE:
                for (i = 0; i < candidate->nc; i++) {
                    icp = candidate->content[i];
                    switch (icp->type) {
                    case SCHEMA_CTYPE_TEXT:
                        break;

                    case SCHEMA_CTYPE_ANY:
                        sdata->skipDeep = 1;
                        updateStack (se, cp, ac);
                        return 1;

                    case SCHEMA_CTYPE_NAME:
                        if (icp->name == name
                            && icp->namespace == namespace) {
                            pushToStack (sdata, icp);
                            updateStack (se, cp, ac);
                            return 1;
                        }
                        break;

                    case SCHEMA_CTYPE_CHOICE:
                        Tcl_Panic ("MIXED or CHOICE child of MIXED or CHOICE");

                    case SCHEMA_CTYPE_INTERLEAVE:
                    case SCHEMA_CTYPE_PATTERN:
                        pushToStack (sdata, icp);
                        rc = matchElementStart (interp, sdata, name, namespace);
                        if (rc == 1) {
                            updateStack (se, cp, ac);
                            return 1;
                        }
                        popStack (sdata);
                        if (rc == -1) mayskip = 1;
                        break;

                    case SCHEMA_CTYPE_VIRTUAL:
                        Tcl_Panic ("Virtual constrain in MIXED or CHOICE");
                        
                    }
                    if (!mayskip && mayMiss (candidate->quants[i]))
                        mayskip = 1;
                }
                break;

            case SCHEMA_CTYPE_VIRTUAL:
                if (evalVirtual (interp, sdata, candidate)) {
                    /* Virtual contraints are always quant ONE, so
                     * that the virtual constraints are called while
                     * looking if an element can end. Therefor we use
                     * here the already present mayskip mechanism to
                     * try further, after calling the tcl script. */
                    mayskip = 1;
                    break;
                }
                else return 0;

            case SCHEMA_CTYPE_INTERLEAVE:
            case SCHEMA_CTYPE_PATTERN:
                pushToStack (sdata, candidate);
                rc = matchElementStart (interp, sdata, name, namespace);
                if (rc == 1) {
                    updateStack (se, cp, ac);
                    return 1;
                }
                popStack (sdata);

                break;
            }
            if (!mayskip && mustMatch (cp->quants[ac], hm)) {
                if (recover (interp, sdata, S("MISSING_CP"))) {
                    /* Skip the just opened element tag and the following
                     * content of the current. */
                    sdata->skipDeep = 2;
                    return 1;
                }
                return 0;
            }
            ac++;
            hm = 0;
        }
        if (isName) {
            if (recover (interp, sdata, "UNEXPECTED_ELEMENT", 15)) {
                /* Skip the just opened element tag and the following
                 * content of the current. */
                sdata->skipDeep = 2;
                return 1;
            }
            return 0;
        }
        return -1;

    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");

    case SCHEMA_CTYPE_INTERLEAVE:
        mayskip = 1;
        for (i = 0; i < cp->nc; i++) {
            if (se->interleaveState[i]) {
                if (maxOne (cp->quants[i])) continue;
            }
            icp = cp->content[i];
            switch (icp->type) {
            case SCHEMA_CTYPE_TEXT:
                if (icp->nc) {
                    if (!checkText (interp, icp, "")) {
                        mayskip = 0;
                    }
                }
                break;

            case SCHEMA_CTYPE_ANY:
                sdata->skipDeep = 1;
                if (mayskip && minOne (cp->quants[i])) mayskip = 0;
                se->hasMatched = 1;
                se->interleaveState[i] = 1;
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

            case SCHEMA_CTYPE_INTERLEAVE:
            case SCHEMA_CTYPE_PATTERN:
                pushToStack (sdata, icp);
                rc = matchElementStart (interp, sdata, name, namespace);
                if (rc == 1) {
                    se->hasMatched = 1;
                    se->interleaveState[i] = 1;
                    return 1;
                }
                popStack (sdata);
                if (mayskip && rc != -1) mayskip = 0;
                break;

            case SCHEMA_CTYPE_VIRTUAL:
                Tcl_Panic ("Virtual constraint child of INTERLEAVE");
                break;
            }

        }
                
        if (mayskip) break;
        if (recover (interp, sdata, S("UNCOMPLET_CP"))) {
            sdata->skipDeep = 2;
            return 1;
        }
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
    h = Tcl_FindHashEntry (&sdata->prefix, ns);
    if (h) {
        return Tcl_GetHashValue (h);
    }
    h = Tcl_CreateHashEntry (&sdata->namespace, ns, &hnew);
    if (h != sdata->emptyNamespace) {
        return Tcl_GetHashKey (&sdata->namespace, h);
    }
    return NULL;
}

int
probeElement (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *name,
    void *namespace
    )
{
    Tcl_HashEntry *entryPtr;
    void *namespacePtr, *namePtr;
    SchemaCP *pattern;
    int rc;

    if (sdata->skipDeep) {
        sdata->skipDeep++;
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }

    DBG(
        fprintf (stderr, "probeElement: look if '%s' in ns '%s' match\n",
                 name, (char *)namespace);
        );

    namespacePtr = getNamespacePtr (sdata, namespace);
    entryPtr = Tcl_FindHashEntry (&sdata->element, name);
    if (entryPtr) {
        namePtr = Tcl_GetHashKey (&sdata->element, entryPtr);
    } else {
        namePtr = NULL;
    }

    if (sdata->validationState == VALIDATION_READY) {
        /* The root of the tree to check. */
        if (sdata->start) {
            if (strcmp (name, sdata->start) != 0) {
                SetResult ("Root element doesn't match");
                return TCL_ERROR;
            }
            if (namespace) {
                if (!sdata->startNamespace||
                    strcmp (namespace, sdata->startNamespace) != 0) {
                    SetResult ("Root element namespace doesn't match");
                    return TCL_ERROR;
                }
            } else {
                if (sdata->startNamespace) {
                    SetResult ("Root element namespace doesn't match");
                    return TCL_ERROR;
                }
            }
        }
    }
    if (entryPtr) {
        pattern = (SchemaCP *) Tcl_GetHashValue (entryPtr);
        while (pattern) {
            if (pattern->namespace == namespacePtr) {
                break;
            }
            pattern = pattern->next;
        }
    } else {
        pattern = NULL;
    }

    if (sdata->stack) {
        SchemaValidationStack *se;
        se = sdata->stack;
        if (se->pattern->type == SCHEMA_CTYPE_NAME
            && se->activeChild >= se->pattern->nc) {
            if (recover (interp, sdata, S("UNEXPECTED_ELEMENT"))) {
                sdata->skipDeep = 1;
                return TCL_OK;
            }
            SetResult ("Unexpected child element \"");
            if (namespacePtr) {
                Tcl_AppendResult (interp, namespacePtr, ":", NULL);
            }
            Tcl_AppendResult (interp, name, "\" for element \"", NULL);
            if (se->pattern->namespace) {
                Tcl_AppendResult (interp, namespace, ":", NULL);
            }
            Tcl_AppendResult (interp, name, "\"", NULL);
            return TCL_ERROR;
        }
    } else {
        if (!pattern) {
            if (recover (interp, sdata, S("UNKNOWN_ROOT_ELEMENT"))) {
                sdata->skipDeep = 1;
                return TCL_OK;
            }
            SetResult ("Unknown element");
            return TCL_ERROR;
        }
        pushToStack (sdata, pattern);
        sdata->validationState = VALIDATION_STARTED;
        return TCL_OK;
    }

    /* The normal case: we're inside the tree */
    rc = matchElementStart (interp, sdata, (char *) namePtr, namespacePtr);
    while (rc == -1) {
        popStack (sdata);
        rc = matchElementStart (interp, sdata, (char *) namePtr, namespacePtr);
    };
    if (rc) {
        DBG(
            fprintf (stderr, "probeElement: element '%s' match\n", name);
            serializeStack (sdata);
            fprintf (stderr, "\n");
            );
        return TCL_OK;
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

int probeAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char **attr
    )
{
    char   **atPtr, *ln, *namespace;
    int i, j, found, nsatt, reqAttr = 0;
    SchemaCP *cp;

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
        for (i = 0; i < cp->numAttr; i++) {
            if (nsatt) {
                if (!cp->attrs[i]->namespace
                    || (strcmp (cp->attrs[i]->namespace, namespace) != 0))
                    continue;
            }
            if (strcmp (ln, cp->attrs[i]->name) == 0) {
                found = 1;
                if (cp->attrs[i]->cp) {
                    if (!checkText (interp, cp->attrs[i]->cp,
                                    (char *) atPtr[1])) {
                        if (!recover (interp, sdata,
                                      S("WRONG_ATTRIBUTE_VALUE"))) {
                            if (nsatt) namespace[j] = '\xFF';
                            SetResult3 ("Attribute value doesn't match for "
                                        "attribute '", atPtr[0], "'");
                            return TCL_ERROR;
                        }
                    }
                }
                if (cp->attrs[i]->required) reqAttr++;
                break;
            }
        }
        if (nsatt) namespace[j] = '\xFF';
        if (!found) {
            if (!recover (interp, sdata, S("UNKNOWN_ATTRIBUTE"))) {
                SetResult3 ("Unknown attribute \"", atPtr[0], "\"");
                return TCL_ERROR;
            }
        }
    }
    if (reqAttr != cp->numReqAttr) {
        /* Lookup the missing attribute(s) */
        SetResult ("Missing mandatory attribute(s):");
        for (i = 0; i < cp->numAttr; i++) {
            if (!cp->attrs[i]->required) continue;
            found = 0;
            for (atPtr = (char **) attr; atPtr[0] && atPtr[1]; atPtr += 2) {
                if (cp->attrs[i]->namespace) {
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
                        continue;
                    }
                    if (strcmp (cp->attrs[i]->namespace, namespace) != 0) {
                        continue;
                    }
                }
                if (strcmp (atPtr[0], cp->attrs[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (!recover (interp, sdata, S("MISSING_ATTRIBUTE"))) {
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

int probeDomAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domAttrNode *attr
    )
{
    domAttrNode *atPtr;
    int i, found, reqAttr = 0;
    const char *ns, *ln;
    SchemaCP *cp;

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
        for (i = 0; i < cp->numAttr; i++) {
            if (ns) {
                if (!cp->attrs[i]->namespace) continue;
                if (strcmp (ns, cp->attrs[i]->namespace) != 0) continue;
            } else {
                if (cp->attrs[i]->namespace) continue;
            }
            if (strcmp (ln, cp->attrs[i]->name) == 0) {
                if (cp->attrs[i]->cp) {
                    if (!checkText (interp, cp->attrs[i]->cp,
                                    (char *) atPtr->nodeValue)) {
                        if (!recover (interp, sdata,
                                      S("WRONG_ATTRIBUTE_VALUE"))) {
                            SetResult3 ("Attribute value doesn't match for "
                                        "attribute '", ln, "'");
                            return TCL_ERROR;
                        }
                    }
                }
                found = 1;
                if (cp->attrs[i]->required) reqAttr++;
                break;
            }
        }
        if (!found) {
            if (!recover (interp, sdata, S("UNKNOWN_ATTRIBUTE"))) {
                if (ns) {
                    SetResult ("Unknown attribute \"");
                    Tcl_AppendResult (interp, ns, ":", atPtr->nodeName,
                                      "\"");
                } else {
                    SetResult3 ("Unknown attribute \"", atPtr->nodeName, "\"");
                }
                sdata->validationState = VALIDATION_ERROR;
                return TCL_ERROR;
            }
        }
    nextAttr:
        atPtr = atPtr->nextSibling;
    }
    if (reqAttr != cp->numReqAttr) {
        /* Lookup the missing attribute(s) */
        SetResult ("Missing mandatory attribute(s):");
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
                if (!recover (interp, sdata, S("MISSING_ATTRIBUTE"))) {
                    if (cp->attrs[i]->namespace) {
                        Tcl_AppendResult (interp, " ", cp->attrs[i]->namespace,
                                          ":", cp->attrs[i]->name, NULL);
                    } else {
                        Tcl_AppendResult (interp, " ", cp->attrs[i]->name,
                                          NULL);
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

static int checkElementEnd (
    Tcl_Interp *interp,
    SchemaData *sdata
    )
{
    SchemaValidationStack *se;
    SchemaCP *cp, *ic;
    int hm, ac, i, mayMiss, rc;
    int isName = 0;

    DBG(fprintf (stderr, "checkElementEnd:\n");
        serializeStack(sdata););
    se = sdata->stack;
    getContext (cp, ac, hm);

    switch (cp->type) {
    case SCHEMA_CTYPE_NAME:
        isName = 1;
        /* Fall through */
    case SCHEMA_CTYPE_PATTERN:
        if (ac < cp->nc && (hasMatched (cp->quants[ac], hm))) {
            DBG(fprintf (stderr, "ac %d has matched, skiping to next ac\n", ac));
            ac++; hm = 0;
        }
        while (ac < cp->nc) {
            DBG(fprintf (stderr, "ac %d hm %d mayMiss: %d\n",
                         ac, hm, mayMiss (cp->quants[ac])));
            if (mayMiss (cp->quants[ac])) {
                ac++; continue;
            }
            
            switch (cp->content[ac]->type) {
            case SCHEMA_CTYPE_TEXT:
                if (cp->content[ac]->nc) {
                    if (!checkText (interp, cp->content[ac], "")) {
                        if (recover (interp, sdata, S("MISSING_TEXT"))) {
                            break;
                        }
                        return 0;
                    }
                }
                break;

            case SCHEMA_CTYPE_CHOICE:
                mayMiss = 0;
                for (i = 0; i < cp->content[ac]->nc; i++) {
                    if (mayMiss (cp->content[ac]->quants[i])) {
                        mayMiss = 1;
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
                        mayMiss = 1;
                        break;

                    case SCHEMA_CTYPE_CHOICE:
                        /* Can't happen */
                    case SCHEMA_CTYPE_NAME:
                    case SCHEMA_CTYPE_ANY:
                        continue;
                        
                    case SCHEMA_CTYPE_INTERLEAVE:
                    case SCHEMA_CTYPE_PATTERN:
                        pushToStack (sdata, ic);
                        if (checkElementEnd (interp, sdata)) {
                            mayMiss = 1;
                        }
                        popStack (sdata);
                        break;
                        
                    case SCHEMA_CTYPE_VIRTUAL:
                        Tcl_Panic ("Virtual constrain in MIXED or CHOICE");
                        
                    }
                    if (mayMiss) break;
                }
                if (mayMiss) break;
                if (!recover (interp, sdata, S("MISSING_ONE_OF_CHOICE"))) {
                    return 0;
                }
                break;
                
            case SCHEMA_CTYPE_VIRTUAL:
                if (evalVirtual (interp, sdata, cp->content[ac])) break;
                else return 0;
                
            case SCHEMA_CTYPE_INTERLEAVE:
            case SCHEMA_CTYPE_PATTERN:
                pushToStack (sdata, cp->content[ac]);
                rc = checkElementEnd (interp, sdata);
                popStack (sdata);
                if (rc) break;
                return 0;
                
            case SCHEMA_CTYPE_ANY:
            case SCHEMA_CTYPE_NAME:
                if (recover (interp, sdata, S("MISSING_ELEMENT"))) {
                    break;
                }
                return 0;
            }
            ac++;
        }
        if (isName) return 1;
        return -1;

    case SCHEMA_CTYPE_VIRTUAL:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
        return 0;

    case SCHEMA_CTYPE_INTERLEAVE:
        for (i = 0; i < cp->nc; i++) {
            if (mustMatch (cp->quants[i], se->interleaveState[i])) {
                if (recover (interp, sdata, S("MISSING_ONE_OF_INTERLEAVE"))) {
                    break;
                }
                return 0;
            }
        }
        return -1;
    }
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

    if (sdata->unknownIDrefs) {
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
    if (sdata->idTables.numEntries) {
        for (h = Tcl_FirstHashEntry (&sdata->idTables, &search);
             h != NULL;
             h = Tcl_NextHashEntry (&search)) {
            dk = Tcl_GetHashValue (h);
            if (dk->unknownIDrefs) {
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
    if (haveErrMsg) {
        sdata->validationState = VALIDATION_ERROR;
        return 0;
    }
    return 1;
}

int
probeElementEnd (
    Tcl_Interp *interp,
    SchemaData *sdata
    )
{
    int rc;
    
    DBG(
        fprintf (stderr, "probeElementEnd: look if current stack top can end "
                 " name: '%s' deep: %d\n",
                 sdata->stack->pattern->name, getDeep (sdata->stack));
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

    rc = checkElementEnd (interp, sdata);
    while (rc == -1) {
        popStack (sdata);
        rc = checkElementEnd (interp, sdata);
    }

    if (rc) {
        popStack (sdata);
        if (sdata->stack == NULL) {
            /* End of the first pattern (the tree root) without error. */
            /* Check for unknown ID references */
            if (!checkDocKeys (interp, sdata)) {
                return TCL_ERROR;
            }
            /*  We have successfully ended validation */
            sdata->validationState = VALIDATION_FINISHED;
        }
        DBG(
            fprintf(stderr, "probeElementEnd: _CAN_ end here.\n");
            serializeStack (sdata);
            );
        return TCL_OK;
    }
    SetResult ("Missing mandatory content");
    sdata->validationState = VALIDATION_ERROR;
    DBG(
        fprintf(stderr, "probeElementEnd: CAN'T end here.\n");
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
    int ac, hm, isName = 0, i;

    DBG(fprintf (stderr, "matchText called with text '%s'\n", text));
    
    while (1) {
        se = sdata->stack;
        getContext (cp, ac, hm);
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
                        updateStack (se, cp, ac);
                        return 1;
                    }
                    if (!sdata->evalError) {
                        SetResult ("Invalid text content");
                    }
                    return 0;

                case SCHEMA_CTYPE_CHOICE:
                    if (candidate->flags & MIXED_CONTENT) {
                        updateStack (se, cp, ac);
                        return 1;
                    }
                    for (i = 0; i < candidate->nc; i++) {
                        ic = candidate->content[i];
                        switch (ic->type) {
                        case SCHEMA_CTYPE_TEXT:
                            if (checkText (interp, ic, text)) {
                                updateStack (se, cp, ac);
                                return 1;
                            }
                            break;

                        case SCHEMA_CTYPE_NAME:
                        case SCHEMA_CTYPE_ANY:
                            break;

                        case SCHEMA_CTYPE_INTERLEAVE:
                        case SCHEMA_CTYPE_PATTERN:
                            pushToStack (sdata, ic);
                            if (matchText (interp, sdata, text)) {
                                updateStack (se, cp, ac);
                                return 1;
                            }
                            popStack (sdata);
                            break;

                        case SCHEMA_CTYPE_VIRTUAL:
                            if (!evalVirtual (interp, sdata, ic)) return 0;
                            break;
                            
                        case SCHEMA_CTYPE_CHOICE:
                            Tcl_Panic ("MIXED or CHOICE child of MIXED or CHOICE");

                        }
                    }
                    if (mustMatch (cp->quants[ac], hm)) {
                        SetResult ("Unexpected text content");
                        return 0;
                    }
                    break;

                case SCHEMA_CTYPE_INTERLEAVE:
                case SCHEMA_CTYPE_PATTERN:
                    pushToStack (sdata, candidate);
                    if (matchText (interp, sdata, text)) {
                        updateStack (se, cp, ac);
                        return 1;
                    }
                    popStack (sdata);
                    if (mustMatch (cp->quants[ac], hm)) {
                        SetResult ("Unexpected text content");
                        return 0;
                    }
                    break;

                case SCHEMA_CTYPE_VIRTUAL:
                    if (!evalVirtual (interp, sdata, candidate)) return 0;
                    break;
                    
                case SCHEMA_CTYPE_NAME:
                case SCHEMA_CTYPE_ANY:
                    if (mustMatch (cp->quants[ac], hm)) {
                        SetResult ("Unexpected text content");
                        return 0;
                    }
                    break;

                }
                ac++;
            }
            if (isName) {
                SetResult ("Unexpected text content");
                return 0;
            }
            popStack (sdata);
            continue;

        case SCHEMA_CTYPE_VIRTUAL:
        case SCHEMA_CTYPE_CHOICE:
        case SCHEMA_CTYPE_TEXT:
        case SCHEMA_CTYPE_ANY:
            /* Never pushed onto stack */
            Tcl_Panic ("Invalid CTYPE onto the validation stack!");
            break;

        case SCHEMA_CTYPE_INTERLEAVE:
            for (i = 0; i < cp->nc; i++) {
                ic = cp->content[i];
                switch (ic->type) {
                case SCHEMA_CTYPE_TEXT:
                    if (checkText (interp, ic, text)) {
                        se->hasMatched = 1;
                        se->interleaveState[i] = 1;
                        return 1;
                    }
                    break;

                case SCHEMA_CTYPE_NAME:
                case SCHEMA_CTYPE_ANY:
                    break;

                case SCHEMA_CTYPE_INTERLEAVE:
                case SCHEMA_CTYPE_PATTERN:
                    pushToStack (sdata, ic);
                    if (matchText (interp, sdata, text)) {
                        updateStack (se, cp, ac);
                        return 1;
                    }
                    popStack (sdata);
                    break;

                case SCHEMA_CTYPE_CHOICE:
                    Tcl_Panic ("MIXED or CHOICE child of INTERLEAVE");

                case SCHEMA_CTYPE_VIRTUAL:
                    break;
                    
                }
            }
        }
        break;
    }
    return 0;
}

int
probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    )
{
    int only_whites;
    char *pc;

    DBG(fprintf (stderr, "probeText started, text: '%s'\n", text);)
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
        if (matchText (interp, sdata, text)) {
            return TCL_OK;
        }
    } else {
        only_whites = 1;
        pc = text;
        while (*pc) {
            if ( (*pc == ' ')  ||
                 (*pc == '\n') ||
                 (*pc == '\r') ||
                 (*pc == '\t') ) {
                pc++;
                continue;
            }
            only_whites = 0;
            break;
        }
        if (only_whites)  return TCL_OK;
        if (matchText (interp, sdata, text)) {
            return TCL_OK;
        }
    }
    if (recover (interp, sdata, S("WRONG_VALUE"))) {
        return TCL_OK;
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
        if (probeText (vdata->interp, sdata,
                       Tcl_DStringValue (vdata->cdata)) != TCL_OK) {
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

    if (probeElement (vdata->interp, sdata, s, namespace)
        != TCL_OK) {
        sdata->validationState = VALIDATION_ERROR;
        XML_StopParser (vdata->parser, 0);
        return;
    }
    if (atts[0] || (sdata->stack
                    && sdata->stack->pattern->attrs)) {
        if (probeAttributes (vdata->interp, sdata, atts)
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
        if (probeText (vdata->interp, sdata,
                       Tcl_DStringValue (vdata->cdata)) != TCL_OK) {
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
    if (probeElementEnd (vdata->interp, sdata)
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

static int
validateString (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *xmlstr,
    int len
    )
{
    XML_Parser parser;
    char sep = '\xFF';
    ValidateMethodData vdata;
    Tcl_DString cdata;
    Tcl_Obj *resultObj;
    char sl[50], sc[50];
    int result;

    parser = XML_ParserCreate_MM (NULL, MEM_SUITE, &sep);
    vdata.interp = interp;
    vdata.sdata = sdata;
    vdata.parser = parser;
    Tcl_DStringInit (&cdata);
    vdata.cdata = &cdata;
    vdata.onlyWhiteSpace = 1;
    vdata.uri = (char *) MALLOC (URI_BUFFER_LEN_INIT);
    vdata.maxUriLen = URI_BUFFER_LEN_INIT;
    XML_SetUserData (parser, &vdata);
    XML_SetElementHandler (parser, startElement, endElement);
    XML_SetCharacterDataHandler (parser, characterDataHandler);

    if (XML_Parse (parser, xmlstr, len, 1) != XML_STATUS_OK
        || sdata->validationState == VALIDATION_ERROR) {
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
        result = TCL_ERROR;
    } else {
        result = TCL_OK;
    }
    XML_ParserFree (parser);
    Tcl_DStringFree (&cdata);
    FREE (vdata.uri);
    while (sdata->stack) popStack (sdata);
    return result;
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
    char *errMsg = NULL, *keystr;
    Tcl_HashTable htable;
    Tcl_DString dStr;

    kc = sdata->stack->pattern->domKeys;
    memset (&nodeList, 0, sizeof (xpathResultSet));
    nodeList.type = EmptyResult;
    memset (&rs, 0, sizeof (xpathResultSet));
    rs.type = EmptyResult;
    memset (&frs, 0, sizeof (xpathResultSet));
    frs.type = EmptyResult;
    Tcl_DStringInit (&dStr);
    while (kc) {
        xpathRSReset (&rs, NULL);
        xpathRSReset (&nodeList, node);
        Tcl_InitHashTable (&htable, TCL_STRING_KEYS);
        rc = xpathEvalAst (kc->selector, &nodeList, node, &rs, &errMsg);
        if (rc) {
            if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                goto nextConstraint;
            }
            goto errorCleanup;
        }
        if (rs.type == EmptyResult) goto nextConstraint;
        if (rs.type != xNodeSetResult) {
            if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                goto nextConstraint;
            }
            SetResult ("INVALID_DOM_KEYCONSTRAINT");
            goto errorCleanup;
        }
        for (i = 0; i < rs.nr_nodes; i++) {
            n = rs.nodes[i];
            if (n->nodeType != ELEMENT_NODE) {
                if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                    break;
                }
                SetResult ("INVALID_DOM_KEYCONSTRAINT");
                goto errorCleanup;
            }
            xpathRSReset (&nodeList, n);
            if (kc->nrFields == 1) {
                xpathRSReset (&frs, NULL);
                rc = xpathEvalAst (kc->fields[0], &nodeList, n, &frs,
                                   &errMsg);
                if (rc) {
                    if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                        break;
                    }
                    SetResult ("INVALID_DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.type != xNodeSetResult
                    && frs.type != EmptyResult) {
                    if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                        break;
                    }
                    SetResult ("INVALID_DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.type == EmptyResult || frs.nr_nodes == 0) {
                    Tcl_CreateHashEntry (&htable, "", &hnew);
                    if (!hnew) {
                        if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                            break;
                        }
                        SetResult ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    continue;
                }
                if (frs.nr_nodes != 1) {
                    if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                        break;
                    }
                    SetResult ("DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.nodes[0]->nodeType != ELEMENT_NODE
                    && frs.nodes[0]->nodeType != ATTRIBUTE_NODE) {
                    if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                        break;
                    }
                    SetResult ("INVALID_DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
                if (frs.nodes[0]->nodeType == ATTRIBUTE_NODE) {
                    attr = (domAttrNode *) frs.nodes[0];
                    Tcl_CreateHashEntry (&htable, attr->nodeValue, &hnew);
                    if (!hnew) {
                        if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                            break;
                        }
                        SetResult ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                } else {
                    keystr = xpathGetStringValue (frs.nodes[0], &len);
                    Tcl_CreateHashEntry (&htable, attr->nodeValue, &hnew);
                    FREE(keystr);
                    if (!hnew) {
                        if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                            break;
                        }
                        SetResult ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                }
            } else {
                Tcl_DStringSetLength (&dStr, 0);
                skip = 1;
                first = 0;
                for (j = 0; j < kc->nrFields; j++) {
                    xpathRSReset (&frs, NULL);
                    rc = xpathEvalAst (kc->fields[j], &nodeList, n, &frs,
                                       &errMsg);
                    if (rc) {
                        if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                            skip = 1;
                            break;
                        }
                        SetResult ("INVALID_DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (frs.type != xNodeSetResult
                        && frs.type != EmptyResult) {
                        if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                            skip = 1;
                            break;
                        }
                        SetResult ("INVALID_DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (frs.type == EmptyResult || frs.nr_nodes == 0) {
                        if (first) first = 0;
                        else Tcl_DStringAppend (&dStr, ":", 1);
                        continue;
                    }
                    if (frs.nr_nodes != 1) {
                        if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                            skip = 1;
                            break;
                        }
                        SetResult ("DOM_KEYCONSTRAINT");
                        goto errorCleanup;
                    }
                    if (frs.nodes[0]->nodeType != ELEMENT_NODE
                        && frs.nodes[0]->nodeType != ATTRIBUTE_NODE) {
                        if (recover (interp, sdata, S("INVALID_DOM_KEYCONSTRAINT"))) {
                            skip = 1;
                            break;
                        }
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
                    }
                }
                if (skip) break;
                Tcl_CreateHashEntry (&htable, Tcl_DStringValue (&dStr), &hnew);
                if (!hnew) {
                    if (recover (interp, sdata, S("DOM_KEYCONSTRAINT"))) {
                        break;
                    }
                    SetResult ("DOM_KEYCONSTRAINT");
                    goto errorCleanup;
                }
            }
        }
    nextConstraint:
        Tcl_DeleteHashTable (&htable);
        kc = kc->next;
    }
    schemaxpathRSFree (&frs);
    schemaxpathRSFree (&rs);
    schemaxpathRSFree (&nodeList);
    return TCL_OK;

errorCleanup:
    Tcl_DeleteHashTable (&htable);
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
    
    if (probeElement (interp, sdata, ln,
                      node->namespace ?
                      node->ownerDocument->namespaces[node->namespace-1]->uri
                      : NULL)
        != TCL_OK) {
        return TCL_ERROR;
    }
    if (node->firstAttr) {
        if (probeDomAttributes (interp, sdata, node->firstAttr) != TCL_OK) {
            return TCL_ERROR;
        }
    } else {
        if (sdata->stack->pattern->numReqAttr) {
            /* probeDomAttributes fills interp result with a msg which
             * required attributes are missing. */
            probeDomAttributes (interp, sdata, NULL);
            return TCL_ERROR;
        }
    }

    if (sdata->stack->pattern->domKeys) {
        if (checkdomKeyConstraints (interp, sdata, node) != TCL_OK)
            return TCL_ERROR;
    }
    
    node = node->firstChild;
    while (node) {
        switch (node->nodeType) {
        case ELEMENT_NODE:
            if (Tcl_DStringLength (sdata->cdata)) {
                if (probeText (interp, sdata,
                               Tcl_DStringValue (sdata->cdata)) != TCL_OK)
                    return TCL_ERROR;
                Tcl_DStringSetLength (sdata->cdata, 0);
            }
            if (validateDOM (interp, sdata, node) != TCL_OK) return TCL_ERROR;
            break;

        case TEXT_NODE:
        case CDATA_SECTION_NODE:
            if (node == node->parentNode->firstChild
                && node == node->parentNode->lastChild) {
                Tcl_DStringAppend (sdata->cdata,
                                   ((domTextNode *) node)->nodeValue,
                                   ((domTextNode *) node)->valueLength);
                if (probeText (interp, sdata,
                               Tcl_DStringValue (sdata->cdata)) != TCL_OK) {
                    Tcl_DStringSetLength (sdata->cdata, 0);
                    return TCL_ERROR;
                }
                Tcl_DStringSetLength (sdata->cdata, 0);
                break;
            }
            Tcl_DStringAppend (sdata->cdata,
                               ((domTextNode *) node)->nodeValue,
                               ((domTextNode *) node)->valueLength);
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
    if (Tcl_DStringLength (sdata->cdata)) {
        if (probeText (interp, sdata, Tcl_DStringValue (sdata->cdata))
            != TCL_OK) return TCL_ERROR;
        Tcl_DStringSetLength (sdata->cdata, 0);
    }
    if (probeElementEnd (interp, sdata) != TCL_OK) return TCL_ERROR;
    return TCL_OK;
}

void
schemaReset (
    SchemaData *sdata
    )
{
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaDocKey *dk;
    
    while (sdata->stack) popStack (sdata);
    sdata->validationState = VALIDATION_READY;
    sdata->skipDeep = 0;
    sdata->evalError = 0;
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
            if (&dk->ids.numEntries) {
                Tcl_DeleteHashTable (&dk->ids);
                Tcl_InitHashTable (&dk->ids, TCL_STRING_KEYS);
                dk->unknownIDrefs = 0;
            }
        }
    }
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
    if (sdata->cp && !sdata->isAttributeConstaint && cp->nc) {
        sdata->cp->flags |= CONSTRAINT_TEXT_CHILD;
    }
    return result;
}

static int
schemaInstanceInfoCmd (
    SchemaData *sdata,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int methodIndex;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    SchemaCP *cp;
    SchemaValidationStack *se;
    Tcl_Obj *resultObj, *elmObj;
    
    static const char *schemaInstanceInfoMethods[] = {
        "defelements", "stack", NULL
    };
    enum schemaInstanceInfoMethod {
        m_defelements, m_stack
    };

    static const char *schemaInstanceInfoStackMethods[] = {
        "top", "inside", NULL
    };
    enum schemaInstanceInfoStackMethod {
        m_top, m_inside
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
    case m_defelements:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 1, objv, "defelements");
            return TCL_ERROR;
        }
        resultObj = Tcl_GetObjResult (interp);
        for (h = Tcl_FirstHashEntry (&sdata->element, &search);
             h != NULL;
             h = Tcl_NextHashEntry (&search)) {
            cp = (SchemaCP *) Tcl_GetHashValue (h);
            if (!cp) continue;
            if (cp->flags & FORWARD_PATTERN_DEF
                || cp->flags & PLACEHOLDER_PATTERN_DEF) continue;
            serializeElementName (elmObj, cp);
            if (Tcl_ListObjAppendElement (interp, resultObj, elmObj) != TCL_OK)
                return TCL_ERROR;
        }
        break;

    case m_stack:
        if (Tcl_GetIndexFromObj (interp, objv[2],
                                 schemaInstanceInfoStackMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum schemaInstanceInfoStackMethod) methodIndex) {
        case m_top:
            break;
        case m_inside:
            if (!sdata->stack) {
                Tcl_ResetResult (interp);
                return TCL_OK;
            }
            se = sdata->stack;
            while (se->pattern->type != SCHEMA_CTYPE_NAME) {
                se = se->down;
            }
            serializeElementName (elmObj, se->pattern);
            Tcl_SetObjResult (interp, elmObj);
            return TCL_OK;
            break;
        }
        
    }
    
    return TCL_OK;
}

int
schemaInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, keywordIndex, hnew, patternIndex;
    int            result = TCL_OK, forwardDef = 0, i = 0, j;
    int            savedDefineToplevel, type, len;
    unsigned int   savedNumPatternList;
    SchemaData    *savedsdata = NULL, *sdata = (SchemaData *) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *h, *h1;
    SchemaCP      *pattern, *current = NULL;
    void          *namespacePtr, *savedNamespacePtr;
    char          *xmlstr, *errMsg;
    domDocument   *doc;
    domNode       *node;

    static const char *schemaInstanceMethods[] = {
        "defelement", "defpattern",  "start",   "event", "delete",
        "nrForwardDefinitions",      "state",   "reset", "define",
        "validate",   "domvalidate", "deftext", "info",  "reportcmd",
        "prefixns",   NULL
    };
    enum schemaInstanceMethod {
        m_defelement,  m_defpattern,  m_start,   m_event, m_delete,
        m_nrForwardDefinitions,       m_state,   m_reset, m_define,
        m_validate,    m_domvalidate, m_deftext, m_info,  m_reportcmd,
        m_prefixns
    };

    static const char *eventKeywords[] = {
        "start", "end", "text", NULL
    };

    enum eventKeyword
    {
        k_elementstart, k_elementend, k_text
    };

    if (sdata == NULL) {
        /* Inline defined defelement, defpattern, deftext, start or
         * prefixns */
        sdata = GETASI;
        CHECK_SI;
        if (!sdata->defineToplevel && sdata->currentEvals > 1) {
            SetResult ("Method not allowed in nested schema define script");
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
            Tcl_WrongNumArgs (interp, 1-i, objv, "<name>"
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
            if (forwardDef) {
                if (pattern->flags & FORWARD_PATTERN_DEF) {
                    sdata->forwardPatternDefs--;
                    pattern->flags &= ~FORWARD_PATTERN_DEF;
                }
                pattern->flags &= ~PLACEHOLDER_PATTERN_DEF;
            }
        } else {
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

    case m_deftext:
        CHECK_RECURSIVE_CALL
        if (objc !=  4-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<name>"
                              " <constraints script>");
            return TCL_ERROR;
        }
        h = Tcl_CreateHashEntry (&sdata->textDef, Tcl_GetString (objv[2-i]),
                                 &hnew);
        if (!hnew) {
            SetResult ("There is already a text type definition with this "
                       "name");
            return TCL_ERROR;
        }
        savedsdata = GETASI;
        if (savedsdata == sdata) {
            SetResult ("Recursive call of schema command is not allowed");
            return TCL_ERROR;
        }
        pattern = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
        pattern->type = SCHEMA_CTYPE_TEXT;
        REMEMBER_PATTERN (pattern)
        SETASI(sdata);
        result = evalConstraints (interp, sdata, pattern, objv[3-i]);
        SETASI(savedsdata);
        if (result == TCL_OK) {
            Tcl_SetHashValue (h, pattern);
        } else {
            Tcl_DeleteHashEntry (h);
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
            if (objc < 4 && objc > 6) {
                Tcl_WrongNumArgs (interp, 3, objv, "<elementname>"
                    "?<attInfo>? ?<namespace>?");
                return TCL_ERROR;
            }
            namespacePtr = NULL;
            if (objc == 6) {
                namespacePtr = getNamespacePtr (sdata,
                                                Tcl_GetString (objv[5]));
            }
            result = probeElement (interp, sdata, Tcl_GetString (objv[3]),
                                   namespacePtr);
            break;
        case k_elementend:
            if (objc != 3) {
                Tcl_WrongNumArgs (interp, 3, objv, "No arguments expected.");
                return TCL_ERROR;
            }
            result = probeElementEnd (interp, sdata);
            break;

        case k_text:
            if (objc != 4) {
                Tcl_WrongNumArgs (interp, 3, objv, "<text>");
                return TCL_ERROR;
            }
            result = probeText (interp, sdata, Tcl_GetString (objv[3]));
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

    case m_nrForwardDefinitions:
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        SetIntResult(sdata->forwardPatternDefs);
        break;

    case m_state:
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

    case m_reset:
        CHECK_EVAL
        schemaReset (sdata);
        break;

    case m_validate:
        CHECK_EVAL
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<xml> ?resultVarName?");
            return TCL_ERROR;
        }
        xmlstr = Tcl_GetStringFromObj (objv[2], &len);
        if (validateString (interp, sdata, xmlstr, len) == TCL_OK) {
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
            sdata->validationState = VALIDATION_STARTED;
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
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<tcl-cmd>");
            return TCL_ERROR;
        }
        if (sdata->reportCmd) {
            Tcl_DecrRefCount (sdata->reportCmd);
        }
        sdata->reportCmd = objv[2];
        Tcl_IncrRefCount (sdata->reportCmd);
        break;

    case m_prefixns:
        CHECK_RECURSIVE_CALL
        if (clientData == NULL && !sdata->defineToplevel) {
            SetResult ("Command only allowed at lop level");
            return TCL_ERROR;
        }
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
        
    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        result = TCL_ERROR;
        break;

    }
    if (sdata->cleanupAfterEval && sdata->currentEvals == 0) {
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
                              schemaInstanceCmd,
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
    if (Tcl_GetIntFromObj (interp, thisObj, m) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return SCHEMA_CQUANT_ERROR;
    }
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
    int n, m;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,2,"?quant?");
    quant = getQuant (interp, sdata, objc == 1 ? NULL : objv[1], &n, &m);
    if (quant == SCHEMA_CQUANT_ERROR) {
        return TCL_ERROR;
    }
    pattern = initSchemaCP (SCHEMA_CTYPE_ANY, NULL, NULL);
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
    unsigned int savedContenSize;
    unsigned int savedAttrSize, savedNumAttr, savedNumReqAttr;
    int result, i;

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

    if (result == TCL_OK) {
        REMEMBER_PATTERN (pattern);
        for (i = 0; i < pattern->nc; i++) {
            if (pattern->content[i]->type == SCHEMA_CTYPE_PATTERN) {
                if (pattern->content[i]->flags & CONSTRAINT_TEXT_CHILD) {
                    pattern->flags |= CONSTRAINT_TEXT_CHILD;
                    break;
                }
            }
        }
        addToContent (sdata, pattern, quant, n, m);
    } else {
        freeSchemaCP (pattern);
    }
    return result;
}

/* Implements the schema definition commands "element" and "ref" */
static int
NamedPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    Schema_CP_Type patternType = (Schema_CP_Type) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    SchemaCP *pattern = NULL, *current;
    SchemaQuant quant;
    int hnew, n, m;

    CHECK_SI
    CHECK_TOPLEVEL
    if (patternType == SCHEMA_CTYPE_NAME) {
        checkNrArgs (2,4,"Expected: elementName ?quant? ?pattern?");
        hashTable = &sdata->element;
    } else {
        checkNrArgs (2,3,"Expected: patternName ?quant?");
        hashTable = &sdata->pattern;
    }

    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2], &n, &m);
    if (quant == SCHEMA_CQUANT_ERROR) {
        return TCL_ERROR;
    }
    entryPtr = Tcl_CreateHashEntry (hashTable,
                                    Tcl_GetString(objv[1]), &hnew);
    if (objc < 4) {
        /* Reference to an element or pattern */
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (entryPtr);
            while (pattern) {
                if (pattern->namespace == sdata->currentNamespace) {
                    break;
                }
                pattern = pattern->next;
            }
        }
        if (!pattern) {
            pattern = initSchemaCP (
                patternType,
                sdata->currentNamespace,
                Tcl_GetHashKey (hashTable, entryPtr)
                );
            pattern->flags |= FORWARD_PATTERN_DEF;
            sdata->forwardPatternDefs++;
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (entryPtr);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }
        addToContent (sdata, pattern, quant, n, m);
    } else {
        /* Local definition of this element */
        if (hnew) {
            pattern = initSchemaCP (
                SCHEMA_CTYPE_NAME,
                sdata->currentNamespace,
                Tcl_GetHashKey (hashTable, entryPtr)
                );
            pattern->flags |= PLACEHOLDER_PATTERN_DEF;
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }
        pattern = initSchemaCP (
            SCHEMA_CTYPE_NAME,
            sdata->currentNamespace,
            Tcl_GetHashKey (hashTable, entryPtr)
            );
        pattern->flags |= LOCAL_DEFINED_ELEMENT;
        return evalDefinition (interp, sdata, objv[3], pattern, quant, n, m);
    }
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
    int hnew, i, result = TCL_OK;
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
         * / namespace */
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
    attr->required = required;
    if (scriptObj) {
        cp = initSchemaCP (SCHEMA_CTYPE_CHOICE, NULL, NULL);
        cp->type = SCHEMA_CTYPE_TEXT;
        REMEMBER_PATTERN (cp)
        sdata->isAttributeConstaint = 1;
        result = evalConstraints (interp, sdata, cp, scriptObj);
        sdata->isAttributeConstaint = 0;
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
TextPatternObjCmd (
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
        h = Tcl_FindHashEntry (&sdata->textDef, Tcl_GetString (objv[2]));
        if (!h) {
            SetResult3 ("Unknown text type \"", Tcl_GetString (objv[2]), "\"");
            return TCL_ERROR;
        }
        quant = SCHEMA_CQUANT_ONE;
        pattern = (SchemaCP *) Tcl_GetHashValue (h);
        sdata->cp->flags |= CONSTRAINT_TEXT_CHILD;
    }
    if (objc < 3) {
        REMEMBER_PATTERN (pattern)
    }
    addToContent (sdata, pattern, quant, 0, 0);
    if (objc == 2) {
        return evalConstraints (interp, sdata, pattern, objv[1]);
    }
    return TCL_OK;
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

    switch (sdata->cp->type) {
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
        break;
    default:
        SetResult ("The \"tcl\" schema definition command is only "
                   "allowed in sequential context (defelement, "
                   "element or defpattern)");
        return TCL_ERROR;
    }

    pattern = initSchemaCP (SCHEMA_CTYPE_VIRTUAL, NULL, NULL);
    REMEMBER_PATTERN (pattern)
    /* We alloc for one arugment more: the always appended schema
     * cmd, */
    pattern->content = MALLOC (sizeof (Tcl_Obj*) * (objc));
    for (i = 1; i < objc; i++) {
        pattern->content[i-1] = (SchemaCP *) objv[i];
        Tcl_IncrRefCount (objv[i]);
    }
    pattern->nc = objc;
    addToContent (sdata, pattern, SCHEMA_CQUANT_ONE, 0, 0);
    return TCL_OK;
}

static int
domuniquePatternCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    ast t;
    char *errMsg = NULL;
    domKeyConstraint *kc;
    int i, nrFields;
    Tcl_Obj *elm;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (3,4,"Expected: <selector> <fieldlist> ?<name>?");
    if (sdata->cp->type != SCHEMA_CTYPE_NAME) {
        SetResult ("The domunique schema definition command is only "
                   "allowed as direct child of an element.");
    }
    
    if (xpathParse (Tcl_GetString (objv[1]), NULL, XPATH_EXPR,
                    sdata->prefixns, NULL, &t, &errMsg) < 0) {
        SetResult3 ("Error in selector xpath: '", errMsg, "");
        FREE (errMsg);
        return TCL_ERROR;
    }

    if (Tcl_ListObjLength (interp, objv[2], &nrFields) != TCL_OK) {
        SetResult ("The <fieldlist> argument must be a valid tcl list");
        xpathFreeAst (t);
        return TCL_ERROR;
    }
    if (nrFields == 0) {
        SetResult ("Non empty fieldlist arugment expected.");
        xpathFreeAst (t);
        return TCL_ERROR;
    }
    
    kc = TMALLOC (domKeyConstraint);
    memset (kc, 0, sizeof (domKeyConstraint));
    kc->fields = MALLOC (sizeof (ast) * nrFields);
    memset (kc->fields, 0, sizeof (ast) * nrFields);
    kc->nrFields = nrFields;
    kc->selector = t;
    
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
    if (objc == 4) {
        kc->name = tdomstrdup (Tcl_GetString (objv[3]));
    }
    kc->next = sdata->cp->domKeys;
    sdata->cp->domKeys = kc;
    return TCL_OK;
}

static int
integerImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int n;

    if (Tcl_GetInt (interp, text, &n) != TCL_OK) {
        return 0;
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

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"no argument expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = integerImpl;
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
    checkNrArgs (2,2,"Expected: <regexp>");
    /* Compile it as syntax test (plus caches the complied regexp in
     * the internal value) */
    if (!Tcl_GetRegExpFromObj (interp, objv[1], 0)) {
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
    CHECK_TOPLEVEL
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
            SetResult ("Invalid charcter: attribute value isn't a NMTOKENS");
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
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = nmtokensImpl;
    return TCL_OK;
}

static int
numberImpl (
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

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = numberImpl;
    return TCL_OK;
}

static int
booleanImpl (
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

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = booleanImpl;
    return TCL_OK;
}

static int
isodateImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int i, y, m, d;

    if (*text == '-') {
        /* A bce date */
        text++;
    }
    for (i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9') return 0;
        text++;
    }
    while (*text >= '0' && *text <= '9') text++;
    if (*text != '-') return 0;
    y = atoi(text-4);
    /* There isn't a year 0. it's either 0001 or -0001 */
    if (y == 0) return 0;
    text++;
    for (i = 0; i < 2; i++) {
        if (*text < '0' || *text > '9') return 0;
        text++;
    }
    if (*text != '-') return 0;
    m = atoi(text-2);
    if (m < 1 || m > 12) return 0;
    text++;
    for (i = 0; i < 2; i++) {
        if (*text < '0' || *text > '9') return 0;
        text++;
    }
    if (*text != '\0') return 0;
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
    return 1;
}

static int
isodateTCObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    SchemaData *sdata = GETASI;
    SchemaConstraint *sc;

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = isodateImpl;
    return TCL_OK;
}

static int
maxLengthImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    long maxlen = (long) constraintData;
    int len = 0, clen;

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
    long len;

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (2,2,"Expected: <maximal length as integer>");
    if (Tcl_GetLongFromObj (interp, objv[1], &len) != TCL_OK) {
        SetResult ("Expected: <maximal length as integer>");
        return TCL_ERROR;
    }
    if (len < 1) {
        SetResult ("The maximum length must be at least 1");
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = maxLengthImpl;
    sc->constraintData = (void *)len;
    return TCL_OK;
}

static int
minLengthImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    long minlen = (long) constraintData;
    int len = 0, clen;

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
    long len;

    CHECK_TI
    CHECK_TOPLEVEL
    checkNrArgs (2,2,"Expected: <minimum length as integer>");
    if (Tcl_GetLongFromObj (interp, objv[1], &len) != TCL_OK) {
        SetResult ("Expected: <minimum length as integer>");
        return TCL_ERROR;
    }
    if (len < 1) {
        SetResult ("The minimum length must be at least 1");
    }
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = minLengthImpl;
    sc->constraintData = (void *)len;
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
    int i;

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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
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
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected");
    ADD_CONSTRAINT (sdata, sc)
    sc->constraint = base64Impl;
    return TCL_OK;
}

void
tDOM_SchemaInit (
    Tcl_Interp *interp
    )
{
    /* Inline definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::defelement",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::defpattern",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::deftext",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::start",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::prefixns",
                          schemaInstanceCmd, NULL, NULL);

    /* The "any" definition command. */
    Tcl_CreateObjCommand (interp, "tdom::schema::any",
                          AnyPatternObjCmd, NULL, NULL);

    /* The named pattern commands "element" and "ref". */
    Tcl_CreateObjCommand (interp, "tdom::schema::element",
                          NamedPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_NAME, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::ref",
                          NamedPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_PATTERN, NULL);

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

    /* The "attribute", "nsattribute", "namespace" and "text" definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::nsattribute",
                          AttributePatternObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text",
                          TextPatternObjCmd, NULL, NULL);

    /* The 'virtual' "tcl" definition command */
    Tcl_CreateObjCommand (interp, "tdom::schema::tcl",
                          VirtualPatternObjCmd, NULL, NULL);

    /* XPath contraints for DOM validation */
    Tcl_CreateObjCommand (interp,"tdom::schema::domunique",
                          domuniquePatternCmd, NULL, NULL);
    
    /* The text constraint commands */
    Tcl_CreateObjCommand (interp,"tdom::schema::text::integer",
                          integerTCObjCmd, NULL, NULL);
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
    Tcl_CreateObjCommand (interp,"tdom::schema::text::isodate",
                          isodateTCObjCmd, NULL, NULL);
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
}


#endif  /* #ifndef TDOM_NO_SCHEMA */
