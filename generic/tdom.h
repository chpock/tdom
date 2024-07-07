
#include "tcl.h"
#include <expat.h>

#ifdef BUILD_tdom
# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLEXPORT
#endif

#include "tclexpat.h"
#include "dom.h"
#include "tdomDecls.h"
