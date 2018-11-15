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

#include <tdom.h>

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

#define TMALLOC(t) (t*)MALLOC(sizeof(t))

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)
#define SetIntResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetIntObj(Tcl_GetObjResult(interp), (i))

#define checkNrArgs(l,h,err) if (objc < l || objc > h) {      \
        SetResult (err);                                      \
        return TCL_ERROR;                                     \
    }

typedef enum structure_cp_type {
  STRUCTURE_CTYPE_EMPTY,
  STRUCTURE_CTYPE_ANY,
  STRUCTURE_CTYPE_MIXED,
  STRUCTURE_CTYPE_NAME,
  STRUCTURE_CTYPE_CHOICE,
  STRUCTURE_CTYPE_INTERLEAVE,
  STRUCTURE_CTYPE_GROUP
} Structure_CP_Type;

static char *Structure_CP_Type2str[] = {
    "EMPTY",
    "ANY",
    "MIXED",
    "NAME",
    "CHOICE",
    "INTERLEAVE",
    "GROUP",
};

typedef enum structure_content_quant  {
  STRUCTURE_CQUANT_NONE,
  STRUCTURE_CQUANT_OPT,
  STRUCTURE_CQUANT_REP,
  STRUCTURE_CQUANT_PLUS,
  STRUCTURE_CQUANT_N,
  STRUCTURE_CQUANT_NM
} Structure_Content_Quant;

typedef unsigned int StructureFlags;
#define FORWARD_PATTERN_DEF 1

typedef struct
{
    Structure_Content_Quant  quant;
    int                      minOccur;
    int                      maxOccur;
}  StructureQuant;

/* Pointers to heap-allocated shared quants. */

static StructureQuant QuantNone;
static StructureQuant *quantNone = &QuantNone;

static StructureQuant QuantOpt;
static StructureQuant *quantOpt = &QuantOpt;

static StructureQuant QuantRep;
static StructureQuant *quantRep = &QuantRep;

static StructureQuant QuantPlus;
static StructureQuant *quantPlus = &QuantPlus;

typedef struct StructureCP
{
    Structure_CP_Type   type;
    char                    *namespace;
    char                    *name;
    struct StructureCP      *next;
    StructureFlags           flags;
    struct StructureCP     **content;
    StructureQuant         **quants;
    unsigned int             numChildren;
} StructureCP;

#define CONTENT_ARRAY_SIZE_INIT 20
#define ANON_PATTERN_ARRAY_SIZE_INIT 128
#define QUANTS_ARRAY_SIZE_INIT 8

typedef struct 
{
    StructureCP *currentModel;
    StructureCP *nextPossible;
    int               deep;
} StructureValidationStack;

typedef struct 
{
    char *start;
    char *startNamespace;
    Tcl_HashTable element;
    Tcl_HashTable namespace;
    Tcl_HashTable pattern;
    StructureCP **anonPattern;
    unsigned int numAnonPattern;
    unsigned int anonPatternSize;
    unsigned int forwardPatternDef;
    StructureQuant **quants;
    unsigned int numQuants;
    unsigned int quantsSize;
    char *currentNamespace;
    char *currentAttributeNamespace;
    int   isAttribute;
    StructureCP **currentContent;
    StructureQuant **currentQuants;
    unsigned int numChildren;
    unsigned int contentSize;
    StructureValidationStack **validationStack;
    int                        validationStackSize;
} StructureData;

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

#define REMEMBER_ANON_PATTERN(pattern)                                  \
    if (sdata->numAnonPattern == sdata->anonPatternSize) {              \
        sdata->anonPattern = (StructureCP **) MALLOC (                  \
            sizeof (StructureCP*) * sdata->anonPatternSize * 2);        \
        sdata->anonPatternSize *= 2;                                    \
    }                                                                   \
    sdata->anonPattern[sdata->numAnonPattern] = pattern;                \
    sdata->numAnonPattern++;

#define SAVE_FOR_EVAL_VARS                                              \
    StructureCP **savedCurrentContent;                                  \
    StructureQuant **savedCurrentQuant;                                 \
    unsigned int savedNumChildren, savedContenSize, savedNumAnonPattern; 

