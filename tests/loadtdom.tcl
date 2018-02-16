# loadtdom.tcl --
#
# This file is [source]d by all.tcl and all test files, to ensure, that
# the tcltest package and the lastest tdom build is present.
#
# RCS: @(#) $Id$
#

package require tcltest
namespace import ::tcltest::*
package require -exact tdom 0.9.1
if {[info commands ::tDOM::xmlReadFile] eq ""} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

