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

#ifndef TDOM_NO_STRUCTURE

#include <tdom.h>
#include <structure.h>

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

#define TMALLOC(t) (t*)MALLOC(sizeof(t))

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)
#define SetIntResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetIntObj(Tcl_GetObjResult(interp), (i))

#define checkNrArgs(l,h,err) if (objc < l || objc > h) {      \
        SetResult (err);                                      \
        return TCL_ERROR;                                     \
    }

#ifdef DEBUG
static char *Structure_CP_Type2str[] = {
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
static char *Structure_Quant_Type2str[] = {
    "ONE",
    "OPT",
    "REP",
    "PLUS",
    "N",
    "NM"
};
#endif


/* The StructureFlags flags */
#define FORWARD_PATTERN_DEF     1
#define PLACEHOLDER_PATTERN_DEF 2
#define AMBIGUOUS_PATTERN       4
#define LOCAL_DEFINED_ELEMENT  8

/* Pointer to heap-allocated shared quants. */
static StructureQuant QuantOne;
static StructureQuant *quantOne = &QuantOne;

static StructureQuant QuantOpt;
static StructureQuant *quantOpt = &QuantOpt;

static StructureQuant QuantRep;
static StructureQuant *quantRep = &QuantRep;

static StructureQuant QuantPlus;
static StructureQuant *quantPlus = &QuantPlus;

#ifndef TCL_THREADS
  static StructureData *activeStructureData = 0;
# define GETASI activeStructureData
# define SETASI(v) activeStructureData = v
#else
  static Tcl_ThreadDataKey activeStructureData;
# define GETASI  *(StructureData**) Tcl_GetThreadData(&activeStructureData, \
                                                     sizeof(StructureData*))
static void SetActiveStructureData (StructureData *v) 
{
    StructureData **structureInfoPtr = Tcl_GetThreadData(&activeStructureData,
                                                        sizeof (StructureData*));
    *structureInfoPtr = v;
}
# define SETASI(v) SetActiveStructureData (v)
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
                     * sizeof (StructureCP*));                          \
        sdata->currentQuants =                                          \
            REALLOC (sdata->currentQuants,                              \
                     2 * sdata->contentSize                             \
                     * sizeof (StructureQuant*));                       \
        sdata->contentSize *= 2;                                        \
    }                                                                   \
    sdata->currentContent[sdata->numChildren] = (pattern);              \
    sdata->currentQuants[sdata->numChildren] = quant;                   \
    sdata->numChildren++;                                               \

#define REMEMBER_PATTERN(pattern)                                       \
    if (sdata->numPatternList == sdata->patternListSize) {              \
        sdata->patternList = (StructureCP **) MALLOC (                  \
            sizeof (StructureCP*) * sdata->patternListSize * 2);        \
        sdata->patternListSize *= 2;                                    \
    }                                                                   \
    sdata->patternList[sdata->numPatternList] = pattern;                \
    sdata->numPatternList++;

static StructureCP*
initStructureCP (
    Structure_CP_Type type,
    void *namespace,
    char *name
    )
{
    StructureCP *pattern;

    pattern = TMALLOC (StructureCP);
    memset (pattern, 0, sizeof(StructureCP));
    pattern->type = type;
    switch (type) {
    case STRUCTURE_CTYPE_NAME:
    case STRUCTURE_CTYPE_PATTERN:
        pattern->namespace = (char *)namespace;
        pattern->name = name;
        /* Fall thru. */
    case STRUCTURE_CTYPE_GROUP:
    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
    case STRUCTURE_CTYPE_INTERLEAVE:
        pattern->content = (StructureCP**) MALLOC (
            sizeof(StructureCP*) * CONTENT_ARRAY_SIZE_INIT
            );
        pattern->quants = (StructureQuant**) MALLOC (
            sizeof (StructureQuant*) * CONTENT_ARRAY_SIZE_INIT
            );
        break;
    case STRUCTURE_CTYPE_EMPTY:
    case STRUCTURE_CTYPE_ANY:
    case STRUCTURE_CTYPE_TEXT:
        /* Do nothing */
        break;
    }
    return pattern;
}

