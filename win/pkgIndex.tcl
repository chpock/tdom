# tDOM Tcl package index file

package ifneeded tdom 0.9.0 \
    "[list load   [file join $dir tdom090[info sharedlibextension] ] tdom];\
     [list source [file join $dir tdom.tcl]]"
