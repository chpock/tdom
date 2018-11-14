
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

#define checkNrArgs(l,h,err) if (objc < l || objc > h) {      \
        SetResult (err);                                      \
        return TCL_ERROR;                                     \
    }

typedef enum structure_content_type {
  STRUCTURE_CTYPE_EMPTY,
  STRUCTURE_CTYPE_ANY,
  STRUCTURE_CTYPE_MIXED,
  STRUCTURE_CTYPE_NAME,
  STRUCTURE_CTYPE_CHOICE,
  STRUCTURE_CTYPE_SEQ,
  STRUCTURE_CTYPE_INTERLEAVE,
  STRUCTURE_CTYPE_GROUP
} Structure_Content_Type;

static char *Structure_Content_Type2str[] = {
    "EMPTY",
    "ANY",
    "MIXED",
    "NAME",
    "CHOICE",
    "SEQ",
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
#define ANON_ELEMENT_DEF 1

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
    char                    *namespace;
    char                    *name;
    struct StructureCP      *next;
    StructureFlags           flags;
    Structure_Content_Type   type;
    struct StructureCP     **content;
    StructureQuant         **quants;
    unsigned int             numChildren;
} StructureCP;

#define START_CONTENT_ARRAY_LEN 20

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
    StructureValidationStack **validationStack;
    int                        validationStackSize;
    Tcl_HashTable element;
    Tcl_HashTable namespace;
    Tcl_HashTable pattern;
    char *currentNamespace;
    char *currentAttributeNamespace;
    int   isAttribute;
    StructureCP **currentChilds;
    StructureQuant **childQuants;
    unsigned int childCount;
    unsigned int childSize;
} StructurInfo;

#ifndef TCL_THREADS
  static StructurInfo *activeStructureInfo = 0;
# define GETASI activeStructureInfo
# define SETASI(v) activeStructureInfo = v
#else
  static Tcl_ThreadDataKey activeStructureInfo;
# define GETASI  *(StructurInfo**) Tcl_GetThreadData(&activeStructureInfo, \
                                                     sizeof(StructurInfo*))
static void SetActiveStructureInfo (StructurInfo *v) 
{
    StructurInfo **structureInfoPtr = Tcl_GetThreadData(&activeStructureInfo,
                                                        sizeof (StructurInfo*));
    *structureInfoPtr = v;
}
# define SETASI(v) SetActiveStructureInfo (v)
#endif


#define CHECK_SI \
    if (!structureInfo) {                                          \
        SetResult ("Command called outside of grammar context.");  \
        return TCL_ERROR;                                          \
    }
#define CHECK_SI_CONTEXT \
    if (structureInfo->isAttribute) {                             \
        SetResult ("Command called in invalid grammar context."); \
        return TCL_ERROR;                                         \
    }

