/*----------------------------------------------------------------------------
|   Copyright (c) 2018  Rolf Ade (rolf@pointsman.de)
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
|   Nov, Dec, Jan 2018-2019
|
\---------------------------------------------------------------------------*/

#ifndef TDOM_NO_SCHEMA

#include <tdom.h>
#include <tcldom.h>
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
 
#define checkNrArgs(l,h,err) if (objc < l || objc > h) {      \
        SetResult (err);                                      \
        return TCL_ERROR;                                     \
    }

#if defined(DEBUG) || defined(DDEBUG)
static char *Schema_CP_Type2str[] = {
    "ANY",
    "MIXED",
    "NAME",
    "CHOICE",
    "INTERLEAVE",
    "PATTERN",
    "GROUP",
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


/* The SchemaFlags flags */
#define FORWARD_PATTERN_DEF     1
#define PLACEHOLDER_PATTERN_DEF 2
#define AMBIGUOUS_PATTERN       4
#define LOCAL_DEFINED_ELEMENT   8

/* Pointer to heap-allocated shared quants. */
static SchemaQuant QuantOne;
static SchemaQuant *quantOne = &QuantOne;

static SchemaQuant QuantOpt;
static SchemaQuant *quantOpt = &QuantOpt;

static SchemaQuant QuantRep;
static SchemaQuant *quantRep = &QuantRep;

static SchemaQuant QuantPlus;
static SchemaQuant *quantPlus = &QuantPlus;

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
        SetResult ("Command called outside of schema context.");        \
        return TCL_ERROR;                                               \
    }                                                                   \
    if (sdata->isTextConstraint) {                                      \
        SetResult ("Command called in invalid schema context.");        \
        return TCL_ERROR;                                               \
    }

#define CHECK_TI                                                        \
    if (!sdata) {                                                       \
        SetResult ("Command called outside of schema context.");        \
        return TCL_ERROR;                                               \
    }                                                                   \
    if (!sdata->isTextConstraint) {                                     \
        SetResult ("Command called in invalid schema context.");        \
        return TCL_ERROR;                                               \
    }

#define CHECK_TOPLEVEL                                                  \
    if (sdata->defineToplevel) {                                        \
        SetResult("Command not allowed at top level "                   \
                  "in schema define evaluation");                       \
        return TCL_ERROR;                                               \
    }

#define ADD_TO_CONTENT(pattern,quant)                                   \
    checkForAmbiguousness (pattern);                                    \
    if (sdata->numChildren == sdata->contentSize) {                     \
        sdata->currentContent =                                         \
            REALLOC (sdata->currentContent,                             \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaCP*));                             \
        sdata->currentQuants =                                          \
            REALLOC (sdata->currentQuants,                              \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaQuant*));                          \
        sdata->contentSize *= 2;                                        \
    }                                                                   \
    sdata->currentContent[sdata->numChildren] = (pattern);              \
    sdata->currentQuants[sdata->numChildren] = quant;                   \
    sdata->numChildren++;                                               \

#define ADD_CONSTRAINT(sdata, sc)                                       \
    sc = TMALLOC (SchemaConstraint);                                    \
    memset (sc, 0, sizeof (SchemaConstraint));                          \
    if (sdata->numChildren == sdata->contentSize) {                     \
        sdata->currentContent =                                         \
            REALLOC (sdata->currentContent,                             \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaCP*));                             \
        sdata->currentQuants =                                          \
            REALLOC (sdata->currentQuants,                              \
                     2 * sdata->contentSize                             \
                     * sizeof (SchemaQuant*));                          \
        sdata->contentSize *= 2;                                        \
    }                                                                   \
    sdata->currentContent[sdata->numChildren] = (SchemaCP *) sc;        \
    sdata->currentQuants[sdata->numChildren] = quantOne;                \
    sdata->numChildren++;                                               \

#define REMEMBER_PATTERN(pattern)                                       \
    if (sdata->numPatternList == sdata->patternListSize) {              \
        sdata->patternList = (SchemaCP **) REALLOC (                    \
            sdata->patternList,                                         \
            sizeof (SchemaCP*) * sdata->patternListSize * 2);           \
        sdata->patternListSize *= 2;                                    \
    }                                                                   \
    sdata->patternList[sdata->numPatternList] = pattern;                \
    sdata->numPatternList++;

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
    case SCHEMA_CTYPE_GROUP:
    case SCHEMA_CTYPE_MIXED:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_INTERLEAVE:
        pattern->content = (SchemaCP**) MALLOC (
            sizeof(SchemaCP*) * CONTENT_ARRAY_SIZE_INIT
            );
        pattern->quants = (SchemaQuant**) MALLOC (
            sizeof (SchemaQuant*) * CONTENT_ARRAY_SIZE_INIT
            );
        break;
    case SCHEMA_CTYPE_TEXT:
        /* content/quant will be allocated, if the cp in fact has
         * constraints */
        break;
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
    fprintf (stderr, "CP type: %s\n",
             Schema_CP_Type2str[pattern->type]);
    switch (pattern->type) {
    case SCHEMA_CTYPE_NAME:
    case SCHEMA_CTYPE_PATTERN:
    case SCHEMA_CTYPE_GROUP:
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
    case SCHEMA_CTYPE_MIXED:
    case SCHEMA_CTYPE_CHOICE:
    case SCHEMA_CTYPE_INTERLEAVE:
        fprintf (stderr, "\t%d childs\n", pattern->numChildren);
        break;
    case SCHEMA_CTYPE_ANY:
    case SCHEMA_CTYPE_TEXT:
        /* Do nothing */
        break;
    }
}

static void serializeQuant (
    SchemaQuant *quant
    )
{
    fprintf (stderr, "Quant type: %s n: %d m: %d\n",
             Schema_Quant_Type2str[quant->type], quant->minOccur, quant->maxOccur);
}

