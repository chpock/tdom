
#include <tdom.h>

#define TMALLOC(t) (t*)MALLOC(sizeof(t))

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)

typedef enum structure_content_type {
  STRUCTURE_CTYPE_EMPTY,
  STRUCTURE_CTYPE_ANY,
  STRUCTURE_CTYPE_MIXED,
  STRUCTURE_CTYPE_NAME,
  STRUCTURE_CTYPE_CHOICE,
  STRUCTURE_CTYPE_SEQ,
  STRUCTURE_CTYPE_GROUP
} Structure_Content_Type;

typedef enum structure_content_quant  {
  STRUCTURE_CQUANT_NONE,
  STRUCTURE_CQUANTURE_OPT,
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
} StructurInfo;


static StructureElement*
initStructureElement () 
{
    StructureElement *element;

    element = TMALLOC (StructureElement);
    memset(element, 0, sizeof(StructureElement));
    return element;
}
    
static StructurInfo*
initStructure () 
{
    StructurInfo *structureInfo;
    
    structureInfo = TMALLOC (StructurInfo);
    structureInfo->start = NULL;
    structureInfo->startNamespace = NULL;
    structureInfo->validationStack = NULL;
    structureInfo->validationStackSize = 0;
    Tcl_InitHashTable (&structureInfo->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->pattern, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->namespace, TCL_STRING_KEYS);
    return structureInfo;
}

static void
structureCleanupElement (
    StructureElement *element,
    int freeStruct
    ) 
{
    if (element->name) FREE (element->name);
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
    void          *namespacePtr;
    
    static const char *structureInstanceMethods[] = {
        "element", "pattern", "start", "event", NULL
    };
    enum structureInstanceMethod {
        m_element, m_pattern, m_start, m_event
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
                    structureCleanupElement (element, 0);
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
        if (!Tcl_ListObjLength (interp, objv[3], &length)
            || (length != 2 && length != 4)) {
            SetResult ("Expected argument: <tdf>}");
            return TCL_ERROR;
        }
        index = 0;
        namespace = NULL;
        patternName = NULL;
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
