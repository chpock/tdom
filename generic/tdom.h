
#define __TDOM_H

#include "tcl.h"
#include <expat.h>
#include <tclexpat.h>

#undef TCL_STORAGE_CLASS
#ifdef BUILD_tdom
# define TCL_STORAGE_CLASS DLLEXPORT
#else
# define TCL_STORAGE_CLASS DLLIMPORT
#endif

#include "dom.h"
#include "tdomDecls.h"
