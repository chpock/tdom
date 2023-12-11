#
# Tcl package index file
#
if {[package vsatisfies [package provide Tcl] 9.0-]} {
    package ifneeded tdom 0.9.3 \
        "[list load [file join $dir tcl9tdom093.dll]];
         [list source [file join $dir tdom.tcl]]"
} else {
    package ifneeded tdom 0.9.3 \
        "[list load [file join $dir tdom093.dll]];
         [list source [file join $dir tdom.tcl]]"
}
