
#include <tdom.h>

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

typedef enum structure_content_quant  {
  STRUCTURE_CQUANT_NONE,
  STRUCTURE_CQUANT_OPT,
  STRUCTURE_CQUANT_REP,
  STRUCTURE_CQUANT_PLUS,
  STRUCTURE_CQUANT_N,
  STRUCTURE_CQUANT_NM
} Structure_Content_Quant;

typedef struct StructurePattern
{
    Structure_Content_Type    type;
    Structure_Content_Quant   quant;
    int                       minOccur;
    int                       maxOccur;
    struct StructurePattern **content;
} StructurePattern;



typedef struct StructureElement 
{
    char *name;
    void *namespace;
    StructurePattern *pattern;
    struct StructureElement *next;
} StructureElement;

typedef struct 
{
    StructurePattern *currentModel;
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
    StructurePattern *currentPattern;
    StructurePattern **currentChilds;
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
        SetResult ("Command called outside of grammer context.");  \
        return TCL_ERROR;                                          \
    }
#define CHECK_SI_CONTEXT \
    if (structureInfo->isAttribute) {                             \
        SetResult ("Command called in invalid grammer context."); \
        return TCL_ERROR;                                         \
    }

static StructureElement*
initStructureElement () 
{
    StructureElement *element;

    element = TMALLOC (StructureElement);
    memset (element, 0, sizeof(StructureElement));
    return element;
}

static StructurePattern*
initStructurePattern ()
{
    StructurePattern *pattern;

    pattern = TMALLOC (StructurePattern);
    memset (pattern, 0, sizeof(StructurePattern));
    return pattern;
}

static StructurInfo*
initStructure () 
{
    StructurInfo *structureInfo;
    
    structureInfo = TMALLOC (StructurInfo);
    memset (structureInfo, 0, sizeof(StructurInfo));
    Tcl_InitHashTable (&structureInfo->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->namespace, TCL_STRING_KEYS);
    return structureInfo;
}

static void
cleanupElement (
    StructureElement *element,
    int freeStruct
    ) 
{
    if (element->name) FREE (element->name);
}

static void
cleanupPattern (
    StructurePattern *pattern
    )
{
    FREE (pattern);
}
    
static int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    )
{
    int            methodIndex, length, index, keywordIndex,
                   hnew, result = TCL_OK;
    StructurInfo  *structureInfo = (StructurInfo *) clientData;
    Tcl_Obj *listelm, *namespace, *patternName;
    Tcl_HashEntry *entryPtr;
    StructureElement *element, *current;
    StructurePattern *pattern;
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "element", "pattern", "start", "event", "delete", NULL
    };
    enum structureInstanceMethod {
        m_element, m_pattern, m_start, m_event, m_delete
    };

    static const char *elementDescriptionKeywords[] = {
        "namespace", "pattern", NULL
    };

    enum elementDescriptionKeyword
    {
        k_namespace, k_pattern
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
        if (objc != 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<name> <definition>");
            return TCL_ERROR;
        }
        if (!Tcl_ListObjLength (interp, objv[3], &length)
            || (length != 2 && length != 4)) {
            SetResult ("Expected argument: {?namespace <namespace>?"
                       " pattern <patternName>}");
            return TCL_ERROR;
        }
        index = 0;
        namespace = NULL;
        patternName = NULL;
        while (index < length) {
            Tcl_ListObjIndex (interp, objv[3], index, &listelm);
            if (Tcl_GetIndexFromObj (interp, listelm,
                                     elementDescriptionKeywords,
                                     "keyword", 0, &keywordIndex)
                != TCL_OK) {
                return TCL_ERROR;
            }
            switch ((enum elementDescriptionKeyword) keywordIndex) {
            case k_namespace:
                Tcl_ListObjIndex (interp, objv[3], index + 1,
                                  &namespace);
                break;
                
            case k_pattern:
                Tcl_ListObjIndex (interp, objv[3], index + 1,
                                  &patternName);
                break;
            }
            index += 2;
        }
        if (!patternName) {
            SetResult ("Expected argument: {?namespace <namespace>?"
                       " pattern <patternName>}");
            return TCL_ERROR;
        }
        namespacePtr = NULL;
        if (namespace) {
            entryPtr = Tcl_CreateHashEntry (&structureInfo->namespace,
                                            namespace, &hnew);
            namespacePtr = Tcl_GetHashKey (&structureInfo->namespace,
                                           entryPtr);
        }
        entryPtr = Tcl_CreateHashEntry (&structureInfo->element,
                                        objv[2], &hnew);
        if (hnew) {
            element = initStructureElement ();
            Tcl_SetHashValue (entryPtr, element);
        } else {
            element = (StructureElement *) Tcl_GetHashValue (entryPtr);
            while (element) {
                if (element->namespace == namespacePtr) {
                    cleanupElement (element, 0);
                    break;
                }
                element = element->next;
            }
            if (!element) {
                element = initStructureElement ();
                current = (StructureElement *) Tcl_GetHashValue (entryPtr);
                element->next = current;
                Tcl_SetHashValue (entryPtr, element);
            }
        }
        element->name = tdomstrdup (Tcl_GetString (objv[2]));
        element->namespace = namespacePtr;
        element->pattern = (StructurePattern *)
            Tcl_CreateHashEntry (&structureInfo->pattern,
                                 Tcl_GetString (patternName), &hnew);
        break;

    case m_pattern:
        if (objc != 4) {
            Tcl_WrongNumArgs (interp, 2, objv, "<patternname> <definition>");
            return TCL_ERROR;
        }
        entryPtr = Tcl_CreateHashEntry (&structureInfo->pattern,
                                        Tcl_GetString(objv[2]), &hnew);
        if (!hnew) {
            cleanupPattern ((StructurePattern *) entryPtr);
        }
        SETASI(structureInfo);
        pattern = initStructurePattern ();
        Tcl_SetHashValue (entryPtr, pattern);
        structureInfo->currentPattern = pattern;
        result = Tcl_VarEval (interp, "namespace eval ::tDOM::structure {",
                              Tcl_GetString (objv[3]), "}", NULL);
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

static void structureFreePattern (
    StructurePattern *pattern
    )
{
    FREE (pattern);
}

static void structureInstanceDelete (
    ClientData clientData
    )
{
    StructurInfo *structureInfo = (StructurInfo *) clientData;
    StructurePattern *pattern;
    
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    entryPtr = Tcl_FirstHashEntry (&structureInfo->element, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        structureFreePattern (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&structureInfo->element);
    entryPtr = Tcl_FirstHashEntry (&structureInfo->pattern, &search);
    while (entryPtr) {
        pattern = Tcl_GetHashValue (entryPtr);
        structureFreePattern (pattern);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (&structureInfo->pattern);
    
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
        structureInfo = initStructure();
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

    CHECK_SI
    checkNrArgs (3,4,"Expected: elementName quant ?pattern?");
    
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
    Tcl_CreateObjCommand (interp, "tDOM::structure::empty",
                          EmptyPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::any",
                          AnyPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::mixed",
                          MixedPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::element",
                          ElementPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::choice",
                          ChoicePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::seq",
                          SeqPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::interleave",
                          InterleavePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::group",
                          GroupPatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::attribute",
                          AttributePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::namespace",
                          NamespacePatternObjCmd, NULL, NULL);
    Tcl_CreateObjCommand (interp, "tDOM::structure::ref",
                          RefPatternObjCmd, NULL, NULL);
}