static void serializeStack (
    SchemaData *sdata
    ) 
{
    SchemaValidationStack *sp;

    fprintf (stderr, "++++ Current validation stack:\n");
    sp = sdata->stack;
    while (sp) {
        serializeCP (sp->pattern);
        fprintf (stderr, "\tdeep: %d ac: %d nm: %d\n",
                 sp->deep, sp->activeChild, sp->nrMatched);
        sp = sp->down;
    }
    fprintf (stderr, "++++ Stack bottom\n");
}
)

/* DBG end */

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
    case SCHEMA_CTYPE_TEXT:
        for (i = 0; i < pattern->numChildren; i++) {
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
        break;
    }
    FREE (pattern);
}

static SchemaData*
initSchemaData () 
{
    SchemaData *sdata;
    int hnew;
    
    sdata = TMALLOC (SchemaData);
    memset (sdata, 0, sizeof(SchemaData));
    Tcl_InitHashTable (&sdata->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->attrNames, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    sdata->emptyNamespace = Tcl_CreateHashEntry (
        &sdata->namespace, "", &hnew);
    sdata->patternList = (SchemaCP **) MALLOC (
        sizeof (SchemaCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->patternListSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    sdata->quants = (SchemaQuant **) MALLOC (
        sizeof (SchemaQuant*) * QUANTS_ARRAY_SIZE_INIT);
    sdata->quantsSize = QUANTS_ARRAY_SIZE_INIT;
    /* evalStub initialization */
    sdata->evalStub = (Tcl_Obj **) (MALLOC (sizeof (Tcl_Obj*) * 4));
    sdata->evalStub[0] = Tcl_NewStringObj("::namespace", 11);
    Tcl_IncrRefCount (sdata->evalStub[0]);
    sdata->evalStub[1] = Tcl_NewStringObj("eval", 4);
    Tcl_IncrRefCount (sdata->evalStub[1]);
    sdata->evalStub[2] = Tcl_NewStringObj("::tdom::schema", 14);
    Tcl_IncrRefCount (sdata->evalStub[2]);
    /* textStub initialization */
    sdata->textStub = (Tcl_Obj **) (MALLOC (sizeof (Tcl_Obj*) * 4));
    sdata->textStub[0] = Tcl_NewStringObj("::namespace", 11);
    Tcl_IncrRefCount (sdata->textStub[0]);
    sdata->textStub[1] = Tcl_NewStringObj("eval", 4);
    Tcl_IncrRefCount (sdata->textStub[1]);
    sdata->textStub[2] = Tcl_NewStringObj("::tdom::schema::text", 20);
    Tcl_IncrRefCount (sdata->textStub[2]);

    sdata->cdata = TMALLOC (Tcl_DString);
    Tcl_DStringInit (sdata->cdata);
    return sdata;
}

static void schemaInstanceDelete (
    ClientData clientData
    )
{
    SchemaData *sdata = (SchemaData *) clientData;
    unsigned int i;
    SchemaValidationStack *down;

    if (sdata->start) FREE (sdata->start);
    if (sdata->startNamespace) FREE (sdata->startNamespace);
    Tcl_DeleteHashTable (&sdata->namespace);
    Tcl_DeleteHashTable (&sdata->element);
    Tcl_DeleteHashTable (&sdata->pattern);
    Tcl_DeleteHashTable (&sdata->attrNames);
    for (i = 0; i < sdata->numPatternList; i++) {
        freeSchemaCP (sdata->patternList[i]);
    }
    FREE (sdata->patternList);
    for (i = 0; i < sdata->numQuants; i++) {
        FREE (sdata->quants[i]);
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
        if (hashTable) {
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
checkForAmbiguousness (
    SchemaCP *pattern
    )
{
    /* As long as we don't know otherwise we assume any pattern to be
     * ambiguous. */
    pattern->flags |= AMBIGUOUS_PATTERN;
}

static void
pushToStack (
    SchemaData *sdata,
    SchemaCP *pattern,
    int deep
    )
{
    SchemaValidationStack *stackElm, *se;

    if (sdata->stackPool) {
        stackElm = sdata->stackPool;
        se = stackElm->down;
        sdata->stackPool = se;
    } else {
        stackElm = TMALLOC (SchemaValidationStack);
    }
    memset (stackElm, 0, sizeof (SchemaValidationStack));
    se = sdata->stack;
    stackElm->down = se;
    stackElm->pattern = pattern;
    stackElm->deep = deep;
    sdata->stack = stackElm;
}

#define maxOne(quant) \
    ((quant) == quantOne || (quant) == quantOpt) ? 1 : 0

#define minOne(quant) \
    ((quant) == quantOne || (quant) == quantPlus) || (quant->type == SCHEMA_CQUANT_NM && (quant)->minOccur > 0)  ? 1 : 0

#define mayMiss(quant) \
    ((quant) == quantOpt || (quant) == quantRep) || (quant->type == SCHEMA_CQUANT_NM && (quant)->minOccur == 0) ? 1 : 0

#define mayRepeat(quant) \
    ((quant) == quantRep || (quant) == quantPlus) ? 1 : 0

#define mustMatch(quant,nr) \
    (nr) == 0 ? minOne(quant)                                              \
        : (quant->type == SCHEMA_CQUANT_NM && quant->minOccur > (nr)) ? 1 : 0

#define hasMatched(quant,nr) \
    (nr) == 0 ? 0 : ((nr) == 1 && (quant == quantOne || quant == quantOpt) ? 1 : quant->maxOccur == (nr))

#define getContext(parent, ac, nm)    \
    parent = sdata->stack->pattern;   \
    ac = sdata->stack->activeChild;   \
    nm = sdata->stack->nrMatched;

static void
updateStack (
    SchemaValidationStack *se,
    int ac,
    int nm
    )
{
    se->activeChild = ac;
    se->nrMatched = nm;
}

static void
popStack (
    SchemaData *sdata
    )
{
    SchemaValidationStack *se;
    se = sdata->stack->down;
    sdata->stack->down = sdata->stackPool;
    sdata->stackPool = sdata->stack;
    sdata->stack = se;
}

/* The cp argument must be type SCHEMA_CTYPE_TEXT */
static int
checkText (
    Tcl_Interp *interp,
    SchemaCP *cp,
    char *text
    )
{
    int i;
    SchemaConstraint *sc;
    
    for (i = 0; i < cp->numChildren; i++) {
        sc = (SchemaConstraint *) cp->content[i];
        if ((sc->constraint) (interp, sc->constraintData, text) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

static int
matchElementStart (
    SchemaData *sdata,
    char *name,
    char *namespace
    )
{
    SchemaCP *cp, *candidate;
    int nm, ac, i, start, end;
    int isName = 0, deep;
    SchemaValidationStack *se;

    getContext (cp, ac, nm);
    se = sdata->stack;
    deep = se->deep;
    
    while (1) {
        switch (cp->type) {
        case SCHEMA_CTYPE_NAME:
            isName = 1;
            /* fall through */
        case SCHEMA_CTYPE_GROUP:
        case SCHEMA_CTYPE_PATTERN:
            if (hasMatched (cp->quants[ac], nm)) {
                ac++; nm = 0;
                updateStack (se, ac, nm);
            }
            while (ac < cp->numChildren) {
                candidate = cp->content[ac];
                switch (candidate->type) {
                case SCHEMA_CTYPE_TEXT:
                    break;

                case SCHEMA_CTYPE_ANY:
                    updateStack (se, ac, nm+1);
                    sdata->skipDeep = 1;
                    return 1;
                
                case SCHEMA_CTYPE_NAME:
                    DBG(fprintf (stderr, "name: %s ns: %s candidate name: %s "
                                 "candidate ns: %s\n", name, namespace,
                                 candidate->name, candidate->namespace));
                    if (candidate->name == name
                        && candidate->namespace == namespace) {
                        updateStack (se, ac, nm+1);
                        pushToStack (sdata, candidate, deep + 1);
                        return 1;
                    }
                    break;
                case SCHEMA_CTYPE_GROUP:
                case SCHEMA_CTYPE_PATTERN:
                case SCHEMA_CTYPE_MIXED:
                case SCHEMA_CTYPE_INTERLEAVE:
                case SCHEMA_CTYPE_CHOICE:
                    pushToStack (sdata, candidate, deep);
                    if (matchElementStart (sdata, name, namespace)) {
                        updateStack (se, ac, nm+1);
                        return 1;
                    }
                    popStack (sdata);
                    break;
                }
                if (mustMatch (cp->quants[ac], nm)) {
                    return 0;
                }
                ac++;
                nm = 0;
            }
            if (isName) return 0;
            popStack (sdata);
            getContext (cp, ac, nm);
            se = sdata->stack;
            continue;
            
        case SCHEMA_CTYPE_TEXT:
            return 0;
        
        case SCHEMA_CTYPE_ANY:
            /* Never pushed onto stack */
            Tcl_Panic ("Invalid CTYPE onto the validation stack!");

        case SCHEMA_CTYPE_INTERLEAVE:
            fprintf (stderr, "matchElementStart: SCHEMA_CTYPE_INTERLEAVE to be implemented\n");
            return 0;

        case SCHEMA_CTYPE_MIXED:
        case SCHEMA_CTYPE_CHOICE:
            if (hasMatched (cp->quants[ac], nm)) {
                popStack (sdata);
                getContext (cp, ac, nm);
                se = sdata->stack;
                continue;
            }
            if (nm) {
                start = ac;
                end = ac + 1;
            } else {
                start = 0;
                end = cp->numChildren;
            }
            for (i = start; i < end; i++) {
                candidate = cp->content[i];
                switch (candidate->type) {
                case SCHEMA_CTYPE_TEXT:
                    break;

                case SCHEMA_CTYPE_ANY:
                    se->activeChild = ac;
                    se->nrMatched = nm + 1;
                    sdata->skipDeep = 1;
                    return 1;
                    
                case SCHEMA_CTYPE_NAME:
                    if (candidate->name == name
                        && candidate->namespace == namespace) {
                        se->activeChild = i;
                        se->nrMatched = nm + 1;
                        pushToStack (sdata, candidate, deep + 1);
                        return 1;
                    }
                    break;
                case SCHEMA_CTYPE_GROUP:
                case SCHEMA_CTYPE_PATTERN:
                case SCHEMA_CTYPE_MIXED:
                case SCHEMA_CTYPE_INTERLEAVE:
                case SCHEMA_CTYPE_CHOICE:
                    pushToStack (sdata, candidate, deep);
                    if (matchElementStart (sdata, name, namespace)) {
                        /* Matched */
                        se->activeChild = i;
                        se->nrMatched = nm + 1;
                        return 1;
                    }
                    popStack (sdata);
                    break;
                }
            }
            return 0;
        }
    }
    return 0;
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

    if (namespace) {
        entryPtr = Tcl_FindHashEntry (&sdata->namespace, (char *)namespace);
        if (!entryPtr) {
            SetResult ("No elements defined in this namespace");
            return TCL_ERROR;
        }
        if (entryPtr == sdata->emptyNamespace) {
            namespacePtr = NULL;
        } else {
            namespacePtr = Tcl_GetHashKey (&sdata->namespace, entryPtr);
        }
    } else {
        namespacePtr = NULL;
    }
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
                if (!sdata->startNamespace ||
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
    
    if (!sdata->stack) {
        if (!pattern) {
            SetResult ("Unknown element");
            return TCL_ERROR;
        }
        pushToStack (sdata, pattern, 0);
        sdata->validationState = VALIDATION_STARTED;
        return TCL_OK;
    }

    /* The normal case: we're inside the tree */
    if (matchElementStart (sdata, (char *) namePtr, (char *) namespacePtr)) {
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
    SetResult ("Element \"");
    if (namespacePtr) {
        Tcl_AppendResult (interp, namespacePtr, ":", NULL);
    }
    Tcl_AppendResult (interp, name, "\" doesn't match", NULL);
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
                    if (checkText (interp, cp->attrs[i]->cp, (char *) atPtr[1])
                        != TCL_OK) {
                        if (nsatt) namespace[j] = '\xFF';
                        SetResult3 ("Attribute value doesn't match for "
                                    "attribute '", atPtr[0], "'");
                        return TCL_ERROR;
                    }
                }
                if (cp->attrs[i]->required) reqAttr++;
                break;
            }
        }
        if (nsatt) namespace[j] = '\xFF';
        if (!found) {
            SetResult3 ("Unknown attribute \"", atPtr[0], "\"");
            return TCL_ERROR;
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
                if (cp->attrs[i]->namespace) {
                    Tcl_AppendResult (interp, " ", cp->attrs[i]->namespace,
                                      ":", cp->attrs[i]->name, NULL);
                } else {
                    Tcl_AppendResult (interp, " ", cp->attrs[i]->name, NULL);
                }
            }
        }
        return TCL_ERROR;
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
                    if (checkText (interp, cp->attrs[i]->cp, (char *) ln)
                        != TCL_OK) {
                        SetResult3 ("Attribute value doesn't match for "
                                    "attribute '", ln, "'");
                        return TCL_ERROR;
                    }
                }
                found = 1;
                if (cp->attrs[i]->required) reqAttr++;
                break;
            }
        }
        if (!found) {
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
                if (cp->attrs[i]->namespace) {
                    Tcl_AppendResult (interp, " ", cp->attrs[i]->namespace,
                                      ":", cp->attrs[i]->name, NULL);
                } else {
                    Tcl_AppendResult (interp, " ", cp->attrs[i]->name, NULL);
                }
            }
        }
        sdata->validationState = VALIDATION_ERROR;
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int checkElementEnd (
    SchemaData *sdata
    )
{
    SchemaCP *cp;
    int nm, ac;
    int isName = 0;
    
    getContext (cp, ac, nm);
    
    switch (cp->type) {
    case SCHEMA_CTYPE_NAME:
        /* if (!sdata->stack->down) return 1; */
        isName = 1;
        /* fall through */
    case SCHEMA_CTYPE_GROUP:
    case SCHEMA_CTYPE_PATTERN:
        if (ac < cp->numChildren && (hasMatched (cp->quants[ac], nm))) {
            DBG(fprintf (stderr, "ac has matched, skiping to next ac\n"));
            ac++; nm = 0;
        }
        while (ac < cp->numChildren) {
            DBG(fprintf (stderr, "ac %d nm %d mustMatch: %d\n",
                         ac, nm, mustMatch (cp->quants[ac], nm)));
            if (mustMatch (cp->quants[ac], nm)) {
                return 0;
            }
            ac ++;
            nm = 0;
        }
        if (isName) return 1;
        else return -1;

    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
        return 0;

    case SCHEMA_CTYPE_INTERLEAVE:
        fprintf (stderr, "checkElementEnd: SCHEMA_CTYPE_INTERLEAVE to be implemented\n");
        return 0;
        
    case SCHEMA_CTYPE_MIXED:
    case SCHEMA_CTYPE_CHOICE:
        if (hasMatched (cp->quants[ac], nm)) {
            return -1;
        }
        return 0;
    }
    return 0;
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
                 sdata->stack->pattern->name, sdata->stack->deep);
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
        rc = checkElementEnd (sdata);
        if (rc != -1
            || (!sdata->stack->down
                || sdata->stack->deep > sdata->stack->down->deep)) break;
        popStack(sdata);
        DBG(fprintf (stderr, "probe element end again after popping from stack\n");
            serializeStack (sdata));
    }
    
    if (rc != 1) {
        SetResult ("Missing mandatory element");
        sdata->validationState = VALIDATION_ERROR;
        DBG(
            fprintf(stderr, "probeElementEnd: CAN'T end here.\n");
            serializeStack (sdata);
            );
        return TCL_ERROR;
    }

    popStack(sdata);
    DBG(
        fprintf(stderr, "probeElementEnd: ended here.\n");
        serializeStack (sdata);
        );
    if (sdata->stack == NULL) {
        /* End of the first pattern (the tree root) without error.
           We have successfully ended validation */
        sdata->validationState = VALIDATION_FINISHED;
    }

    return TCL_OK;
}

int
probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    )
{
    SchemaCP *cp;
    SchemaValidationStack *se;
    int ac, nm, only_whites;
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

    getContext (cp, ac, nm);

    if (cp->type == SCHEMA_CTYPE_MIXED) {
        se = sdata->stack->down;
        if (hasMatched (se->pattern->quants[se->activeChild], se->nrMatched)) {
            popStack (sdata);
            return probeText (interp, sdata, text);
        }
        se->nrMatched++;
        return TCL_OK;
    }
    while (ac < cp->numChildren) {
        switch (cp->content[ac]->type) {
        case SCHEMA_CTYPE_TEXT:
        case SCHEMA_CTYPE_MIXED: goto foundCP;
        default:
            if (mustMatch (cp->quants[ac], nm)) break;
        }
        ac++;
        nm = 0;
    }
foundCP:
    if (ac < cp->numChildren) {
        if (cp->content[ac]->type == SCHEMA_CTYPE_MIXED) {
            updateStack (sdata->stack, ac, nm+1);
            return TCL_OK;
        }
        if (cp->content[ac]->type == SCHEMA_CTYPE_TEXT) {
            updateStack (sdata->stack, ac, nm+1);
            if (cp->content[ac]->numChildren) {
                if (checkText (interp, cp->content[ac], text) != TCL_OK) {
                    SetResult ("Invalid text content");
                    return TCL_ERROR;
                }
            }
            return TCL_OK;
        }
    }
    /* If we are here then there isn't a matching TEXT cp. Check, if
     * this is white space only between tags. */
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
    SetResult ("Unexpected text content");
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
    char *namespace;
    const char *s;
    int i = 0;
    
    DBG(fprintf (stderr, "startElement: '%s'\n", name);)
    if (Tcl_DStringLength (vdata->cdata)) {
        if (probeText (vdata->interp, vdata->sdata,
                       Tcl_DStringValue (vdata->cdata)) != TCL_OK) {
            vdata->sdata->validationState = VALIDATION_ERROR;
            XML_StopParser (vdata->parser, 0);
            Tcl_DStringSetLength (vdata->cdata, 0);
            return;
        }
        Tcl_DStringSetLength (vdata->cdata, 0);
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

    if (probeElement (vdata->interp, vdata->sdata, s, namespace)
        != TCL_OK) {
        vdata->sdata->validationState = VALIDATION_ERROR;
        XML_StopParser (vdata->parser, 0);
        return;
    }
    if (atts[0] || vdata->sdata->stack->pattern->attrs) {
        if (probeAttributes (vdata->interp, vdata->sdata, atts)
            != TCL_OK) {
            vdata->sdata->validationState = VALIDATION_ERROR;
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
    
    DBG(fprintf (stderr, "endElement: '%s'\n", name);)
    if (Tcl_DStringLength (vdata->cdata)) {
        if (probeText (vdata->interp, vdata->sdata,
                       Tcl_DStringValue (vdata->cdata)) != TCL_OK) {
            vdata->sdata->validationState = VALIDATION_ERROR;
            XML_StopParser (vdata->parser, 0);
            Tcl_DStringSetLength (vdata->cdata, 0);
            return;
        }
        Tcl_DStringSetLength (vdata->cdata, 0);
    }
    if (probeElementEnd (vdata->interp, vdata->sdata)
        != TCL_OK) {
        vdata->sdata->validationState = VALIDATION_ERROR;
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

static int
validateDOM (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domNode    *node
    )
{
    if (probeElement (interp, sdata, node->nodeName,
                      node->namespace ?
                      node->ownerDocument->namespaces[node->namespace-1]->uri
                      : NULL)
        != TCL_OK) {
        return TCL_ERROR;
    } else {
        if (probeDomAttributes (interp, sdata, node->firstAttr) != TCL_OK) {
            return TCL_ERROR;
        }
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
                if (probeText (interp, sdata,
                               ((domTextNode *) node)->nodeValue) != TCL_OK)
                    return TCL_ERROR;
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

static void
schemaReset (
    SchemaData *sdata
    )
{
    while (sdata->stack) popStack (sdata);
    sdata->validationState = VALIDATION_READY;
    sdata->skipDeep = 0;
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
    int            result = TCL_OK, forwardDef = 0, i = 0;
    int            savedDefineToplevel, type, len;
    unsigned int   savedNumPatternList;
    SchemaData  *sdata = (SchemaData *) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    SchemaCP   *pattern, *current = NULL;
    void          *namespacePtr, *savedNamespacePtr;
    char          *xmlstr, *errMsg;
    domDocument   *doc;
    domNode       *node;
    
    static const char *schemaInstanceMethods[] = {
        "defelement", "defpattern", "start", "event", "delete",
        "nrForwardDefinitions", "state", "reset", "define",
        "validate", "domvalidate", NULL
    };
    enum schemaInstanceMethod {
        m_defelement, m_defpattern, m_start, m_event, m_delete,
        m_nrForwardDefinitions, m_state, m_reset, m_define,
        m_validate, m_domvalidate
    };

    static const char *eventKeywords[] = {
        "start", "end", "text", NULL
    };

    enum eventKeyword 
    {
        k_elementstart, k_elementend, k_text
    };
    
    if (objc < 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?arguments?");
        return TCL_ERROR;
    }

    if (sdata == NULL) {
        /* Inline defined defelement, defpattern or start */
        sdata = GETASI;
        CHECK_SI;
        if (!sdata->defineToplevel) {
            SetResult ("Command not allowed in nested schema define script");
            return TCL_ERROR;
        }
        i = 1;
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
            entryPtr = Tcl_CreateHashEntry (&sdata->namespace,
                                            Tcl_GetString (objv[3-i]), &hnew);
            if (entryPtr != sdata->emptyNamespace) {
                namespacePtr = Tcl_GetHashKey (&sdata->namespace,
                                               entryPtr);
            }
        }
        entryPtr = Tcl_CreateHashEntry (hashTable,
                                        Tcl_GetString (objv[2-i]), &hnew);
        pattern = NULL;
        if (!hnew) {
            pattern = (SchemaCP *) Tcl_GetHashValue (entryPtr);
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
                                       Tcl_GetHashKey (hashTable, entryPtr));
            if (!hnew) {
                current = (SchemaCP *) Tcl_GetHashValue (entryPtr);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }

        if (!sdata->defineToplevel) {
            SETASI(sdata);
        }
        savedDefineToplevel = sdata->defineToplevel;
        savedNamespacePtr = sdata->currentNamespace;
        sdata->defineToplevel = 0;
        sdata->currentNamespace = namespacePtr;
        sdata->currentCP = pattern;
        sdata->currentContent = pattern->content;
        sdata->currentQuants = pattern->quants;
        sdata->numChildren = 0;
        sdata->numAttr = 0;
        sdata->numReqAttr = 0;
        sdata->currentAttrs = NULL;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        sdata->evalStub[3] = objv[patternIndex];
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub,
                               TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);
        sdata->currentNamespace = NULL;
        pattern->content = sdata->currentContent;
        pattern->quants = sdata->currentQuants;
        pattern->numChildren = sdata->numChildren;
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
            SETASI(0);
        }
        break;

    case m_define:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<definition commands>");
            return TCL_ERROR;
        }
        SETASI(sdata);
        savedNumPatternList = sdata->numPatternList;
        sdata->currentNamespace = 0;
        sdata->currentCP = NULL;
        sdata->currentContent = NULL;
        sdata->currentQuants = NULL;
        sdata->numChildren = 0;
        sdata->contentSize = 0;
        sdata->defineToplevel = 1;
        sdata->evalStub[3] = objv[2];
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub,
                               TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);
        if (result != TCL_OK) {
            cleanupLastPattern (sdata, savedNumPatternList);
        }
        sdata->defineToplevel = 0;
        SETASI(0);
        break;

    case m_start:
        if (objc < 3-i || objc > 3-i) {
            Tcl_WrongNumArgs (interp, 2-i, objv, "<documentElement>"
                              " ?<namespace>?");
            return TCL_ERROR;
        }
        if (sdata->start) {
            FREE (sdata->start);
        }
        if (objc == 3-i && strcmp (Tcl_GetString (objv[2-i]), "") == 0) {
            if (sdata->startNamespace) {
                FREE (sdata->startNamespace);
            }
            sdata->start = NULL;
            break;
        }
        sdata->start = tdomstrdup (Tcl_GetString (objv[2-i]));
        if (objc == 4-i) {
            if (sdata->startNamespace) {
                FREE (sdata->startNamespace);
            }
            sdata->startNamespace =
                tdomstrdup (Tcl_GetString (objv[3-i]));
        }
        break;

    case m_event:
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
            if (objc == 6) {
                entryPtr = Tcl_FindHashEntry (&sdata->namespace,
                                              Tcl_GetString (objv[5]));
                if (entryPtr && entryPtr != sdata->emptyNamespace) {
                    namespacePtr = Tcl_GetHashKey (&sdata->namespace,
                                                   entryPtr);
                } else {
                    namespacePtr = NULL;
                }
            } else {
                namespacePtr = NULL;
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
        break;

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
        schemaReset (sdata);
        break;

    case m_validate:
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
        
    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        result = TCL_ERROR;
        break;
        
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
        sdata = initSchemaData ();
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


static SchemaQuant *
initSchemaQuant  (
    SchemaData * sdata,
    Schema_Content_Quant quantType,
    int n,
    int m
    )
{
    SchemaQuant * quant;

    quant = TMALLOC (SchemaQuant);
    quant->type = quantType;
    quant->minOccur = n;
    quant->maxOccur = m;
    if (sdata->numQuants == sdata->quantsSize) {
        sdata->quants = REALLOC (
            sdata->quants,
            sizeof (SchemaQuant*) * 2 * sdata->quantsSize
            );
        sdata->quantsSize *= 2;
    }
    sdata->quants[sdata->numQuants] = quant;
    sdata->numQuants++;
    return quant;
}


static SchemaQuant *
getQuant (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *quantObj
    ) 
{
    char *quantStr;
    int len, n, m;
    Tcl_Obj *thisObj;
    
    if (!quantObj) {
        return quantOne;
    }
    quantStr = Tcl_GetStringFromObj (quantObj, &len);
    if (len == 1) {
        switch (quantStr[0]) {
        case '!':
            return quantOne;
        case '*':
            return quantRep;
        case '?':
            return quantOpt;
        case '+':
            return quantPlus;
        }
    }
    if (Tcl_ListObjLength (interp, quantObj, &len) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    if (len != 1 && len != 2) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    if (len == 1) {
        if (Tcl_GetIntFromObj (interp, quantObj, &n) != TCL_OK) {
            SetResult ("Invalid quant specifier");
            return NULL;
        }
        if (n < 1) {
            SetResult ("Invalid quant specifier");
            return NULL;
        }
        if (n == 1) {
            return quantOne;
        }
        return initSchemaQuant (sdata, SCHEMA_CQUANT_NM, n, n);
    }
    /* The "list-ness" of the quantObj is already checked by the
     * Tcl_ListObjLength() call above, no need to check result. */
    Tcl_ListObjIndex (interp, quantObj, 0, &thisObj);
    if (Tcl_GetIntFromObj (interp, thisObj, &n) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    if (n < 0) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    Tcl_ListObjIndex (interp, quantObj, 1, &thisObj);
    if (Tcl_GetIntFromObj (interp, thisObj, &m) != TCL_OK) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    if (n >= m) {
        SetResult ("Invalid quant specifier");
        return NULL;
    }
    if (n == 0 && m == 1) {
        return quantOpt;
    }
    return initSchemaQuant (sdata, SCHEMA_CQUANT_NM, n, m);
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
    SchemaQuant *quant;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,2,"?quant?");
    quant = getQuant (interp, sdata, objc == 1 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }
    if (quant == quantOne
        || quant == quantOpt
        || (quant->type == SCHEMA_CQUANT_NM && (quant->minOccur == quant->maxOccur))) {
        pattern = initSchemaCP (SCHEMA_CTYPE_ANY, NULL, NULL);
        REMEMBER_PATTERN (pattern)
        ADD_TO_CONTENT (pattern, quant)
        return TCL_OK;
    }
    SetResult("The any command allows only the quantifier !, ? and <integer>");
    return TCL_ERROR;
}

static int
evalDefinition (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *definition,
    SchemaCP *pattern,
    SchemaQuant *quant
    )
{
    SchemaCP **savedCurrentContent, *savedCurrentCP;
    SchemaQuant **savedCurrentQuant;
    SchemaAttr **savedCurrentAttrs;
    unsigned int savedNumChildren, savedContenSize, savedNumAttr;
    unsigned int savedAttrSize, savedNumReqAttr;
    int result;

    /* Save some state of sdata .. */
    savedCurrentCP = sdata->currentCP;
    savedCurrentContent = sdata->currentContent;
    savedCurrentQuant = sdata->currentQuants;
    savedNumChildren = sdata->numChildren;
    savedContenSize = sdata->contentSize;
    savedNumAttr = sdata->numAttr;
    savedNumReqAttr = sdata->numReqAttr;
    savedAttrSize = sdata->attrSize;
    savedCurrentAttrs = sdata->currentAttrs;
    /* ... and prepare sdata for definition evaluation. */
    sdata->currentCP = pattern;
    sdata->currentContent = pattern->content;
    sdata->currentQuants = pattern->quants;
    sdata->numChildren = 0;
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
    sdata->numAttr = 0;
    sdata->numReqAttr = 0;
    sdata->currentAttrs = NULL;
    sdata->attrSize = 0;

    result = Tcl_EvalObjEx (interp, definition, TCL_EVAL_DIRECT);

    /* Save the definition evaluation results to the pattern ... */
    pattern->content = sdata->currentContent;
    pattern->quants = sdata->currentQuants;
    pattern->numChildren = sdata->numChildren;
    pattern->attrs = sdata->currentAttrs;
    pattern->numAttr = sdata->numAttr;
    pattern->numReqAttr = sdata->numReqAttr;
    /* ... and restore the previously saved sdata states  */
    sdata->currentCP = savedCurrentCP;
    sdata->currentContent = savedCurrentContent;
    sdata->currentQuants = savedCurrentQuant;
    sdata->numChildren = savedNumChildren;
    sdata->contentSize = savedContenSize;
    sdata->numAttr = savedNumAttr;
    sdata->numReqAttr = savedNumReqAttr;
    sdata->currentAttrs = savedCurrentAttrs;
    sdata->attrSize = savedAttrSize;

    if (result == TCL_OK) {
        REMEMBER_PATTERN (pattern);
        ADD_TO_CONTENT (pattern, quant);
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
    SchemaQuant *quant;
    int hnew;

    CHECK_SI
    CHECK_TOPLEVEL
    if (patternType == SCHEMA_CTYPE_NAME) {
        checkNrArgs (2,4,"Expected: elementName ?quant? ?pattern?");
        hashTable = &sdata->element;
    } else {
        checkNrArgs (2,3,"Expected: patternName ?quant?");
        hashTable = &sdata->pattern;
    }
        
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2]);
    if (!quant) {
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
        ADD_TO_CONTENT (pattern, quant);
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
        return evalDefinition (interp, sdata, objv[3], pattern, quant);
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
    Schema_CP_Type patternType = (Schema_CP_Type) clientData;
    SchemaQuant *quant;
    SchemaCP *pattern;

    CHECK_SI
    CHECK_TOPLEVEL
    if (patternType == SCHEMA_CTYPE_TEXT) {
        checkNrArgs (1,2,"Expected: ?definition?");
        quant = quantOpt;
    } else if (patternType == SCHEMA_CTYPE_MIXED) {
        checkNrArgs (2,3,"Expected: ?quant? definition");
        quant = quantRep;
        if (objc == 3) {
            quant = getQuant (interp, sdata, objv[1]);
        }
    } else {
        checkNrArgs (2,3,"Expected: ?quant? definition");
        quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    }
    if (!quant) {
        return TCL_ERROR;
    }

    pattern = initSchemaCP (patternType, NULL, NULL);

    return evalDefinition (interp, sdata, objc == 2 ? objv[1] : objv[2],
                           pattern, quant);
}

static int
evalConstraints (
    Tcl_Interp *interp,
    SchemaData *sdata,
    SchemaCP *cp,
    Tcl_Obj *script
    )
{
    int result;
    SchemaCP **savedCurrentContent, *savedCurrentCP;
    SchemaQuant **savedCurrentQuant;
    unsigned int savedNumChildren, savedContenSize;

    /* Save some state of sdata .. */
    savedCurrentCP = sdata->currentCP;
    savedCurrentContent = sdata->currentContent;
    savedCurrentQuant = sdata->currentQuants;
    savedNumChildren = sdata->numChildren;
    savedContenSize = sdata->contentSize;
    /* ... and prepare sdata for definition evaluation. */
    sdata->currentCP = cp;
    sdata->currentContent = cp->content;
    sdata->currentQuants = cp->quants;
    sdata->numChildren = 0;
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
    sdata->isTextConstraint = 1;
    sdata->textStub[3] = script;
    result = Tcl_EvalObjv (interp, 4, sdata->textStub,
                           TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);
    sdata->isTextConstraint = 0;
    /* Save the definition evaluation results to the pattern ... */
    cp->content = sdata->currentContent;
    cp->quants = sdata->currentQuants;
    cp->numChildren = sdata->numChildren;
    /* ... and restore the previously saved sdata states  */
    sdata->currentCP = savedCurrentCP;
    sdata->currentContent = savedCurrentContent;
    sdata->currentQuants = savedCurrentQuant;
    sdata->numChildren = savedNumChildren;
    sdata->contentSize = savedContenSize;
    return result;
}

static int maybeAddAttr (
    Tcl_Interp *interp,
    SchemaData *sdata,
    Tcl_Obj *nameObj,
    Tcl_Obj *namespaceObj,
    Tcl_Obj *scriptObj,
    int required
    )
{
    Tcl_HashEntry *h;
    int hnew, hnew1, i, result = TCL_OK;
    char *name, *namespace = NULL;
    SchemaAttr *attr;
    SchemaCP *cp;
        
    if (namespaceObj) {
        h = Tcl_CreateHashEntry (&sdata->namespace,
                                 Tcl_GetString (namespaceObj), &hnew1);
        if (h != sdata->emptyNamespace) {
            namespace = Tcl_GetHashKey (&sdata->namespace, h);
        }
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
        /* TODO */
        cp = initSchemaCP (SCHEMA_CTYPE_TEXT, NULL, NULL);
        REMEMBER_PATTERN (cp)
        cp->content = (SchemaCP**) MALLOC (
            sizeof(SchemaCP*) * CONTENT_ARRAY_SIZE_INIT
            );
        cp->quants = (SchemaQuant**) MALLOC (
            sizeof (SchemaQuant*) * CONTENT_ARRAY_SIZE_INIT
            );        
        result = evalConstraints (interp, sdata, cp, scriptObj);
        attr->cp = cp;
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
    char *quantStr;
    int len, required = 1, scriptIndex, i;
    Tcl_Obj *nsobj;

    CHECK_SI
    CHECK_TOPLEVEL

    if (clientData) {
        checkNrArgs (3,5,"Expected: name namespace"
                     " | name namespace attquant"
                     " | name namespace <constraint script>"
                     " | name namespace attquant <constraint script>");
        i = 1;
        nsobj = objv[2];
    } else {
        checkNrArgs (2,4,"Expected: name"
                     " | name attquant"
                     " | name <constraint script>"
                     " | name attquant <constraint script>");
        i = 0;
        nsobj = NULL;
    }

    if (objc == 2+i) {
        return maybeAddAttr (interp, sdata, objv[1], nsobj, NULL, 1);
    }
    quantStr = Tcl_GetStringFromObj (objv[2+i], &len);
    if (len == 1) {
        if (quantStr[0] == '?') {
            required = 0;
        } else if (quantStr[0] != '!') {
            SetResult ("Invalid attribute quant");
            return TCL_ERROR;
        }
        if (objc == 3+i) {
            return maybeAddAttr (interp, sdata, objv[1], nsobj, NULL,
                                 required);
        }
        scriptIndex = 3+i;
    } else {
        if (objc == 4+i) {
            SetResult ("Invalid attribute quant");
            return TCL_ERROR;
        }
        scriptIndex = 2+i;
    }
    return maybeAddAttr (interp, sdata, objv[1], nsobj,
                         objv[scriptIndex], required);
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
    Tcl_HashEntry *entryPtr;
    int hnew, result;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (3,3,"Expected: namespace pattern");

    currentNamespace = sdata->currentNamespace;
    entryPtr = Tcl_CreateHashEntry (&sdata->namespace,
                                    Tcl_GetString(objv[1]), &hnew);
    if (entryPtr == sdata->emptyNamespace) {
        sdata->currentNamespace = NULL;
    } else {
        sdata->currentNamespace = (char *)
            Tcl_GetHashKey (&sdata->namespace, entryPtr);
    }
    result = Tcl_EvalObjEx (interp, objv[2], TCL_EVAL_DIRECT);
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
    SchemaCP *pattern;
    
    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,2,"?<definition script>?");
    pattern = initSchemaCP (SCHEMA_CTYPE_TEXT, NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantOne)
    if (objc == 2) {
        pattern->content = (SchemaCP**) MALLOC (
            sizeof(SchemaCP*) * CONTENT_ARRAY_SIZE_INIT
            );
        pattern->quants = (SchemaQuant**) MALLOC (
            sizeof (SchemaQuant*) * CONTENT_ARRAY_SIZE_INIT
            );        
        return evalConstraints (interp, sdata, pattern, objv[1]);
    }
    return TCL_OK;
}

static int 
isintImpl (
    Tcl_Interp *interp,
    void *constraintData,
    char *text
    )
{
    int n;
    
    if (Tcl_GetInt (interp, text, &n) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int
isintTCObjCmd (
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
    sc->constraint = isintImpl;
    return TCL_OK;
}

typedef struct 
{
    int nrArg;
    Tcl_Obj **evalStub;
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
    result = Tcl_EvalObjv (interp, tcdata->nrArg, tcdata->evalStub,
                           TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount (tcdata->evalStub[tcdata->nrArg-1]);
    if (result != TCL_OK) {
        return TCL_ERROR;
    }
    result = Tcl_GetBooleanFromObj (interp, Tcl_GetObjResult (interp), &bool);
    if (result != TCL_OK) {
        return TCL_ERROR;
    }
    if (bool) {
        return TCL_OK;
    } else {
        return TCL_ERROR;
    }
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
        return TCL_OK;
    }
    return TCL_ERROR;
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

    if (Tcl_FindHashEntry(values, text)) return TCL_OK;
    return TCL_ERROR;
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

void
tDOM_SchemaInit (
    Tcl_Interp *interp
    )
{
    memset (quantOne, 0, sizeof (SchemaQuant));
    quantOne->type = SCHEMA_CQUANT_ONE;
    memset (quantOpt, 0, sizeof (SchemaQuant));
    quantOpt->type = SCHEMA_CQUANT_OPT;
    memset (quantRep, 0, sizeof (SchemaQuant));
    quantRep->type = SCHEMA_CQUANT_REP;
    memset (quantPlus, 0, sizeof (SchemaQuant));
    quantPlus->type = SCHEMA_CQUANT_PLUS;

    /* Inline definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::defelement",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::defpattern",
                          schemaInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::start",
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
                          AnonPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_CHOICE, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::mixed",
                          AnonPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_MIXED, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::interleave",
                          AnonPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_INTERLEAVE, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::group",
                          AnonPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_GROUP, NULL);
    
    /* The "attribute", "nsattribute", "namespace" and "text" definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::nsattribute",
                          AttributePatternObjCmd, (ClientData) 1, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text",
                          TextPatternObjCmd, NULL, NULL);

    /* The text constraint commands */
    Tcl_CreateObjCommand (interp, "tdom::schema::text::isint",
                          isintTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::tcl",
                          tclTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::fixed",
                          fixedTCObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text::enumeration",
                          enumerationTCObjCmd, NULL, NULL);

}

#endif  /* #ifndef TDOM_NO_SCHEMA */
