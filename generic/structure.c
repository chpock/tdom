
#include <tdom.h>


#define TMALLOC(t) (t*)MALLOC(sizeof(t))

typedef Tcl_HashEntry* Namespace;

typedef struct 
{
    char *start;
    char *startNamespace;
    Tcl_HashTable element;
    Tcl_HashTable pattern;
} StructurInfo;

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
    char *namespace;
    StructurePattern *pattern;
    struct StructureElement *next;
} StructureElement;

static int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *CONST objv[]
    )
{
    int            methodIndex, result = TCL_OK;
    StructurInfo  *structureInfo = (StructurInfo *) clientData;

    static CONST84 char *structureMethods[] = {
        "element", "pattern", "start", NULL
    };
    enum datatypeMethod {
        m_element, m_pattern, m_start
    };

    if (objc < 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "subcommand ?arguments?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj (interp, objv[1], structureMethods,
                             "method", 0, &methodIndex)
        != TCL_OK) {
        return TCL_ERROR;
    }
        
    Tcl_ResetResult (interp);
    switch ((enum datatypeMethod) methodIndex) {
    case m_element:
        break;

    case m_pattern:
        break;

    case m_start:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<documentElement>");
            return TCL_ERROR;
        }
        if (structureInfo->start) {
            FREE (structureInfo->start);
        }
        structureInfo->start = tdomstrdup (Tcl_GetString (objv[2]));
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

static StructurInfo*
initStructure () 
{
    StructurInfo *structureInfo;
    
    structureInfo = TMALLOC (StructurInfo);
    Tcl_InitHashTable (&structureInfo->element, TCL_STRING_KEYS);
    Tcl_InitHashTable (&structureInfo->pattern, TCL_STRING_KEYS);
    return structureInfo;
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
    Tcl_Obj *CONST objv[]
    )
{
    int            methodIndex, result = TCL_OK;
    StructurInfo  *structureInfo;

    static CONST84 char *structureMethods[] = {
        "create", NULL
    };
    enum datatypeMethod {
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
    switch ((enum datatypeMethod) methodIndex) {
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
