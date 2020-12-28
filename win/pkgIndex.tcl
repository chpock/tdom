# tDOM Tcl package index file

package ifneeded tdom 0.9.3 \
    "[list load   [file join $dir tdom092[info sharedlibextension] ] tdom];\
     [list source [file join $dir tdom.tcl]]"