#define SAVE_FOR_EVAL                                   \
    savedCurrentContent = sdata->currentContent;        \
    savedCurrentQuant = sdata->currentQuants;           \
    savedNumChildren = sdata->numChildren;              \
    savedContenSize = sdata->contentSize;               \
    savedNumAnonPattern = sdata->numAnonPattern;        \
    sdata->currentContent = pattern->content;           \
    sdata->currentQuants = pattern->quants;             \
    sdata->numChildren = 0;                             \
    sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;

#define RESTORE_AFTER_EVAL                              \
    pattern->content = sdata->currentContent;           \
    pattern->quants = sdata->currentQuants;             \
    pattern->numChildren = sdata->numChildren;          \
    sdata->currentContent = savedCurrentContent;        \
    sdata->currentQuants = savedCurrentQuant;           \
    sdata->numChildren = savedNumChildren;              \
    sdata->contentSize = savedContenSize;               \

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
        pattern->namespace = (char *)namespace;
        /* Fall thru. */
    case STRUCTURE_CTYPE_GROUP:
        pattern->name = name;
        /* Fall thru. */
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
    
    sdata = TMALLOC (StructureData);
    memset (sdata, 0, sizeof(StructureData));
    Tcl_InitHashTable (&sdata->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&sdata->namespace, TCL_STRING_KEYS);
    sdata->anonPattern = (StructureCP **) MALLOC (
        sizeof (StructureCP*) * ANON_PATTERN_ARRAY_SIZE_INIT);
    sdata->anonPatternSize = ANON_PATTERN_ARRAY_SIZE_INIT;
    sdata->quants = (StructureQuant **) MALLOC (
        sizeof (StructureQuant*) * QUANTS_ARRAY_SIZE_INIT);
    sdata->quantsSize = QUANTS_ARRAY_SIZE_INIT;
    return sdata;
}