DBG(
static void serializeCP (
    StructureCP *pattern
    )
{
    fprintf (stderr, "CP type: %s\n",
             Structure_CP_Type2str[pattern->type]);
    switch (pattern->type) {
    case STRUCTURE_CTYPE_NAME:
    case STRUCTURE_CTYPE_PATTERN:
    case STRUCTURE_CTYPE_GROUP:
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
    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
    case STRUCTURE_CTYPE_INTERLEAVE:
        fprintf (stderr, "\t%d childs\n", pattern->numChildren);
        break;
    case STRUCTURE_CTYPE_EMPTY:
    case STRUCTURE_CTYPE_ANY:
    case STRUCTURE_CTYPE_TEXT:
        /* Do nothing */
        break;
    }
}

static void serializeQuant (
    StructureQuant *quant
    )
{
    fprintf (stderr, "Quant type: %s n: %d m: %d\n",
             Structure_Quant_Type2str[quant->type], quant->minOccur, quant->maxOccur);
}

static void serializeStack (
    StructureData *sdata
    ) 
{
    int i = sdata->stackPtr-1;

    fprintf (stderr, "++++ Current validation stack (size %d):\n", i+1);
    while (i >= 0) {
        serializeCP (sdata->stack[i]->pattern);
        fprintf (stderr, "deep: %d ac: %d nm: %d\n", sdata->stack[i]->deep,
                 sdata->stack[i]->activeChild, sdata->stack[i]->nrMatched);
        i--;
    }
    fprintf (stderr, "++++ Stack bottom\n");
}
)

/* DBG end */

static void freeStructureCP (
    StructureCP *pattern
    )
{
    switch (pattern->type) {
    case STRUCTURE_CTYPE_EMPTY:
    case STRUCTURE_CTYPE_ANY:
        /* do nothing */
        break;
    default:
        FREE (pattern->content);
        FREE (pattern->quants);
        break;
    }
    FREE (pattern);
}

