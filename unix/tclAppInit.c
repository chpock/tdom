/*----------------------------------------------------------------------------
|   Copyright (c) 2007 Rolf Ade (rolf@pointsman.de)
+-----------------------------------------------------------------------------
|
|   $Id$
|
|
|   Main file for a standalone tclsh with tDOM build in ('big tclsh').
|
|   The contents of this file are subject to the Mozilla Public License
|   Version 1.1 (the "License"); you may not use this file except in
|   compliance with the License. You may obtain a copy of the License at
|   http://www.mozilla.org/MPL/
|
|   Software distributed under the License is distributed on an "AS IS"
|   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
|   License for the specific language governing rights and limitations
|   under the License.
|
|   The Original Code is tDOM.
|
|   The Initial Developer of the Original Code is Jochen Loewer
|   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|
|
|   written by Rolf Ade
|   August, 2007
|
\---------------------------------------------------------------------------*/

#include "tcl.h"
#include "tclInt.h"

#ifndef MODULE_SCOPE
#   define MODULE_SCOPE extern
#endif
MODULE_SCOPE int Tcl_AppInit(Tcl_Interp *);
MODULE_SCOPE int main(int, char **);
 
extern int Tdom_Init _ANSI_ARGS_((Tcl_Interp *interp));
extern int Tdom_SafeInit _ANSI_ARGS_((Tcl_Interp *interp));

#define MAX_SCRIPT_SIZE 10000
/*----------------------------------------------------------------------------
|   main
|
\---------------------------------------------------------------------------*/
int
main(
    int    argc,
    char **argv
    )
{
    Tcl_Main (argc, argv, Tcl_AppInit);
    return 0;
}

/*----------------------------------------------------------------------------
|   Tcl_AppInit
|
\---------------------------------------------------------------------------*/
int
Tcl_AppInit(interp)
    Tcl_Interp *interp;
{
    char script[MAX_SCRIPT_SIZE];
    char init[] = "set doc [dom parse <doc/>]";
    Tcl_Channel input;
    Tcl_Obj *data;
    
    /* What Tcl_Init() does is not needed for this purpose but slow
     * down things notable. */
    /* if ((Tcl_Init)(interp) == TCL_ERROR) { */
    /*     return TCL_ERROR; */
    /* } */
    if (Tdom_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "tdom", Tdom_Init, Tdom_SafeInit);

    input = Tcl_GetStdChannel(TCL_STDIN);
    if (input == NULL) {
        fprintf(stderr, "No stdin input.\n");
        exit(3);
    }
    data = Tcl_NewObj();
    Tcl_ReadChars(input, data, -1, 0);

    if (1) {
        /* Test xpath lexer / parser */
        if (Tcl_EvalEx(interp, init, -1, 0) != TCL_OK) {
            fprintf(stderr, "Error during init:\n%s\n", 
                    Tcl_GetString(Tcl_GetObjResult(interp)));
            exit(2);
        }
        if (snprintf(script, MAX_SCRIPT_SIZE, "$doc selectNodes {%s}",
                     Tcl_GetString(data)) >= MAX_SCRIPT_SIZE) {
            fprintf(stderr, "Resulting script would be too large.\n");
            exit(3);
        }
    } else {
        if (snprintf(script, MAX_SCRIPT_SIZE,
                     "dom parse -json -- {%s} doc;$doc asJSON",
                     Tcl_GetString(data)) >= MAX_SCRIPT_SIZE) {
            fprintf(stderr, "Resulting script would be too large.\n");
            exit(3);
        }
    }
    
    if (Tcl_EvalEx(interp, script, -1, 0) != TCL_OK) {
        fprintf(stderr, "Script error:\n%s\n",
                Tcl_GetString(Tcl_GetObjResult(interp)));
        exit(4);
    }
    fprintf(stdout, "%s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
    exit(0);
}