static StructureCP*
initStructureCP (
    Structure_Content_Type type,
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
    case STRUCTURE_CTYPE_SEQ:
    case STRUCTURE_CTYPE_INTERLEAVE:
        pattern->content = (StructureCP**) MALLOC (
            sizeof(StructureCP*) * START_CONTENT_ARRAY_LEN
            );
        pattern->quants = (StructureQuant**) MALLOC (
            sizeof (StructureQuant*) * START_CONTENT_ARRAY_LEN
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
             Structure_Content_Type2str[pattern->type]);
    switch (pattern->type) {
    case STRUCTURE_CTYPE_NAME:
    case STRUCTURE_CTYPE_GROUP:
        fprintf (stderr, "\tName: '%s' Namespace: '%s'\n",
                 pattern->name,pattern->namespace);
        if (pattern->flags & ANON_ELEMENT_DEF) {
            fprintf (stderr, "\tAnonymously defined NAME\n");
        }
        /* Fall thru. */
    case STRUCTURE_CTYPE_MIXED:
    case STRUCTURE_CTYPE_CHOICE:
    case STRUCTURE_CTYPE_SEQ:
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

static StructurInfo*
initStructureInfo () 
{
    StructurInfo *structureInfo;
    
    structureInfo = TMALLOC (StructurInfo);
    memset (structureInfo, 0, sizeof(StructurInfo));
    Tcl_InitHashTable (&structureInfo->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->namespace, TCL_STRING_KEYS);
    return structureInfo;
}

static void structureInstanceDelete (
    ClientData clientData
    )
{
    StructurInfo *structureInfo = (StructurInfo *) clientData;
    StructureCP *pattern;
    
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    if (structureInfo->start) FREE (structureInfo->start);
    entryPtr = Tcl_FirstHashEntry (&structureInfo->element, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        DBG(serializeCP (pattern));
        freeStructureCP (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&structureInfo->element);
    entryPtr = Tcl_FirstHashEntry (&structureInfo->pattern, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        DBG(serializeCP (pattern));
        freeStructureCP (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&structureInfo->pattern);
    FREE (structureInfo);
}

static int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, keywordIndex,
                   hnew, patternIndex, result = TCL_OK;
    StructurInfo  *structureInfo = (StructurInfo *) clientData;
    Tcl_HashEntry *entryPtr;
    StructureCP *element, *current, *pattern;
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "element", "pattern", "start", "event", "delete", NULL
    };
    enum structureInstanceMethod {
        m_element, m_pattern, m_start, m_event, m_delete
    };

    static const char *eventKeywords[] = {
        "elementstart", "elementend", "text", NULL
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
        if (objc != 3 && objc != 5) {
            Tcl_WrongNumArgs (interp, 1, objv, "<name>"
                 " ?namespace <namespace>? pattern");
            return TCL_ERROR;
        }
        namespacePtr = NULL;
        if (objc == 5) {
            patternIndex = 4;
            entryPtr = Tcl_CreateHashEntry (&structureInfo->namespace,
                                            Tcl_GetString (objv[3]), &hnew);
            namespacePtr = Tcl_GetHashKey (&structureInfo->namespace,
                                           entryPtr);
        } else {
            patternIndex = 2;
        }            
        entryPtr = Tcl_CreateHashEntry (&structureInfo->element,
                                        objv[2], &hnew);
        element = NULL;
        if (!hnew) {
            element = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (element) {
                if (element->namespace == namespacePtr) {
                    if (element->flags & ANON_ELEMENT_DEF) {
                        element->flags &= ~ANON_ELEMENT_DEF;
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
                Tcl_GetHashKey (&structureInfo->element, entryPtr)
                );
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                element->next = current;
            }
        }
        Tcl_SetHashValue (entryPtr, element);

        SETASI(structureInfo);
        structureInfo->currentChilds = element->content;
        structureInfo->childQuants = element->quants;
        structureInfo->childCount = 0;
        structureInfo->childSize = START_CONTENT_ARRAY_LEN;
        result = Tcl_VarEval (interp, "namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[patternIndex]), "}", NULL);
        element->content = structureInfo->currentChilds;
        element->quants = structureInfo->childQuants;
        element->numChildren = structureInfo->childCount;
        SETASI(0);
        break;

    case m_pattern:
        if (objc != 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<patternname> <definition>");
            return TCL_ERROR;
        }
        entryPtr = Tcl_CreateHashEntry (&structureInfo->pattern,
                                        Tcl_GetString(objv[2]), &hnew);
        if (!hnew) {
            SetResult ("Element already defined in this namespace.");
            return TCL_ERROR;
        }
        SETASI(structureInfo);
        pattern = initStructureCP (
            STRUCTURE_CTYPE_GROUP,
            NULL,
            Tcl_GetHashKey (&structureInfo->pattern, entryPtr)
            );
        Tcl_SetHashValue (entryPtr, pattern);
        structureInfo->currentChilds = pattern->content;
        structureInfo->childQuants = pattern->quants;
        structureInfo->childCount = 0;
        structureInfo->childSize = START_CONTENT_ARRAY_LEN;
        result = Tcl_VarEval (interp, "namespace eval ::tdom::structure {",
                              Tcl_GetString (objv[3]), "}", NULL);
        pattern->content = structureInfo->currentChilds;
        pattern->quants = structureInfo->childQuants;
        pattern->numChildren = structureInfo->childCount;
        
        SETASI(0);
        break;

    case m_start:
        if (objc < 3 || objc > 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<documentElement>"
                              " ?<namespace>?");
            return TCL_ERROR;
        }
        if (structureInfo->start) {
            FREE (structureInfo->start);
        }
        structureInfo->start = tdomstrdup (Tcl_GetString (objv[2]));
        if (objc == 4) {
            if (structureInfo->startNamespace) {
                FREE (structureInfo->startNamespace);
            }
            structureInfo->startNamespace =
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
    StructurInfo  *structureInfo;

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
        structureInfo = initStructureInfo ();
        Tcl_CreateObjCommand (interp, Tcl_GetString(objv[2]),
                              structureInstanceCmd, 
                              (ClientData) structureInfo,
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

int
EmptyPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

    return TCL_OK;
}

int
AnyPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

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
    StructurInfo *structureInfo = GETASI;
    Tcl_HashEntry *entryPtr;
    StructureCP *element = NULL, *current;
    int hnew;
    
    CHECK_SI
    checkNrArgs (2,4,"Expected: elementName ?quant? ?pattern?");

    if (objc < 4) {
        /* Reference to an element type */
        entryPtr = Tcl_CreateHashEntry (&structureInfo->element,
                                        Tcl_GetString(objv[1]), &hnew);
        
        if (!hnew) {
            element = (StructureCP *) Tcl_GetHashValue (entryPtr);
            while (element) {
                if (element->namespace == structureInfo->currentNamespace) {
                    break;
                }
                element = element->next;
            }
        }
        if (!element) {
            element = initStructureCP(
                STRUCTURE_CTYPE_NAME,
                structureInfo->currentNamespace,
                Tcl_GetHashKey (&structureInfo->element, entryPtr)
                );
            element->flags &= ANON_ELEMENT_DEF;
            if (!hnew) {
                current = (StructureCP *) Tcl_GetHashValue (entryPtr);
                element->next = current;
            }
            Tcl_SetHashValue (entryPtr, element);
        }
        if (structureInfo->childCount == structureInfo->childSize) {
            structureInfo->currentChilds =
                REALLOC (structureInfo->currentChilds,
                         2 * structureInfo->childSize
                         * sizeof (StructureCP*));
            structureInfo->childSize *= 2;
        }
        structureInfo->currentChilds[structureInfo->childCount] = element;
        structureInfo->childCount++;
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
    StructurInfo *structureInfo = GETASI;
    char *currentNamespace;
    Tcl_HashEntry *entryPtr;
    int hnew;
    
    CHECK_SI
    checkNrArgs (3,3,"Expected: namespace pattern");

    currentNamespace = structureInfo->currentNamespace;
    entryPtr = Tcl_CreateHashEntry (&structureInfo->namespace,
                                    objv[1], &hnew);
    structureInfo->currentNamespace = (char *)
        Tcl_GetHashKey (&structureInfo->namespace, entryPtr);
    if (Tcl_EvalObjEx (interp, objv[2], 0) != TCL_OK) {
        return TCL_ERROR;
    }
    structureInfo->currentNamespace = currentNamespace;
    return TCL_OK;
}

int
RefPatternObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{

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
                          EmptyPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::any",
                          AnyPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::mixed",
                          MixedPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::element",
                          ElementPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::choice",
                          ChoicePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tdom::structure::seq",
                          SeqPatternObjCmd, NULL, NULL);
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

