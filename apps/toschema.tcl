
package require tdom
package require uri

variable dtdStart ""
variable dtdFinished 0
variable indent 4

proc indent {} {
    variable indent
    upvar level level
    return [string repeat " " [expr {$indent * $level}]]
}

proc fromDTD_serialize {level type quant name content} {
    switch $type {
        "NAME" {
            puts "[indent]element $name $quant"
            return
        }
        "MIXED" {
            if {[llength $content] == 0} {
                puts "[indent]text"
                return
            }
            puts "[indent]mixed \{"
        }
        "EMPTY" {
            return
        }
        "ANY" {
            puts "[indent]ANY\}"
            return
        }
        "SEQ" {
            puts "[indent]group $quant \{"
        }
        "CHOICE" {
            puts "[indent]choice $quant \{"
        }
    }
    foreach cp $content {
        fromDTD_serialize [expr {$level +1}] {*}$cp
    }
    puts "[indent]\}"
}
    
proc fromDTD_generate {} {
    variable dtdStart
    variable dtdElements
    variable dtdAttributes
    
    if {$dtdStart ne ""} {
        if {![info exists dtdElements($dtdStart)]} {
            puts "Document element not defined."
            exit 1
        }
        puts "start $dtdStart"
    }
    set level 1
    foreach name [lsort [array names dtdElements]] {
        puts "defelement $name \{"
        # First round to get possible namespace declarations
        array unset nslookup
        foreach {attkey attDef} [array get dtdAttributes $name,*] {
            lassign $attDef attname type default isRequired
            if {$attname eq "xmlns"} {
                if {$default ne ""} {
                    set nslookup(:default) $default
                }
            } else {
                set parts [split $attname ":"]
                if {[llength $parts] == 2} {
                    switch [lindex $parts 0] {
                        "xml" {
                            set nslookup(xml) "http://www.w3.org/XML/1998/namespace"
                        }
                        "xmlns" {
                            if {$default ne ""} {
                                set nslookup([lindex $parts 1]) $default
                            }
                        }
                    }
                }
            }
        }
        foreach {attkey attDef} [array get dtdAttributes $name,*] {
            lassign $attDef attname type default isRequired
            set parts [split $attname ":"]
            if {[llength $parts] == 2} {
                set prefix [lindex $parts 0]
                if {![info exists nslookup($prefix)]} {
                    # Hmmm. Either dtd error or the namespace is
                    # defined somewhere on the ancestors. To be
                    # handled. TODO
                    set cmd "attribute $attname"
                } else {
                    set cmd "nsattribute [lindex $parts 1] $nslookup($prefix)"
                }
            } else {
                set cmd "attribute $attname"
            }
            if {$isRequired && $default != ""} {
                puts "[indent]$cmd ? {[list "fixed" $default]}"
                continue
            }
            switch $type {
                "ID" -
                "IDREF" -
                "IDREFS" -
                "ENTITY" -
                "ENTITIES" -
                "NOTATION" {
                    # All above to be done
                    puts "[indent]$cmd [expr {$isRequired ? "" : "?"}]"
                }
                "NMTOKEN" {
                    puts "[indent]$cmd [expr {$isRequired ? "" : "?"}] \{nmtoken\}"
                }
                "NMTOKENS" {
                    puts "[indent]$cmd [expr {$isRequired ? "" : "?"}] \{nmtokens\}"
                }
                "CDATA" {
                    puts "[indent]$cmd [expr {$isRequired ? "" : "?"}]"
                }
                default {
                    if {[string index $type 0] ne "("} {
                        # Ups. Should not happen.
                        error "Unexpeced (invalid) attribute type '$type'"
                    } 
                    puts "[indent]$cmd [expr {$isRequired ? "" : "?"}] {enumeration {[split [string trim $type "()"] "|"]}}"
                }
            }
        }
        fromDTD_serialize 1 {*}$dtdElements($name)
        puts "\}"
    }
}

proc fromDTD_startDoctypeDecl {name systemID publicID hasInternalSubset} {
    variable dtdStart $name
    variable dtdFinished 0
}

proc fromDTD_endDoctypeDecl {args} {
    variable dtdFinished 1

    fromDTD_generate
}

proc fromDTD_elementDecl {name content} {
    variable dtdElements

    set dtdElements($name) $content
}

proc fromDTD_attlistDecl {elname name type default isRequired} {
    variable dtdAttributes

    set dtdAttributes($elname,$name) [list $name $type $default $isRequired]
}

proc fromDTD {file} {

    ::xml::parser p \
        -baseurl [tdom::baseURL $file] \
        -paramentityparsing always \
        -externalentitycommand tdom::extRefHandler \
        -startdoctypedeclcommand fromDTD_startDoctypeDecl \
        -enddoctypedeclcommand fromDTD_endDoctypeDecl \
        -elementdeclcommand fromDTD_elementDecl \
        -attlistdeclcommand fromDTD_attlistDecl
        
    p parse [tdom::xmlReadFile $file]
}

fromDTD $argv
if {[info exists dtdAttTypes]} {
    puts [array names dtdAttTypes]
}
