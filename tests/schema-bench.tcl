
package require tdom

puts "[info patchlevel] [info nameofexecutable]"

tdom::schema s
set docdir [file join [file dir [info script]] ../doc]
set file [file join $docdir tmml.schema]
set fd [open $file]
set tmmlschema [read $fd]
close $fd
s define $tmmlschema

set result ""
foreach tmmlfile {
    domDoc.xml
    domNode.xml
    dom.xml
    expatapi.xml
    expat.xml
    pullparser.xml
    schema.xml
    tdomcmd.xml
} {
    set file [file join $docdir $tmmlfile]
    set fd [open $file]
    set tmmldoc [read $fd]
    close $fd
    set doc [dom parse $tmmldoc]
    lappend result [s domvalidate $doc]
    puts [timerate {
        s domvalidate $doc
    }]
}
s delete
puts $result
          
