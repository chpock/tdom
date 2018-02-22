# tDOM Tcl package index file

package ifneeded tdom 0.9.1 \
    "[list load   [file join $dir tdom091[info sharedlibextension] ] tdom];\
     [list source [file join $dir tdom.tcl]]"