static StructureData*
initStructureData () 
{
    StructureData *sdata;
    int hnew;
    
    sdata = TMALLOC (StructureData);
    memset (sdata, 0, sizeof(StructureData));
    Tcl_InitHashTable (&sdata->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    sdata->emptyNamespace = Tcl_CreateHashEntry (
        &sdata->namespace, "", &hnew);
    sdata->patternList = (StructureCP **) MALLOC (
        sizeof (StructureCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->patternListSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    sdata->quants = (StructureQuant **) MALLOC (
        sizeof (StructureQuant*) * QUANTS_ARRAY_SIZE_INIT);
    sdata->quantsSize = QUANTS_ARRAY_SIZE_INIT;
    sdata->stack = (StructureValidationStack **) MALLOC (
        sizeof (StructureValidationStack *) * STACK_SIZE_INIT);
    sdata->stackSize = STACK_SIZE_INIT;
    sdata->stackList = (StructureValidationStack **) MALLOC (
        sizeof (StructureValidationStack *) * STACK_LIST_SIZE_INIT);
    sdata->stackListSize = STACK_LIST_SIZE_INIT;
    return sdata;
}

static void structureInstanceDelete (
    ClientData clientData
    )
{
    StructureData *sdata = (StructureData *) clientData;
    unsigned int i;

    if (sdata->start) FREE (sdata->start);
    if (sdata->startNamespace) FREE (sdata->startNamespace);
    Tcl_DeleteHashTable (&sdata->namespace);
    Tcl_DeleteHashTable (&sdata->element);
    Tcl_DeleteHashTable (&sdata->pattern);
    for (i = 0; i < sdata->numPatternList; i++) {
        freeStructureCP (sdata->patternList[i]);
    }
    FREE (sdata->patternList);
    for (i = 0; i < sdata->numQuants; i++) {
        FREE (sdata->quants[i]);
    }
    FREE (sdata->quants);
    FREE (sdata->stack);
    for (i = 0; i < sdata->numStackAllocated; i++) {
        FREE (sdata->stackList[i]);
    }
    FREE (sdata->stackList);
    FREE (sdata);
}

static void
cleanupLastPattern (
    StructureData *sdata,
    unsigned int from
    )
{
    unsigned int i;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    StructureCP *this, *previous, *current;
    
    for (i = from; i < sdata->numPatternList; i++) {
        this = sdata->patternList[i];
        hashTable = NULL;
        if (this->type == STRUCTURE_CTYPE_NAME) {
            hashTable = &sdata->element;
        }
        if (this->type == STRUCTURE_CTYPE_PATTERN) {
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
                if (current->next) {
                    Tcl_SetHashValue (entryPtr, current->next);
                } else {
                    Tcl_DeleteHashEntry (entryPtr);
                }
            }
        }
        freeStructureCP (sdata->patternList[i]);
    }
    sdata->numPatternList = from;
}

static void
checkForAmbiguousness (
    StructureCP *pattern
    )
{
    /* As long as we don't know otherwise we assume any pattern to be
     * ambiguous. */
    pattern->flags |= AMBIGUOUS_PATTERN;
}

static void
pushToStack (
    StructureData *sdata,
    StructureCP *pattern,
    int deep
    )
{
    StructureValidationStack *stackElm;

    if (sdata->numStackList < sdata->numStackAllocated) {
        stackElm = sdata->stackList[sdata->numStackList];
    } else {
        if (sdata->stackPtr == sdata->stackSize) {
            sdata->stack = REALLOC (
                sdata->stack,
                sizeof (StructureValidationStack *) * 2 * sdata->stackSize);
            sdata->stackSize *= 2;
        }
        if (sdata->stackListSize == sdata->numStackList) {
            sdata->stackList = REALLOC (
                sdata->stackList,
                sizeof (StructureValidationStack *) * 2 * sdata->stackListSize);
            sdata->stackListSize *= 2;
        }
        stackElm = TMALLOC (StructureValidationStack);
        sdata->numStackAllocated++;
        sdata->stackList[sdata->numStackList] = stackElm;
    }
    sdata->numStackList++;
    memset (stackElm, 0, sizeof (StructureValidationStack));
    stackElm->pattern = pattern;
    stackElm->deep = deep;
    sdata->stack[sdata->stackPtr] = stackElm;
    sdata->stackPtr++;
}

#define maxOne(quant) \
    ((quant) == quantOne || (quant) == quantOpt) ? 1 : 0

#define minOne(quant) \
    ((quant) == quantOne || (quant) == quantPlus) || ((quant->type == STRUCTURE_CQUANT_N || quant->type == STRUCTURE_CQUANT_NM) && (quant)->minOccur > 0)  ? 1 : 0

#define mayMiss(quant) \
    ((quant) == quantOpt || (quant) == quantRep) || (quant->type == STRUCTURE_CQUANT_NM && (quant)->minOccur == 0) ? 1 : 0

#define mayRepeat(quant) \
    ((quant) == quantRep || (quant) == quantPlus) ? 1 : 0

#define mustMatch(quant,nr) \
    (nr) == 0 ? minOne(quant)                                              \
        : (quant->type == STRUCTURE_CQUANT_N || quant->type == STRUCTURE_CQUANT_NM) && (quant->minOccur < (nr)) ? 1 : 0

#define getContext(parent, ac, nm) \
    parent = sdata->stack[sdata->stackPtr-1]->pattern;   \
    ac = sdata->stack[sdata->stackPtr-1]->activeChild;   \
    nm = sdata->stack[sdata->stackPtr-1]->nrMatched;

#define getContext2(parent2, ac2, nm2)                       \
    parent = sdata->stack[sdata->stackPtr-2]->pattern;   \
    ac = sdata->stack[sdata->stackPtr-2]->activeChild;   \
    nm = sdata->stack[sdata->stackPtr-2]->nrMatched;


static void
updateStack (
    StructureData *sdata,
    int stackPtr,
    int newac,
    int newnm
    )
{
    StructureCP *parent = sdata->stack[stackPtr-1]->pattern;
    StructureQuant *quant =  parent->quants[newac];

    switch (parent->type) {
    case STRUCTURE_CTYPE_NAME:
    case STRUCTURE_CTYPE_GROUP:
    case STRUCTURE_CTYPE_PATTERN:
        if (mayRepeat(quant)) break;
        if (maxOne(quant)) {
            newac++;
            newnm = 0;
            break;
        } else {
            if (quant->type == STRUCTURE_CQUANT_N) {
                if (quant->minOccur == newnm) {
                    newac++;
                    newnm = 0;
                }
            } else {
                if (quant->maxOccur == newnm) {
                    newac++;
                    newnm = 0;
                }
            }
        }
        break;
        
    case STRUCTURE_CTYPE_TEXT:
    case STRUCTURE_CTYPE_ANY:
    case STRUCTURE_CTYPE_EMPTY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
        return;

    case STRUCTURE_CTYPE_INTERLEAVE:
        fprintf (stderr, "updateStack: STRUCTURE_CTYPE_INTERLEAVE to be implemented\n");
        return;

    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
        break;
    }
    DBG(fprintf (stderr, "updateStack: updating %d, ac: %d, nm: %d\n",
                 sdata->stackPtr-1, newac, newnm));
    sdata->stack[sdata->stackPtr-1]->activeChild = newac;
    sdata->stack[sdata->stackPtr-1]->nrMatched = newnm;
}

#define popStack(sdata) sdata->stackPtr--

#define waterMark(sdata) sdata->stack[sdata->stackPtr-1]->stacklistWatermark = sdata->numStackList - 1
#define restoreWaterMark(sdata) sdata->numStackList = sdata->stack[sdata->stackPtr-1]->stacklistWatermark

static int
matchNamePattern (
    StructureData *sdata,
    StructureCP *pattern,
    int currentDeep
    )
{
    StructureCP *parent, *candidate;
    int nm, ac, startac, savedStackPtr, rc, i;
    int isName = 0;
    
    /* The caller must ensure pattern->type = STRUCTURE_CTYPE_NAME */

    getContext (parent, ac, nm);

    switch (parent->type) {
    case STRUCTURE_CTYPE_NAME:
        isName = 1;
        /* fall through */
    case STRUCTURE_CTYPE_GROUP:
    case STRUCTURE_CTYPE_PATTERN:
        startac = ac;
    loopOverContent:
        while (ac < parent->numChildren) {
            candidate = parent->content[ac];

            switch (candidate->type) {
            case STRUCTURE_CTYPE_EMPTY:
                /* The empty pattern never match an element */
                ac++;
                nm = 0;
                break;

            case STRUCTURE_CTYPE_ANY:
                updateStack (sdata, sdata->stackPtr, ac, nm+1);
                return 1;
                
            case STRUCTURE_CTYPE_NAME:
                if (candidate == pattern
                    || strcmp (candidate->name, pattern->name) == 0) {
                    updateStack (sdata, sdata->stackPtr, ac, nm+1);
                    /* We need to push the element only onto the stack
                     * if it has non-empty content. But how many real
                     * life XML vocabularies does have EMPTY
                     * elements? Should we check for this? */
                    pushToStack (sdata, candidate, currentDeep + 1);
                    waterMark (sdata);
                    return 1;
                }
                if (mustMatch (parent->quants[ac], nm)) return 0;
                ac++;
                nm = 0;
                break;
                
            case STRUCTURE_CTYPE_TEXT:
                if (mustMatch (parent->quants[ac], nm)) return 0;
                ac++;
                nm = 0;
                break;
                
            case STRUCTURE_CTYPE_GROUP:
            case STRUCTURE_CTYPE_PATTERN:
            case STRUCTURE_CTYPE_MIXED:
            case STRUCTURE_CTYPE_INTERLEAVE:
            case STRUCTURE_CTYPE_CHOICE:
                savedStackPtr = sdata->stackPtr;
                pushToStack (sdata, candidate, currentDeep);
                rc = matchNamePattern (sdata, pattern, currentDeep);
                if (rc == 1) return 1;
                popStack (sdata);
                if (rc == 0) return 0;
                if (mustMatch (parent->quants[ac], nm)) return 0;
                ac++;
                nm = 0;
                break;
            }
        }
        if (isName) {
            /* No match, but any remaining possible childs tested */
            return 0;
        } else {
            /* No match, but also no explicit error. */
            /* We finished a non atomic pattern (GROUP or PATTERN) and
             * increment the nrMatched of that inside its parent. */
            sdata->stack[sdata->stackPtr-2]->nrMatched += 1;
            
            /* Check, if we have to restart the parent pattern (look
               for match right from the start) because of quant. */
            if (startac) {
                /* It only make sense to look for a match from the
                 * start if the start active child wasn't already 0*/
                StructureQuant *parentQuant;
                parentQuant = sdata->stack[sdata->stackPtr-2]->
                    pattern->quants[sdata->stack[sdata->stackPtr-2]->activeChild];
                if (mayRepeat (parentQuant)) {
                    ac = 0;
                    nm = 0;
                    startac = 0;
                    DBG(fprintf (stderr, "... loopOverContent\n"));
                    goto loopOverContent;
                }
                if (!maxOne (parentQuant)) {
                    if (sdata->stack[sdata->stackPtr-2]->nrMatched < parentQuant->maxOccur) {
                        ac = 0;
                        nm = 0;
                        startac = 0;
                        goto loopOverContent;
                    }
                }
            }
            sdata->stack[sdata->stackPtr-2]->activeChild += 1;
            sdata->stack[sdata->stackPtr-2]->nrMatched = 0;
            return -1;
        }
        
    case STRUCTURE_CTYPE_TEXT:
    case STRUCTURE_CTYPE_ANY:
    case STRUCTURE_CTYPE_EMPTY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");

    case STRUCTURE_CTYPE_INTERLEAVE:
        fprintf (stderr, "matchNamePattern: STRUCTURE_CTYPE_INTERLEAVE to be implemented\n");
        return 0;

    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
        for (i = 0; i < pattern->numChildren; i++) {
            candidate = parent->content[ac];

            switch (candidate->type) {
            case STRUCTURE_CTYPE_EMPTY:
                /* The empty pattern never match an element */
                break;

            case STRUCTURE_CTYPE_ANY:
                return 1;
                
            case STRUCTURE_CTYPE_TEXT:
                /* An elememt never match text. */
                break;
                
            case STRUCTURE_CTYPE_NAME:
                if (candidate == pattern) {
                    updateStack (sdata, sdata->stackPtr, 0, nm+1);
                    pushToStack (sdata, candidate, currentDeep + 1);
                    return 1;
                }
                break;
                
            case STRUCTURE_CTYPE_GROUP:
            case STRUCTURE_CTYPE_PATTERN:
            case STRUCTURE_CTYPE_MIXED:
            case STRUCTURE_CTYPE_INTERLEAVE:
            case STRUCTURE_CTYPE_CHOICE:
                savedStackPtr = sdata->stackPtr;
                pushToStack (sdata, candidate, currentDeep);
                rc = matchNamePattern (sdata, pattern, currentDeep);
                if (rc == 1) {
                    /* Matched */
                    updateStack (sdata, savedStackPtr, 0, nm+1);
                    return 1;
                }
                popStack (sdata);
                break;
            }
            
        }
        /* No match, but the pattern may be optional or has already
         * matched often enough. Check this on caller level. */
        return -1;
    }
    return 0;
}

int
probeElement (
    Tcl_Interp *interp,
    StructureData *sdata,
    const char *name,
    void *namespace
    ) 
{
    Tcl_HashEntry *entryPtr;
    void *namespacePtr;
    StructureCP *pattern;
    int savedStackPtr, activeStack, currentDeep, rc;

    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
            
    if (namespace) {
        entryPtr = Tcl_FindHashEntry (&sdata->namespace, (char *)namespace);
        if (!entryPtr) {
            SetResult ("No elements defined in this namespace.");
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
    if (!entryPtr) {
        SetResult ("No such element name in grammar.");
        return TCL_ERROR;
    }
    pattern = (StructureCP *) Tcl_GetHashValue (entryPtr);
    while (pattern) {
        if (pattern->namespace == namespacePtr) {
            break;
        }
        pattern = pattern->next;
    }
    if (!pattern) {
        SetResult ("No such element definition.");
        return TCL_ERROR;
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
        pushToStack (sdata, pattern, 0);
        sdata->validationState = VALIDATION_STARTED;
        return TCL_OK;
    }
    
    /* The normal case: we're inside the tree */
    savedStackPtr = sdata->stackPtr;
    activeStack = savedStackPtr - 1;
    currentDeep = sdata->stack[activeStack]->deep;

    DBG(
        fprintf (stderr, "probeElement: look if '%s' match\n", pattern->name);
        serializeStack (sdata);
        );

    while ((rc = matchNamePattern (sdata, pattern, currentDeep)) == -1) {
        activeStack--;
        if (activeStack < 0 || sdata->stack[activeStack]->deep < currentDeep) {
            break;
        }
        sdata->stackPtr = activeStack + 1;
    }
    if (rc == 1) {
        DBG(
            fprintf (stderr, "probeElement: element '%s' match\n", name);
            serializeStack (sdata);
            fprintf (stderr, "\n");
            );
        return TCL_OK;
    }
    sdata->stackPtr = savedStackPtr;
    DBG(
        fprintf (stderr, "element '%s' DOESN'T match\n", name);
        serializeStack (sdata);
        fprintf (stderr, "\n");
        );
    SetResult ("Element doesn't match");
    return TCL_ERROR;
}

static int checkElementEnd (
    StructureData *sdata
    )
{
    StructureCP *parent;
    int nm, ac;
    int isName = 0;
    
    getContext (parent, ac, nm);
    
    switch (parent->type) {
    case STRUCTURE_CTYPE_NAME:
        if (sdata->stackPtr == 1) return 1;
        isName = 1;
        /* fall through */
    case STRUCTURE_CTYPE_GROUP:
    case STRUCTURE_CTYPE_PATTERN:
        while (ac < parent->numChildren) {
            if (mustMatch (parent->quants[ac], nm)) {
                return 0;
            }
            ac ++;
            nm = 0;
        }
        if (isName) return 1;
        else return -1;

    case STRUCTURE_CTYPE_TEXT:
    case STRUCTURE_CTYPE_ANY:
    case STRUCTURE_CTYPE_EMPTY:
        /* Never pushed onto stack */
        Tcl_Panic ("Invalid CTYPE onto the validation stack!");
        return 0;

    case STRUCTURE_CTYPE_INTERLEAVE:
        fprintf (stderr, "checkElementEnd: STRUCTURE_CTYPE_INTERLEAVE to be implemented\n");
        return 0;
        
    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
        return -1;
    }
    return 0;
}

int
probeElementEnd (
    Tcl_Interp *interp,
    StructureData *sdata
    )
{
    int activeStack, savedStackPtr, rc, currentDeep;
    
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }

    savedStackPtr = sdata->stackPtr;
    activeStack = savedStackPtr;
    currentDeep = sdata->stack[activeStack-1]->deep;

    DBG(
        fprintf (stderr, "probeElementEnd: look if current stack top can end "
                 " name: '%s' currentDeep: %d\n",
                 sdata->stack[sdata->stackPtr-1]->pattern->name, currentDeep);
        serializeStack (sdata);
        );
    
    while ((rc = checkElementEnd (sdata)) == -1) {
        activeStack--;
        if (activeStack < 1 || sdata->stack[activeStack]->deep < currentDeep) {
            break;
        }
        popStack(sdata);
    }

    if (rc != 1) {
        sdata->stackPtr = savedStackPtr;
        SetResult ("Missing mandatory elements\n");
        return TCL_ERROR;
    }

    restoreWaterMark(sdata);
    popStack(sdata);
    if (sdata->stackPtr == 0) {
        /* End of the first pattern (the tree root) without error.
           We have successfully ended validation */
        sdata->validationState = VALIDATION_FINISHED;
    }

    return TCL_OK;
}

int
probeText (
    Tcl_Interp *interp,
    StructureData *sdata,
    char *text
    )
{
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }

    return TCL_OK;
}


int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, keywordIndex, hnew, patternIndex;
    int            result = TCL_OK, forwardDef = 0, i = 0;
    int            savedDefineToplevel;
    unsigned int   savedNumPatternList;
    StructureData  *sdata = (StructureData *) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    StructureCP   *pattern, *current = NULL;
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "defelement", "defpattern", "start", "event", "delete",
        "nrForwardDefinitions", "state", "reset", "define", NULL
    };
    enum structureInstanceMethod {
        m_defelement, m_defpattern, m_start, m_event, m_delete,
        m_nrForwardDefinitions, m_state, m_reset, m_define
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
        i = 1;
    }

    if (Tcl_GetIndexFromObj (interp, objv[1-i], structureInstanceMethods,
                             "method", 0, &methodIndex)
        != TCL_OK) {
        return TCL_ERROR;
    }
    
    Tcl_ResetResult (interp);
    switch ((enum structureInstanceMethod) methodIndex) {
    case m_defelement:
    case m_defpattern:
        if (objc != 4-i && objc != 5-i) {
            Tcl_WrongNumArgs (interp, 1-i, objv, "<name>"
                 " ?<namespace>? pattern");
            return TCL_ERROR;
        }
        if ((enum structureInstanceMethod) methodIndex == m_defelement) {
            hashTable = &sdata->element;
        } else {
            hashTable = &sdata->pattern;
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
            pattern = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (pattern) {
                if (pattern->namespace == namespacePtr) {
                    if (pattern->flags & FORWARD_PATTERN_DEF
                        || pattern->flags & PLACEHOLDER_PATTERN_DEF) {
                        forwardDef = 1;
                        break;
                    }
                    if ((enum structureInstanceMethod) methodIndex
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
            pattern = initStructureCP (
                STRUCTURE_CTYPE_NAME,
                namespacePtr,
                Tcl_GetHashKey (hashTable, entryPtr)
                );
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }

        if (!sdata->defineToplevel) {
            SETASI(sdata);
        }
        savedDefineToplevel = sdata->defineToplevel;
        sdata->defineToplevel = 0;
        sdata->currentNamespace = namespacePtr;
        sdata->currentContent = pattern->content;
        sdata->currentQuants = pattern->quants;
        sdata->numChildren = 0;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        result = Tcl_VarEval (interp, "::namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[patternIndex]), "}", NULL);
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
        result = Tcl_VarEval (interp, "::namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[2]), "}", NULL);
        if (result != TCL_OK) {
            cleanupLastPattern (sdata, savedNumPatternList);
        }
        sdata->defineToplevel = 0;
        SETASI(0);
        break;

    case m_start:
        if (objc < 3-i || objc > 3-i) {
            Tcl_WrongNumArgs (interp, 2-1, objv, "<documentElement>"
                              " ?<namespace>?");
            return TCL_ERROR;
        }
        if (sdata->start) {
            FREE (sdata->start);
        }
        sdata->start = tdomstrdup (Tcl_GetString (objv[2]));
        if (objc == 4-i) {
            if (sdata->startNamespace) {
                FREE (sdata->startNamespace);
            }
            sdata->startNamespace =
                tdomstrdup (Tcl_GetString (objv[3]));
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
        sdata->stackPtr = 0;
        sdata->validationState = VALIDATION_READY;
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
 * tDOM_StructureObjCmd --
 *
 *	This procedure is invoked to process the "structure" command.
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
tDOM_StructureObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, result = TCL_OK;
    StructureData  *sdata;

    static const char *structureMethods[] = {
        "create", NULL
    };
    enum structureMethod {
        m_create
    };

    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?argument?");
        return TCL_ERROR;
    }

    if (objc == 2) {
        methodIndex = m_create;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], structureMethods,
                                 "method", 0, &methodIndex)
            != TCL_OK) {
            return TCL_ERROR;
        }
    }
        
    Tcl_ResetResult (interp);
    switch ((enum structureMethod) methodIndex) {
    case m_create:
        sdata = initStructureData ();
        Tcl_CreateObjCommand (interp, Tcl_GetString(objv[2]),
                              structureInstanceCmd, 
                              (ClientData) sdata,
                              structureInstanceDelete);
        Tcl_SetObjResult (interp, objv[2]);
        break;
        
    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        result = TCL_ERROR;
        break;
        
    }
    return result;
}


static StructureQuant *
initStructureQuant  (
    StructureData * sdata,
    Structure_Content_Quant quantType,
    int n,
    int m
    )
{
    StructureQuant * quant;

    quant = TMALLOC (StructureQuant);
    quant->type = quantType;
    quant->minOccur = n;
    quant->maxOccur = m;
    if (sdata->numQuants == sdata->quantsSize) {
        sdata->quants = REALLOC (
            sdata->quants,
            sizeof (StructureQuant*) * 2 * sdata->quantsSize
            );
        sdata->quantsSize *= 2;
    }
    sdata->quants[sdata->numQuants] = quant;
    sdata->numQuants++;
    return quant;
}


static StructureQuant *
getQuant (
    Tcl_Interp *interp,
    StructureData *sdata,
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
        return initStructureQuant (sdata, STRUCTURE_CQUANT_N, n, n);
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
    return initStructureQuant (sdata, STRUCTURE_CQUANT_NM, n, m);
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
    StructureData *sdata = GETASI;
    StructureCP *pattern;
    
    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected.");
    pattern = initStructureCP ((Structure_CP_Type) clientData,
                               NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantRep)
    return TCL_OK;
}

static int
evalDefinition (
    Tcl_Interp *interp,
    StructureData *sdata,
    Tcl_Obj *definition,
    StructureCP *pattern,
    StructureQuant *quant
    )
{
    StructureCP **savedCurrentContent;
    StructureQuant **savedCurrentQuant;
    unsigned int savedNumChildren, savedContenSize, savedNumPatternList;
    int result;

    /* Save some state of sdata .. */
    savedCurrentContent = sdata->currentContent;
    savedCurrentQuant = sdata->currentQuants;
    savedNumChildren = sdata->numChildren;
    savedContenSize = sdata->contentSize;
    /* ... and prepare sdata for definition evaluation. */
    savedNumPatternList = sdata->numPatternList;
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
        cleanupLastPattern (sdata, savedNumPatternList);
        freeStructureCP (pattern);
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
    StructureData *sdata = GETASI;
    Structure_CP_Type patternType = (Structure_CP_Type) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    StructureCP *pattern = NULL, *current;
    StructureQuant *quant;
    int hnew;
    
    CHECK_SI
    CHECK_TOPLEVEL
    if (patternType == STRUCTURE_CTYPE_NAME) {
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
            pattern = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (pattern) {
                if (pattern->namespace == sdata->currentNamespace) {
                    break;
                }
                pattern = pattern->next;
            }
        }
        if (!pattern) {
            pattern = initStructureCP (
                patternType,
                sdata->currentNamespace,
                Tcl_GetHashKey (hashTable, entryPtr)
                );
            pattern->flags |= FORWARD_PATTERN_DEF;
            sdata->forwardPatternDefs++;
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                pattern->next = current;
            }
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }
        ADD_TO_CONTENT (pattern, quant);
    } else {
        /* Local definition of this element */
        if (hnew) {
            pattern = initStructureCP (
                STRUCTURE_CTYPE_NAME,
                sdata->currentNamespace,
                Tcl_GetHashKey (hashTable, entryPtr)
                );
            pattern->flags |= PLACEHOLDER_PATTERN_DEF;
            REMEMBER_PATTERN (pattern);
            Tcl_SetHashValue (entryPtr, pattern);
        }
        pattern = initStructureCP (
            STRUCTURE_CTYPE_NAME,
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
    StructureData *sdata = GETASI;
    Structure_CP_Type patternType = (Structure_CP_Type) clientData;
    StructureQuant *quant;
    StructureCP *pattern;

    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (2,3,"Expected: ?quant? definition");
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }

    pattern = initStructureCP (patternType, NULL, NULL);

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
    StructureData *sdata = GETASI;
    char *currentNamespace;
    Tcl_HashEntry *entryPtr;
    int hnew, result;

    fprintf (stderr, "+++HIER 0: '%s'\n", Tcl_GetString(objv[0]));
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
    StructureData *sdata = GETASI;
    StructureCP *pattern;
    
    CHECK_SI
    CHECK_TOPLEVEL
    checkNrArgs (1,1,"No arguments expected.");
    pattern = initStructureCP ((Structure_CP_Type) clientData,
                               NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantOne)
    return TCL_OK;
}

