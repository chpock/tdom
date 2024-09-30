# loadtdom.tcl --
#
# This file is [source]d by all.tcl and all test files, to ensure, that
# the tcltest package and the lastest tdom build is present.
#
# RCS: @(#) $Id$
#

package require Tcl 8.5-
package require tcltest 2.2
namespace import ::tcltest::*
catch {tcltest::loadTestedCommands}

if {[catch {package require -exact tdom 0.9.5}]} {
    if {[package vsatisfies [package provide Tcl] 9.0-]} {
        set libname libtcl9tdom0.9.5[info sharedlibextension]
    } else {
        set libname libtdom0.9.5[info sharedlibextension]
    }
    if {[catch {load [file join [file dir [info script]] ../unix/$libname]}]} {
        error "Unable to load the appropriate tDOM version!"
    }
}
if {[info commands ::tdom::xmlReadFile] == ""} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

# Package internal test constraints
if {[info procs ::tdom::extRefHandler] != ""} {
    testConstraint need_uri 1
}
if {[package vsatisfies [package present Tcl] 9]} {
    testConstraint Tcl9 1
}
testConstraint 64bit [expr {$tcl_platform(pointerSize) >= 8}]
