
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
            puts "[indent]group \{"
        }
        "CHOICE" {
            puts "[indent]choice \{"
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
        foreach {attkey attDef} [array get dtdAttributes $name,*] {
            lassign $attDef attname type default isRequired
            switch $type {
                "ID" -
                "IDREF" -
                "IDREFS" -
                "ENTITY" -
                "ENTITIES" -
                "NMTOKEN" -
                "NMTOKENS" -
                "NOTATION" {
                    # All above to be done
                    puts "[indent]attribute $attname [expr {$isRequired ? "" : "?"}]"
                }
                "CDATA" {
                    puts "[indent]attribute $attname [expr {$isRequired ? "" : "?"}]"
                }
                default {
                    if {[string index $type 0] ne "("} {
                        # Ups. Should not happen.
                        error "Unexpeced (invalid) attribute type '$type'"
                    } 
                    puts "[indent]attribute $attname [expr {$isRequired ? "" : "?"}] {enumeration {[split [string trim $type "()"] "|"]}}"
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