static void structureInstanceDelete (
    ClientData clientData
    )
{
    StructureData *sdata = (StructureData *) clientData;
    StructureCP *pattern;
    unsigned int i;
    
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    if (sdata->start) FREE (sdata->start);
    entryPtr = Tcl_FirstHashEntry (&sdata->element, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        DBG(serializeCP (pattern));
        freeStructureCP (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&sdata->element);
    entryPtr = Tcl_FirstHashEntry (&sdata->pattern, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        DBG(serializeCP (pattern));
        freeStructureCP (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&sdata->pattern);
    for (i = 0; i < sdata->numAnonPattern; i++) {
        freeStructureCP (sdata->anonPattern[i]);
    }
    FREE (sdata->anonPattern);
    for (i = 0; i < sdata->numQuants; i++) {
        FREE (sdata->quants[i]);
    }
    FREE (sdata->quants);
    FREE (sdata);
}

static int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, keywordIndex, hnew, patternIndex;
    int            result = TCL_OK, isAnonPattern = 0, newPattern = 0;
    StructureData  *sdata = (StructureData *) clientData;
    Tcl_HashEntry *entryPtr;
    StructureCP   *element, *pattern, *current = NULL;
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "element", "pattern", "start", "event", "delete",
        "nrForwardDefinitions", NULL
    };
    enum structureInstanceMethod {
        m_element, m_pattern, m_start, m_event, m_delete,
        m_nrForwardDefinitions
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
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs (interp, 1, objv, "<name>"
                 " ?<namespace>? pattern");
            return TCL_ERROR;
        }
        namespacePtr = NULL;
        patternIndex = 3;
        if (objc == 5) {
            patternIndex = 4;
            entryPtr = Tcl_CreateHashEntry (&sdata->namespace,
                                            Tcl_GetString (objv[3]), &hnew);
            namespacePtr = Tcl_GetHashKey (&sdata->namespace,
                                           entryPtr);
        }
        entryPtr = Tcl_CreateHashEntry (&sdata->element,
                                        Tcl_GetString (objv[2]), &hnew);
        element = NULL;
        if (!hnew) {
            element = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (element) {
                if (element->namespace == namespacePtr) {
                    if (element->flags & FORWARD_PATTERN_DEF) {
                        isAnonPattern = 1;
                        break;
                    }
                    SetResult ("Element already defined in this namespace.");
                    return TCL_ERROR;
                }
                element = element->next;
            }
        }
        if (element == NULL) {
            element = initStructureCP (
                STRUCTURE_CTYPE_NAME,
                namespacePtr,
                Tcl_GetHashKey (&sdata->element, entryPtr)
                );
            newPattern = 1;
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                element->next = current;
            }
            Tcl_SetHashValue (entryPtr, element);
        }

        SETASI(sdata);
        sdata->currentNamespace = namespacePtr;
        sdata->currentContent = element->content;
        sdata->currentQuants = element->quants;
        sdata->numChildren = 0;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        result = Tcl_VarEval (interp, "namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[patternIndex]), "}", NULL);
        sdata->currentNamespace = NULL;
        element->content = sdata->currentContent;
        element->quants = sdata->currentQuants;
        element->numChildren = sdata->numChildren;
        if (result == TCL_OK) {
            if (isAnonPattern) {
                element->flags &= ~FORWARD_PATTERN_DEF;
                sdata->forwardPatternDef--;
            }
        } else {
            if (hnew) Tcl_DeleteHashEntry (entryPtr);
            if (newPattern) freeStructureCP (element);
        }
        SETASI(0);
        break;

    case m_pattern:
        if (objc != 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<patternname> <definition>");
            return TCL_ERROR;
        }
        entryPtr = Tcl_CreateHashEntry (&sdata->pattern,
                                        Tcl_GetString(objv[2]), &hnew);
        if (hnew) {
            pattern = initStructureCP (
                STRUCTURE_CTYPE_GROUP,
                NULL,
                Tcl_GetHashKey (&sdata->pattern, entryPtr)
                );
            Tcl_SetHashValue (entryPtr, pattern);
        } else {
            pattern = (StructureCP *) Tcl_GetHashValue (entryPtr);
            if (pattern->flags & FORWARD_PATTERN_DEF) {
                isAnonPattern = 1;
            } else {
                SetResult ("Pattern already defined.");
                return TCL_ERROR;
            }
        }
        SETASI(sdata);
        sdata->currentContent = pattern->content;
        sdata->currentQuants = pattern->quants;
        sdata->numChildren = 0;
        sdata->contentSize = CONTENT_ARRAY_SIZE_INIT;
        result = Tcl_VarEval (interp, "namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[3]), "}", NULL);
        pattern->content = sdata->currentContent;
        pattern->quants = sdata->currentQuants;
        pattern->numChildren = sdata->numChildren;
        if (result == TCL_OK) {
            if (isAnonPattern) {
                pattern->flags &= ~FORWARD_PATTERN_DEF;
                sdata->forwardPatternDef--;
            }
        } else {
            if (hnew) {
                Tcl_DeleteHashEntry (entryPtr);
                freeStructureCP (pattern);
            }
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
                    "?<namespace>? ?<attInfo>?");
                return TCL_ERROR;
            }
            break;
        case k_elementend:
            if (objc < 4 && objc > 5) {
                Tcl_WrongNumArgs (interp, 3, objv, "<elementname>"
                    "?<namespace>?");
                return TCL_ERROR;
            }
            break;
            
        case k_text:
            if (objc != 4) {
                Tcl_WrongNumArgs (interp, 3, objv, "<text>");
                return TCL_ERROR;
            }
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
        SetIntResult(sdata->forwardPatternDef);
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
    REMEMBER_ANON_PATTERN (pattern)
    ADD_TO_CONTENT (pattern, quantNone)
    return TCL_OK;
}

int
MixedPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
}

int
ElementPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    StructureData *sdata = GETASI;
    Tcl_HashEntry *entryPtr;
    StructureCP *element = NULL, *current;
    StructureQuant *quant;
    int hnew;
    
    CHECK_SI
    checkNrArgs (2,4,"Expected: elementName ?quant? ?pattern?");

    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2]);
    if (!quant) {
        return TCL_ERROR;
    }
    if (objc < 4) {
        /* Reference to an element type */
        entryPtr = Tcl_CreateHashEntry (&sdata->element,
                                        Tcl_GetString(objv[1]), &hnew);
        if (!hnew) {
            element = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (element) {
                if (element->namespace == sdata->currentNamespace) {
                    break;
                }
                element = element->next;
            }
        }
        if (!element) {
            element = initStructureCP (
                STRUCTURE_CTYPE_NAME,
                sdata->currentNamespace,
                Tcl_GetHashKey (&sdata->element, entryPtr)
                );
            element->flags |= FORWARD_PATTERN_DEF;
            sdata->forwardPatternDef++;
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                element->next = current;
            }
            Tcl_SetHashValue (entryPtr, element);
        }
        ADD_TO_CONTENT(element, quant);
    } else {
        /* Local definition of this element */
        
    }
    
    return TCL_OK;
}