void
tDOM_StructureInit (
    Tcl_Interp *interp
    )
{
    quantOne->type = STRUCTURE_CQUANT_ONE;
    quantOpt->type = STRUCTURE_CQUANT_OPT;
    quantRep->type = STRUCTURE_CQUANT_REP;
    quantPlus->type = STRUCTURE_CQUANT_PLUS;

    /* Inline definition commands. */
    Tcl_CreateObjCommand (interp, "tdom::structure::defelement",
                          structureInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::defelement",
                          structureInstanceCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::start",
                          structureInstanceCmd, NULL, NULL);
    
    Tcl_CreateObjCommand (interp, "tdom::structure::empty",
                          EmptyAnyPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_EMPTY, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::any",
                          EmptyAnyPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_ANY, NULL);

    Tcl_CreateObjCommand (interp, "tdom::structure::element",
                          NamedPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_NAME, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::ref",
                          NamedPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_PATTERN, NULL);

    Tcl_CreateObjCommand (interp, "tdom::structure::choice",
                          AnonPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_CHOICE, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::mixed",
                          AnonPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_MIXED, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::interleave",
                          AnonPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_INTERLEAVE, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::group",
                          AnonPatternObjCmd,
                          (ClientData) STRUCTURE_CTYPE_GROUP, NULL);
    
    Tcl_CreateObjCommand (interp, "tdom::structure::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::text",
                          TextPatternObjCmd, NULL, NULL);
}

# else  /* #ifndef TDOM_NO_STRUCTURE */
int 
structureInstanceCmd (
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
    StructureData *sdata,
    char *name,
    void *namespace
    )
{
    return TCL_OK;
}

int
probeElementEnd (
    Tcl_Interp * interp,
    StructureData *sdata
    )
{
    return TCL_OK;
}

int
probeText (
    Tcl_Interp *interp,
    StructureData *sdata,
    char *text
    )
{
    return TCL_OK;
}

#endif  /* #ifndef TDOM_NO_STRUCTURE */
