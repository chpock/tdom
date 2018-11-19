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
#endif

/* The StructureFlags flags */
#define FORWARD_PATTERN_DEF     1
#define PLACEHOLDER_PATTERN_DEF 2
#define AMBIGUOUS_PATTERN       4

/* Pointer to heap-allocated shared quants. */
static StructureQuant QuantNone;
static StructureQuant *quantNone = &QuantNone;

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
    case STRUCTURE_CTYPE_GROUP:
        fprintf (stderr, "\tName: '%s' Namespace: '%s'\n",
                 pattern->name,pattern->namespace);
        if (pattern->flags & FORWARD_PATTERN_DEF) {
            fprintf (stderr, "\tAnonymously defined NAME\n");
        }
        if (pattern->flags & PLACEHOLDER_PATTERN_DEF) {
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
        /* Do nothing */
        break;
    }
}
)

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
        DBG(serializeCP (pattern));
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
        sdata->numStackList++;
        sdata->stack[sdata->stackPtr] = stackElm;
        sdata->stackPtr++;
    }
    memset (stackElm, 0, sizeof (StructureValidationStack));
    stackElm->pattern = pattern;
    stackElm->deep = deep;
}

int
probeElement (
    Tcl_Interp *interp,
    StructureData *sdata,
    char *name,
    void *namespace
    ) 
{
    Tcl_HashEntry *entryPtr;
    void *namespacePtr;
    StructureCP *pattern, *apattern;
    StructureValidationStack *astackElm;

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
                if (sdata->startNamespace &&
                    strcmp((char*)namespace, sdata->startNamespace) != 0) {
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
    astackElm = sdata->stack[sdata->stackPtr-1];
    while (astackElm) {
        apattern = astackElm->pattern;
        while (1) {
            if (astackElm->activeChild < apattern->numChildren) {
                if (pattern == apattern->content[astackElm->activeChild]) {
                    if (apattern->quants[astackElm->activeChild] == quantNone ||
                        apattern->quants[astackElm->activeChild] == quantOpt) {
                        astackElm->activeChild++;
                    }
                }
                pushToStack (sdata, pattern, astackElm->deep + 1);
                return TCL_OK;
            }
            break;
        }
        astackElm = astackElm->next;
    }

    return TCL_ERROR;
}

int
probeElementEnd (
    Tcl_Interp *interp,
    StructureData *sdata
    )
{
    StructureCP *apattern;
    StructureValidationStack *astackElm, *thisstackElm;
    int ac, thisStackPtr, i;
    
    if (sdata->validationState == VALIDATION_FINISHED) {
        SetResult ("Validation finished.");
        return TCL_ERROR;
    }
    if (sdata->validationState == VALIDATION_READY) {
        SetResult ("No validation started");
        return TCL_ERROR;
    }
    if (sdata->stackPtr < 1) {
        SetResult ("Spurious element end event after tree validation "
                   "has finished.");
        return TCL_ERROR;
    }

    astackElm = sdata->stack[sdata->stackPtr-1];
    while (astackElm) {
        thisstackElm = astackElm;
        thisStackPtr = sdata->stackPtr-1;
        while (thisstackElm->deep == astackElm->deep) {
            ac = thisstackElm->activeChild;
            apattern = thisstackElm->pattern;
            for (i = ac; i < apattern->numChildren; i++) {
                if (apattern->quants[i] == quantOpt ||
                    apattern->quants[i] == quantRep) {
                    continue;
                }
                if (apattern->quants[i] == quantNone ||
                    apattern->quants[i] == quantPlus) {
                    SetResult ("Element can not end here (required "
                               "element(s) missing)");
                    return TCL_ERROR;
                }
                SetResult ("N and NM quantifier not implemented.");
                return TCL_ERROR;
            }
            thisStackPtr--;
            if (thisStackPtr < 1) {
                break;
            }
            thisstackElm = sdata->stack[thisStackPtr];
        }
        astackElm = astackElm->next;
    }
    if (sdata->stackPtr == 1) {
        /* End of the first pattern (the tree root) without error.
           We have successfully ended validation */
        sdata->validationState = VALIDATION_FINISHED;
        return TCL_OK;
    }
    thisStackPtr = sdata->stackPtr - 2;
    while (sdata->stack[thisStackPtr]->deep ==
           sdata->stack[sdata->stackPtr-1]->deep) {
        thisStackPtr--;
        if (thisStackPtr < 1) {
            break;
        }
    }
    sdata->stackPtr = thisStackPtr + 1;
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
    int            result = TCL_OK, forwardDef = 0;
    unsigned int   savedNumPatternList;
    StructureData  *sdata = (StructureData *) clientData;
    Tcl_HashTable *hashTable;
    Tcl_HashEntry *entryPtr;
    StructureCP   *pattern, *current = NULL;
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "element", "pattern", "start", "event", "delete",
        "nrForwardDefinitions", "state", "reset", NULL
    };
    enum structureInstanceMethod {
        m_element, m_pattern, m_start, m_event, m_delete,
        m_nrForwardDefinitions, m_state, m_reset
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

    if (Tcl_GetIndexFromObj (interp, objv[1], structureInstanceMethods,
                             "method", 0, &methodIndex)
        != TCL_OK) {
        return TCL_ERROR;
    }
        
    Tcl_ResetResult (interp);
    switch ((enum structureInstanceMethod) methodIndex) {
    case m_element:
    case m_pattern:
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs (interp, 1, objv, "<name>"
                 " ?<namespace>? pattern");
            return TCL_ERROR;
        }
        if ((enum structureInstanceMethod) methodIndex == m_element) {
            hashTable = &sdata->element;
        } else {
            hashTable = &sdata->pattern;
        }
        savedNumPatternList = sdata->numPatternList;
        namespacePtr = NULL;
        patternIndex = 3;
        if (objc == 5) {
            patternIndex = 4;
            entryPtr = Tcl_CreateHashEntry (&sdata->namespace,
                                            Tcl_GetString (objv[3]), &hnew);
            if (entryPtr != sdata->emptyNamespace) {
                namespacePtr = Tcl_GetHashKey (&sdata->namespace,
                                               entryPtr);
            }
        }
        entryPtr = Tcl_CreateHashEntry (hashTable,
                                        Tcl_GetString (objv[2]), &hnew);
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
                        == m_element) {
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

        SETASI(sdata);
        sdata->currentNamespace = namespacePtr;
        sdata->currentContent = pattern->content;
        sdata->currentQuants = pattern->quants;
        sdata->numChildren = 0;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        result = Tcl_VarEval (interp, "namespace eval ::tdom::structure {",
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
        SETASI(0);
        break;

    case m_start:
        if (objc < 3 || objc > 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<documentElement>"
                              " ?<namespace>?");
            return TCL_ERROR;
        }
        if (sdata->start) {
            FREE (sdata->start);
        }
        sdata->start = tdomstrdup (Tcl_GetString (objv[2]));
        if (objc == 4) {
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
    quant->quant = quantType;
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
        return quantNone;
    }
    quantStr = Tcl_GetStringFromObj (quantObj, &len);
    if (len == 1) {
        switch (quantStr[0]) {
        case '!':
            return quantNone;
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
        return initStructureQuant (sdata, STRUCTURE_CQUANT_N, n, 0);
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
    return initStructureQuant (sdata, STRUCTURE_CQUANT_NM, n, m);
}

/* Implements the grammar definition commands "empty" and "any" */
int
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
int
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
        return evalDefinition (interp, sdata, objv[3], pattern, quant);
    }
    return TCL_OK;
}

/* Implements the grammar definition commands "choice", "group",
 * "interleave" and "mixed" */
int
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
    checkNrArgs (2,3,"Expected: ?quant? definition");
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }

    pattern = initStructureCP (patternType, NULL, NULL);

    return evalDefinition (interp, sdata, objc == 2 ? objv[1] : objv[2],
                           pattern, quant);
}

int
AttributePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
}

int
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
    
    CHECK_SI
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

int
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
    checkNrArgs (1,1,"No arguments expected.");
    pattern = initStructureCP ((Structure_CP_Type) clientData,
                               NULL, NULL);
    REMEMBER_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantNone)
    return TCL_OK;
}

void
tDOM_StructureInit (
    Tcl_Interp *interp
    )
{
    quantNone->quant = STRUCTURE_CQUANT_NONE;
    quantOpt->quant = STRUCTURE_CQUANT_OPT;
    quantRep->quant = STRUCTURE_CQUANT_REP;
    quantPlus->quant = STRUCTURE_CQUANT_PLUS;
                                      
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

#endif