int
ChoicePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    StructureData *sdata = GETASI;
    StructureQuant *quant;
    StructureCP *pattern;
    SAVE_FOR_EVAL_VARS
    int result, i;

    CHECK_SI
    checkNrArgs (2,3,"Expected: ?quant? definition");
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }
    pattern = initStructureCP (STRUCTURE_CTYPE_CHOICE, NULL, NULL);
    SAVE_FOR_EVAL
    result = Tcl_EvalObjEx (interp, objc == 2 ? objv[1] : objv[2], 0);
    RESTORE_AFTER_EVAL
    if (result == TCL_OK) {
        REMEMBER_ANON_PATTERN (pattern);
        ADD_TO_CONTENT (pattern, quant);
    } else {
        for (i = savedNumAnonPattern; i < sdata->numAnonPattern; i++) {
            freeStructureCP (sdata->anonPattern[i]);
        }
        sdata->numAnonPattern = savedNumAnonPattern;
        freeStructureCP (pattern);
    }
    return TCL_OK;
}

int
SeqPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
}

int
InterleavePatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
}

int
GroupPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    StructureData *sdata = GETASI;
    StructureQuant *quant;
    StructureCP *pattern;
    SAVE_FOR_EVAL_VARS
    int result, i;

    CHECK_SI
    checkNrArgs (2,3,"Expected: ?quant? definition");
    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[1]);
    if (!quant) {
        return TCL_ERROR;
    }
    pattern = initStructureCP (STRUCTURE_CTYPE_CHOICE, NULL, NULL);
    SAVE_FOR_EVAL
    result = Tcl_EvalObjEx (interp, objc == 2 ? objv[1] : objv[2], 0);
    RESTORE_AFTER_EVAL
    if (result == TCL_OK) {
        REMEMBER_ANON_PATTERN (pattern);
        ADD_TO_CONTENT (pattern, quant);
    } else {
        for (i = savedNumAnonPattern; i < sdata->numAnonPattern; i++) {
            freeStructureCP (sdata->anonPattern[i]);
        }
        sdata->numAnonPattern = savedNumAnonPattern;
        freeStructureCP (pattern);
    }
    return TCL_OK;

    return TCL_OK;
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
    sdata->currentNamespace = (char *)
        Tcl_GetHashKey (&sdata->namespace, entryPtr);
    
    result = Tcl_EvalObjEx (interp, objv[2], 0);
    sdata->currentNamespace = currentNamespace;
    return result;
}

int
RefPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    StructureData *sdata = GETASI;
    StructureQuant *quant;
    Tcl_HashEntry *entryPtr;
    int hnew;
    StructureCP *pattern;
    
    CHECK_SI
    checkNrArgs (2,3,"Expected argument: patternName ?quant?");

    quant = getQuant (interp, sdata, objc == 2 ? NULL : objv[2]);
    if (!quant) {
        return TCL_ERROR;
    }
    entryPtr = Tcl_CreateHashEntry (&sdata->pattern,
                                    Tcl_GetString (objv[1]), &hnew);
    if (hnew) {
        pattern = initStructureCP (
            STRUCTURE_CTYPE_GROUP,
            NULL,
            Tcl_GetString (objv[1])
            );
        pattern->flags |= FORWARD_PATTERN_DEF;
        Tcl_SetHashValue (entryPtr, pattern);
        sdata->forwardPatternDef++;
    } else {
        pattern = Tcl_GetHashValue (entryPtr);
    }
    ADD_TO_CONTENT(pattern, quant);
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
    Tcl_CreateObjCommand (interp, "tdom::structure::mixed",
                          MixedPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::element",
                          ElementPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::choice",
                          ChoicePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::interleave",
                          InterleavePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::group",
                          GroupPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::ref",
                          RefPatternObjCmd, NULL, NULL);
}
