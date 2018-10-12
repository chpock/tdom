# loadtdom.tcl --
#
# This file is [source]d by all.tcl and all test files, to ensure, that
# the tcltest package and the lastest tdom build is present.
#
# RCS: @(#) $Id$
#

package require tcltest
namespace import ::tcltest::*
if {[catch {package require -exact tdom 0.9.2}]} {
    if {[catch {load [file join [file dir [info script]] ../unix/libtdom0.9.2.so]}]} {
        error "Unable to load the appropriate tDOM version!"
    }
}
if {[info commands ::tdom::xmlReadFile] == ""} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

