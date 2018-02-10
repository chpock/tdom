
#include <tdom.h>

typedef struct tDOM_PullParserInfo 
{
    XML_Parser  parser;
    Tcl_Interp *inter;
    Tcl_Obj    *name;
    int status;
} tDOM_PullParserInfo;

static void
tDOM_PullParserDeleteCmd (
    ClientData clientdata
    )
{
    tDOM_PullParserInfo *pullInfo = clientdata;

    FREE (pullInfo);
}

static int
tDOM_PullParserInstanceCmd (
    ClientData  clientdata,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
    )
{
    tDOM_PullParserInfo *pullInfo = clientdata;

    return TCL_OK;
}


int
tDOM_PullParserCmd (
    ClientData  dummy,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
    )
{
    tDOM_PullParserInfo *pullInfo;

    if (objc < 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "cmdName");
    }
    
    pullInfo = (tDOM_PullParserInfo *) MALLOC(sizeof(tDOM_PullParserInfo));
    memset (pullInfo, 0, sizeof (tDOM_PullParserInfo));
    
    pullInfo->name = objv[1];

    Tcl_CreateObjCommand (interp, Tcl_GetString(pullInfo->name),
                          tDOM_PullParserInstanceCmd, (ClientData) pullInfo,
                          tDOM_PullParserDeleteCmd);
    Tcl_SetObjResult(interp, pullInfo->name);
    return TCL_OK;
}

