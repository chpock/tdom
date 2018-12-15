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
|   November 2018
|
\---------------------------------------------------------------------------*/

#ifndef TDOM_NO_SCHEMA

#include <tdom.h>
#include <schema.h>

/* #define DEBUG */
/*----------------------------------------------------------------------------
|   Debug Macros
|
\---------------------------------------------------------------------------*/
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x) 
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

#ifdef DEBUG
static char *Schema_CP_Type2str[] = {
    "EMPTY",
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
    "N",
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
        SetResult ("Command called outside of grammar context.");       \
        return TCL_ERROR;                                               \
    }

#define CHECK_TOPLEVEL                                                  \
    if (sdata->defineToplevel) {                                        \
        SetResult("Command not allowed at top level "                   \
                  "in grammar define evaluation");                      \
        return TCL_ERROR;                                               \
    }

#define CHECK_SI_CONTEXT                                                \
    if (sdata->isAttribute) {                                           \
        SetResult ("Command called in invalid grammar context.");       \
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

#define REMEMBER_PATTERN(pattern)                                       \
    if (sdata->numPatternList == sdata->patternListSize) {              \
        sdata->patternList = (SchemaCP **) MALLOC (                     \
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
    case SCHEMA_CTYPE_EMPTY:
    case SCHEMA_CTYPE_ANY:
    case SCHEMA_CTYPE_TEXT:
        /* Do nothing */
        break;
    }
    return pattern;
}

DBG(
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
    case SCHEMA_CTYPE_EMPTY:
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
    switch (pattern->type) {
    case SCHEMA_CTYPE_EMPTY:
    case SCHEMA_CTYPE_ANY:
        /* do nothing */
        break;
    default:
        FREE (pattern->content);
        FREE (pattern->quants);
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
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    sdata->emptyNamespace = Tcl_CreateHashEntry (
        &sdata->namespace, "", &hnew);
    sdata->patternList = (SchemaCP **) MALLOC (
        sizeof (SchemaCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->patternListSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    sdata->quants = (SchemaQuant **) MALLOC (
        sizeof (SchemaQuant*) * QUANTS_ARRAY_SIZE_INIT);
    sdata->quantsSize = QUANTS_ARRAY_SIZE_INIT;
    sdata->evalStub = (Tcl_Obj **) (MALLOC (sizeof (Tcl_Obj*) * 4));
    sdata->evalStub[0] = Tcl_NewStringObj("::namespace", 11);
    Tcl_IncrRefCount (sdata->evalStub[0]);
    sdata->evalStub[1] = Tcl_NewStringObj("eval", 4);
    Tcl_IncrRefCount (sdata->evalStub[1]);
    sdata->evalStub[2] = Tcl_NewStringObj("::tdom::schema", 17);
    Tcl_IncrRefCount (sdata->evalStub[2]);
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
        : (quant->type == SCHEMA_CQUANT_NM && quant->minOccur < (nr)) ? 1 : 0

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

static int
matchElementStart (
    SchemaData *sdata,
    char *name,
    char *namespace
    )
{
    SchemaCP *cp, *candidate;
    int nm, ac, i;
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
                case SCHEMA_CTYPE_EMPTY:
                    break;

                case SCHEMA_CTYPE_ANY:
                    updateStack (se, ac, nm+1);
                    sdata->skipDeep = 1;
                    return 1;
                
                case SCHEMA_CTYPE_NAME:
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
        case SCHEMA_CTYPE_EMPTY:
            /* Never pushed onto stack */
            Tcl_Panic ("Invalid CTYPE onto the validation stack!");

        case SCHEMA_CTYPE_INTERLEAVE:
            fprintf (stderr, "matchElementStart: SCHEMA_CTYPE_INTERLEAVE to be implemented\n");
            return 0;

        case SCHEMA_CTYPE_MIXED:
        case SCHEMA_CTYPE_CHOICE:
            for (i = 0; i < cp->numChildren; i++) {
                candidate = cp->content[i];
                switch (candidate->type) {
                case SCHEMA_CTYPE_TEXT:
                case SCHEMA_CTYPE_EMPTY:
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
                        se->nrMatched = 1;
                        pushToStack (sdata, candidate, deep + 1);
                        return 1;
                    }
                case SCHEMA_CTYPE_GROUP:
                case SCHEMA_CTYPE_PATTERN:
                case SCHEMA_CTYPE_MIXED:
                case SCHEMA_CTYPE_INTERLEAVE:
                case SCHEMA_CTYPE_CHOICE:
                    pushToStack (sdata, candidate, deep);
                    if (matchElementStart (sdata, name, namespace)) {
                        /* Matched */
                        se->activeChild = i;
                        se->nrMatched = 1;
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
                SetResult ("Root element doesn't match.");
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
        pattern = (SchemaCP *) Tcl_GetHashValue (entryPtr);
        while (pattern) {
            if (pattern->namespace == namespacePtr) {
                break;
            }
            pattern = pattern->next;
        }
        if (!pattern) {
            SetResult ("Root element doesn't match.");
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
    SetResult3 ("Element \"", name, "\" doesn't match");
    return TCL_ERROR;
}

static int checkElementEnd (
    SchemaData *sdata
    )
{
    SchemaCP *parent;
    int nm, ac;
    int isName = 0;
    
    getContext (parent, ac, nm);
    
    if (ac >= parent->numChildren) {
        if (parent->type == SCHEMA_CTYPE_NAME) return 1;
        else return -1;
    }
    if (hasMatched (parent->quants[ac], nm)) {ac++; nm++;}
    switch (parent->type) {
    case SCHEMA_CTYPE_NAME:
        /* if (!sdata->stack->down) return 1; */
        isName = 1;
        /* fall through */
    case SCHEMA_CTYPE_GROUP:
    case SCHEMA_CTYPE_PATTERN:
        while (ac < parent->numChildren) {
            if (mustMatch (parent->quants[ac], nm)) {
                return 0;
            }
            ac ++;
            nm = 0;
        }
        if (isName) return 1;
        else return -1;

    case SCHEMA_CTYPE_TEXT:
    case SCHEMA_CTYPE_ANY:
    case SCHEMA_CTYPE_EMPTY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
        return 0;

    case SCHEMA_CTYPE_INTERLEAVE:
        fprintf (stderr, "checkElementEnd: SCHEMA_CTYPE_INTERLEAVE to be implemented\n");
        return 0;
        
    case SCHEMA_CTYPE_MIXED:
    case SCHEMA_CTYPE_CHOICE:
        return -1;
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
        serializeStack (sdata);
        );
    
    if (sdata->skipDeep) {
        sdata->skipDeep--;
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }

    rc = checkElementEnd (sdata);
    while (rc == -1) {
        if (!sdata->stack->down
            || sdata->stack->deep > sdata->stack->down->deep) {
            break;
        }
        popStack(sdata);
        rc = checkElementEnd (sdata);
    }

    if (rc != 1) {
        SetResult ("Missing mandatory element\n");
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

static int
checkText (
    Tcl_Interp *interp,
    SchemaCP *cp,
    char *text
    )
{
    
    return TCL_OK;
}

int
probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    )
{
    SchemaCP *parent;
    int ac, nm, only_whites;
    char *pc;

    DBG(fprintf (stderr, "probeText started, text: '%s'\n", text);)
    if (sdata->skipDeep) {
        return TCL_OK;
    }
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }

    getContext (parent, ac, nm);

    while (ac < parent->numChildren) {
        if (parent->content[ac]->type == SCHEMA_CTYPE_TEXT
            || mustMatch (parent->quants[ac], nm)) break;
        ac++;
        nm = 0;
    }
    if (ac < parent->numChildren) {
        if (parent->content[ac]->type == SCHEMA_CTYPE_TEXT) {
            return checkText (interp, parent->content[ac], text);
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
            XML_StopParser (vdata->parser, 0);
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
        XML_StopParser (vdata->parser, 0);
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
            XML_StopParser (vdata->parser, 0);
        }
        Tcl_DStringSetLength (vdata->cdata, 0);
    }
    if (probeElementEnd (vdata->interp, vdata->sdata)
        != TCL_OK) {
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
        Tcl_AppendStringsToObj (resultObj, "error \"",
                                XML_ErrorString(XML_GetErrorCode(parser)),
                                "\" at line ", sl, " character ", sc, NULL);
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
schemaReset (
    SchemaData *sdata
    )
{
    sdata->stack = NULL;
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
    char          *xmlstr;
    
    static const char *schemaInstanceMethods[] = {
        "defelement", "defpattern", "start", "event", "delete",
        "nrForwardDefinitions", "state", "reset", "define",
        "validate", NULL
    };
    enum schemaInstanceMethod {
        m_defelement, m_defpattern, m_start, m_event, m_delete,
        m_nrForwardDefinitions, m_state, m_reset, m_define,
        m_validate
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
                                   "in this namespace.");
                    } else {
                        SetResult ("Pattern already defined "
                                   "in this namespace.");
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
        sdata->currentContent = pattern->content;
        sdata->currentQuants = pattern->quants;
        sdata->numChildren = 0;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        sdata->evalStub[3] = objv[patternIndex];
        result = Tcl_EvalObjv (interp, 4, sdata->evalStub,
                               TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);
        sdata->currentNamespace = NULL;
        pattern->content = sdata->currentContent;
        pattern->quants = sdata->currentQuants;
        pattern->numChildren = sdata->numChildren;
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
            SetResult ("Internal error: Invalid validation state.");
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
    int            methodIndex, result = TCL_OK;
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
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], schemaMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
    }
        
    Tcl_ResetResult (interp);
    switch ((enum schemaMethod) methodIndex) {
    case m_create:
        sdata = initSchemaData ();
        Tcl_CreateObjCommand (interp, Tcl_GetString(objv[2]),
                              schemaInstanceCmd, 
                              (ClientData) sdata,
                              schemaInstanceDelete);
        Tcl_SetObjResult (interp, objv[2]);
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
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    if (len != 1 && len != 2) {
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    if (len == 1) {
        if (Tcl_GetIntFromObj (interp, quantObj, &n) != TCL_OK) {
            SetResult ("Invalid quant specifier.");
            return NULL;
        }
        if (n < 1) {
            SetResult ("Invalid quant specifier.");
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
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    if (n < 0) {
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    Tcl_ListObjIndex (interp, quantObj, 1, &thisObj);
    if (Tcl_GetIntFromObj (interp, thisObj, &m) != TCL_OK) {
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    if (n >= m) {
        SetResult ("Invalid quant specifier.");
        return NULL;
    }
    if (n == 0 && m == 1) {
        return quantOpt;
    }
    return initSchemaQuant (sdata, SCHEMA_CQUANT_NM, n, m);
}

/* Implements the grammar definition commands "empty" and "any" */
static int
EmptyAnyPatternObjCmd (
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
    checkNrArgs (1,1,"No arguments expected.");
    pattern = initSchemaCP ((Schema_CP_Type) clientData,
                               NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantRep)
    return TCL_OK;
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
    SchemaCP **savedCurrentContent;
    SchemaQuant **savedCurrentQuant;
    unsigned int savedNumChildren, savedContenSize;
    int result;

    /* Save some state of sdata .. */
    savedCurrentContent = sdata->currentContent;
    savedCurrentQuant = sdata->currentQuants;
    savedNumChildren = sdata->numChildren;
    savedContenSize = sdata->contentSize;
    /* ... and prepare sdata for definition evaluation. */
    sdata->currentContent = pattern->content;
    sdata->currentQuants = pattern->quants;
    sdata->numChildren = 0;
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;

    result = Tcl_EvalObjEx (interp, definition, TCL_EVAL_DIRECT);

    /* Save the definition evaluation results to the pattern ... */
    pattern->content = sdata->currentContent;
    pattern->quants = sdata->currentQuants;
    pattern->numChildren = sdata->numChildren;
    /* ... and restore the previously saved sdata states  */
    sdata->currentContent = savedCurrentContent;
    sdata->currentQuants = savedCurrentQuant;
    sdata->numChildren = savedNumChildren;
    sdata->contentSize = savedContenSize;

    if (result == TCL_OK) {
        REMEMBER_PATTERN (pattern);
        ADD_TO_CONTENT (pattern, quant);
    } else {
        freeSchemaCP (pattern);
    }
    return result;
}

/* Implements the grammar definition commands "element" and "ref" */
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

/* Implements the grammar definition commands "choice", "group",
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
    checkNrArgs (2,3,"Expected: ?quant? definition");
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }

    pattern = initSchemaCP (patternType, NULL, NULL);

    return evalDefinition (interp, sdata, objc == 2 ? objv[1] : objv[2],
                           pattern, quant);
}

static int
AttributePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
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
                                    objv[1], &hnew);
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
    checkNrArgs (1,1,"No arguments expected.");
    pattern = initSchemaCP ((Schema_CP_Type) clientData,
                               NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantOne)
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
    
    /* The "empty" and "any" definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::empty",
                          EmptyAnyPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_EMPTY, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::any",
                          EmptyAnyPatternObjCmd,
                          (ClientData) SCHEMA_CTYPE_ANY, NULL);

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
    
    /* The "attribute", "namespace" and "text" definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::schema::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::schema::text",
                          TextPatternObjCmd, NULL, NULL);
}

# else  /* #ifndef TDOM_NO_SCHEMA */
int 
schemaInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    return TCL_OK;
}

int
probeElement (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *name,
    void *namespace
    )
{
    return TCL_OK;
}

int
probeElementEnd (
    Tcl_Interp * interp,
    SchemaData *sdata
    )
{
    return TCL_OK;
}

int
probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    )
{
    return TCL_OK;
}

#endif  /* #ifndef TDOM_NO_SCHEMA */
