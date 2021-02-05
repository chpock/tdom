package require tdom 0.9.2

if {[info commands ::tdom::xmlReadFile] == ""} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

if {$argc < 2} {
    puts stderr "usage: $argv0 ?options? schema-file xml-file ?xml-file ...?"
    exit 1
}

# Defaults
set elidelength 40
set eliderange [expr {$elidelength - 6}]

proc elide {text} {
    global elidelength
    global eliderange
    if {[string length $text] > $elidelength} {
        set text "[string range $text 0 $eliderange]\[...\]"
    }
    return $text
}

proc reportcmd {scmd errorType} {
    set result "[$scmd info line],[$scmd info column]: "
    switch $errorType {
        "MISSING_ATTRIBUTE" -
        "UNKNOWN_ATTRIBUTE" {
            append result "$errorType "
            if {[$scmd info vaction namespace] ne ""} {
                append result "{[$scmd info vaction namespace]}"
            }
            append result [$scmd info vaction name]
            append result " at element [$scmd info stack top]"
        }
        "INVALID_ATTRIBUTE_VALUE" {
            append result \
                "$errorType \"[elide [$scmd info vaction text]]\" for\
                 attribute [$scmd info vaction name]"
            if {[$scmd info vaction namespace] ne ""} {
                append result " {[$scmd info vaction namespace]}"
            }
            append result " at element [$scmd info stack top]"
        }
        "INVALID_VALUE" -
        "UNEXPECTED_TEXT" {
            append result "$errorType \"[elide [$scmd info vaction text]]\"\
                   within element [$scmd info stack top]"
        }
        "DOM_KEYCONSTRAINT" -
        "DOM_XPATH_BOOLEAN" -
        "MISSING_ELEMENT" -
        "MISSING_TEXT" -
        "UNEXPECTED_ROOT_ELEMENT" -
        "UNEXPECTED_ELEMENT" -
        "INVALID_KEYREF" -
        "UNKNOWN_ROOT_ELEMENT" -
        "UNKNOWN_GLOBAL_ID" -
        "UNKNOWN_ID" {
            append result "$errorType within element [$scmd info stack top]\
                           - given [$scmd info vaction name],\
                           expecting [$scmd info expected]"

        }
    }
    puts $result
    if {$errorType eq "MISSING_ELEMENT"} {
        return "ignore"
    } elseif {$errorType eq "UNEXPECTED_ELEMENT"} {
        return result "vanish"
    }
}

tdom::schema s
# puts "Loading schema: [time {
#     s define {
#         source [lindex $argv 0]
#     }
# }]"
s define {
    source [lindex $::argv 0]
}
s reportcmd reportcmd
foreach xml [lrange $argv 1 end] {
    s validatefile $xml
}
