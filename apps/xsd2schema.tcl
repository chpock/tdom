# Simple converter from (sane) xsd to tdom schema.
#
# BEWARE: Within the xsd namespace in this code there is a 'list'
# procedure. Refer to the core [list] command by ::list within code
# evaluated within the xsd namespace.

package require tdom
namespace import tdom::*

namespace eval xsd {
    variable indent 2
    variable level 0
    variable xsddata
    variable basedir
    variable targetNS
    variable schema
    variable output "collect"
    variable unsupportedFeaturesSeen
    variable xsd2schemaName [dict create {*}{
        base64Binary base64
        decimal number
        ID id
        IDREF idref
        Name name
        NCName ncname
        NMTOKEN nmtoken
        QName qname
    }]
}

# Not used by the converter code itself but written to the created
# schema with the help of [info body].
proc xsd::tclxsdtypes {type value} {
    switch $type {
        default {
            puts  "Type $type not implemented"
        }
    }
    return 1
}

proc xsd::prolog {} {
    variable output

    if {$output eq "collect"} {
        variable schema
        append schema "# Prolog\n"
        append schema "::namespace eval ::xsd { }\n"
        append schema "proc ::xsd::tclxsdtypes {type value} \{[info body ::xsd::tclxsdtypes]\}\n"
        append schema "# Prolog end\n"
    } else {
        puts "# Prolog"
        puts "::namespace eval ::xsd { }"
        puts "proc ::xsd::tclxsdtypes {type value} \{[info body ::xsd::tclxsdtypes]\}"
        puts "# Prolog end"
    }
}

proc xsd::indent {} {
    variable indent
    variable level
    return [string repeat " " [expr {$indent * $level}]]
}

proc xsd::prefix {ns} {
    variable xsddata
    return [dict get $xsddata nsprefix $ns]
}
    
proc rproc {name args body} {
    proc $name $args "variable level;upvar result result;$body"
}

rproc xsd::sput {text} {
    append result "[indent]$text\n"
}

rproc xsd::sputnnl {text} {
    append result "[indent]$text"
}

rproc xsd::sputc {text} {
    foreach line [split $text "\n"] {
        append result "[indent]# $line\n"
    }
}

rproc xsd::sputce {text} {
    foreach line [split $text "\n"] {
        append result "\n[indent]# LOOK_AT $line\n"
    }
}

rproc xsd::out {{nonl 0}} {
    variable output

    if {![info exists result]} {
        return
    }
    if {$output eq "collect"} {
        variable schema
        if {$nonl} {
            append schema $result
        } else {
            append schema $result "\n"
        }
    } else {
        if {$nonl} {
            puts -nonewline $result
        } else {
            puts $result
        }
    }
    set result ""
}

proc xsd::getQuant {node} {
    set min [$node @minOccurs 1]
    set max [$node @maxOccurs 1]

    switch $min {
        0 {
            switch $max {
                0 {
                    # Huch: min and max == 0, so never at all?
                }
                1 {return ?}
                "unbounded" {return *}
                default {return "{0 $max}"}
            }
        }
        1 {
            switch $max {
                0 {
                    # Huch: min == 1 and max == 0, ?
                }
                1 {return "!"}
                "unbounded" {return +}
                default {return "{1 $max}"}
            }
        }
        default {
            if {$max < $min} {
                # Huch, contradiction
                return
            } elseif {$max == $min} {
                return $min
            } else {
                return "{$min $max}"
            }
        }
    }
}

rproc xsd::annotation {node} {
    foreach child [$node selectNodes xsd:*] {
        if {[$child localName] eq "documentation"} {
            set lang [$child getAttributeNS \
                          "http://www.w3.org/XML/1998/namespace" lang ""]
            if {$lang ne ""} {
                 sputc "($lang:) [$child text]"
            } else {
                sputc "[$child text]"
            }
        } else {
            sputc "(appinfo:) [$child text]"
        }
    }
}

proc xsd::nsfromprefix {contextNode prefix} {
    return [lindex [lindex [$contextNode selectNodes {namespace::*[name()=$prefix]}] 0] 1]

}

proc xsd::resolveFQ {name node {forAtt 0}} {
    if {[string first : $name] > -1} {
        lassign [split $name :] prefix name
        return [::list [nsfromprefix $node $prefix] $name]
    } else {
        if {$forAtt} {
            return [::list "" $name]
        } else {
            return [::list \
                        [lindex [lindex [$node selectNodes {
                            namespace::*[name()='']
                        }] 0] 1]\
                        $name]
        }
    }
}

proc xsd::processNamespaces {} {
    variable xsddata
    variable level

    set result "prefixns \{\n"
    set level 1
    set nr 1
    dict for {ns dummy} [dict get $xsddata namespace] {
        if {$ns eq ""} {
            dict set xsddata nsprefix $ns ""
        } else {
            dict set xsddata nsprefix $ns "ns$nr"
            sput "ns$nr $ns"
            incr nr
        }
    }
    if {$nr > 1} {
        append result "\}\n"
        out
    }
    set level 0
}

rproc xsd::import {import} {
    variable xsddata
    variable targetNS
    variable basedir
    
    set schemaLocation [$import @schemaLocation ""]
    if {$schemaLocation eq ""} {
        sputce "import: skiping because of missing schemaLocation\n[$import asXML]"
        return
    }
    if {[dict exists $xsddata imported $schemaLocation]} {
        return
    }
    dict set xsddata imported $schemaLocation ""
    set savedTargetNS $targetNS
    try {
        set xsddoc [dom parse [xmlReadFile [file join $basedir $schemaLocation]]]
        $xsddoc selectNodesNamespaces {
            xsd "http://www.w3.org/2001/XMLSchema"
        }
        set xsd [$xsddoc documentElement]
        set targetNS [$xsd @targetNamespace ""]
        processToplevel $xsd
    } on error errMsg {
        sputce "Can't access\n[$import asXML]$errMsg"
        out
        exit
    } finally {
        set targetNS $savedTargetNS
    }
}

rproc xsd::include {include} {
    variable xsddata
    variable basedir
    
    set schemaLocation [$include @schemaLocation ""]
    if {$schemaLocation eq ""} {
        sputce "include: skiping because of missing schemaLocation\n[$include asXML]"
        return
    }
    if {[dict exists $xsddata included $schemaLocation]} {
        return
    }
    dict set xsddata included $schemaLocation ""
    try {
        set xsddoc [dom parse [xmlReadFile [file join $basedir $schemaLocation]]]
        $xsddoc selectNodesNamespaces {
            xsd "http://www.w3.org/2001/XMLSchema"
        }
        set xsd [$xsddoc documentElement]
        processToplevel $xsd
    } on error errMsg {
        sputce "Can't access\n[$include asXML]$errMsg"
        out
        exit
    }        
}

rproc xsd::redefine {redefine} {
    variable xsddata
    variable basedir

    set schemaLocation [$redefine @schemaLocation ""]
    if {$schemaLocation eq ""} {
        sputce "redefine: skiping because of missing schemaLocation\n[$redefine asXML]"
        return
    }
    if {[dict exists $xsddata redefined $schemaLocation]} {
        return
    }
    sputce "Global element redefine just includes, but does not actually redefine, so far"
    dict set xsddata redefined $schemaLocation ""
    try {
        set xsddoc [dom parse [xmlReadFile [file join $basedir $schemaLocation]]]
        $xsddoc selectNodesNamespaces {
            xsd "http://www.w3.org/2001/XMLSchema"
        }
        set xsd [$xsddoc documentElement]
        processToplevel $xsd
    } on error errMsg {
        sputce "Can't access\n[$redefine asXML]$errMsg"
        out
        exit
    }
}

proc xsd::processToplevel {xsd} {
    variable xsddata
    variable targetNS

    foreach toplevelelm [$xsd selectNodes xsd:*] {
        set localname [$toplevelelm localName]
        incr ::ecount($localname)
        switch $localname {
            "annotation" -
            "import" -
            "include" -
            "redefine" {
                $localname $toplevelelm
            }
            "annotation" {
                annotation $toplevelelm
            }
            default {
                set name [$toplevelelm @name ""]
                incr ::tlc($localname)
                if {$name eq ""} {
                    sputce "Global element $localname without name"
                    continue
                }
                dict set xsddata namespace $targetNS $localname $name xsd $toplevelelm
            }
        }
        out
    }
}

proc xsd::mapXsdTypeToSchema {type} {
    variable xsd2schemaName

    switch $type {
        "base64Binary" -
        "decimal" -
        "ID" -
        "IDREF" -
        "Name" -
        "NCName" -
        "NMTOKEN" -
        "QName" {
            return [dict get $xsd2schemaName $type]
        }
        "anyURI" -
        "byte" -
        "double" -
        "ENTITIES" -
        "ENTITY" -
        "float" -
        "gDay" -
        "gMonth" -
        "gMonthDay" -
        "gYear" -
        "gYearMonth" -
        "int" -
        "long" -
        "NOTATION" -
        "short" {
            return "tcl ::xsd::tclxsdtypes $type"
        }
        "IDREFS" {
            return "split idref"
        }
        "language" {
            return {regexp {^[a-zA-Z]{1,8}(-[a-zA-Z0-9]{1,8})*$}}
        }
        "NMTOKENS" {
            return "split nmtoken"
        }
        "boolean" -
        "date" -
        "dateTime" -
        "double" -
        "duration" -
        "hexBinary" -
        "integer" -
        "negativeInteger" -
        "nonNegativeInteger" -
        "nonPositiveInteger" -
        "positiveInteger" -
        "unsignedByte" -
        "unsignedInt" -
        "unsignedLong" -
        "unsignedShort" -
        "time" {
            return $type
        }
        "normalizedString" {
            incr count(normalizedString)
        }
        "string" {
            incr count(string)
        }
        "token" {
            incr count(token)
        }
    }
    return ""
}

rproc xsd::restriction {node} {
    variable xsddata
    variable targetNS
    variable currentBase

    set savedCurrentBase $currentBase
    set currentBase [$node @base $currentBase]
    set base $currentBase
    lassign [resolveFQ $base $node] typens type
    set context [[$node parentNode] localName] 
    switch $context {
        "simpleContent" -
        "simpleType" {
            if {$typens eq "http://www.w3.org/2001/XMLSchema"} {
                # Derived from an xsd base type
                #
                # The first restiction element child may be an annotation.
                set firstxsdchild [$node selectNodes {xsd:*[1]}]
                if {$firstxsdchild != "" && [$firstxsdchild localName] eq "annotation"} {
                    annotation $firstxsdchild
                }
                set enumerations [$node selectNodes xsd:enumeration]
                if {[llength $enumerations]} {
                    sput "enumeration \{"
                    incr level
                    foreach enumeration $enumerations {
                        sput [::list [$enumeration @value]]
                    }
                    incr level -1
                    sput "\}"
                    set currentBase $savedCurrentBase
                    return
                }
                set tdomtype [mapXsdTypeToSchema $type]
                if {$tdomtype ne ""} {
                    sput $tdomtype
                }
            } else {
                # Derived from another simple Type
                if {$typens eq ""} {
                    set typens $targetNS
                }
                if {$typens ne ""} {
                    sput "type [dict get $xsddata nsprefix $typens]:$type"
                } else {
                    sput "type $type"
                }
            }
            if {$context eq "simpleContent"} {
                sputce "simpleContent restriction to finalize"
            }
        }
        "complexContent" {
            sputce "complexType restriction to do"
        }
    }
    # foreach child [$node selectNodes xsd:*] {
    #     [$child localName] $child
    # }
    set currentBase $savedCurrentBase
}

rproc xsd::enumeration {node} {
    # Already handled by code in xsd::restriction.
    # Just do nothing
}

rproc xsd::fractionDigits {node} {

}

rproc xsd::length {node} {
    # TODO Special handling of xs:hexBinary and xs:base64Binary and
    # list types.
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    sput "length \{[$node @value]\}"
}

rproc xsd::maxExclusive {node} {

}

rproc xsd::maxInclusive {node} {

}

rproc xsd::maxLength {node} {
    # TODO Special handling of xs:hexBinary and xs:base64Binary and
    # list types.
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    sput "maxLength [$node @value]"
}

rproc xsd::minExclusive {node} {

}

rproc xsd::minInclusive {node} {

}

rproc xsd::minLength {node} {
    # TODO Special handling of xs:hexBinary and xs:base64Binary and
    # list types.
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    sput "minLength [$node @value]"
}

rproc xsd::pattern {node} {
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    sput "regexp \{^[$node @value]$\}"
}

rproc xsd::totalDigits {node} {

}

rproc xsd::whiteSpace {node} {

}

rproc xsd::union {node} {
    sput "oneOf \{"
    incr level
    set memberTypes [$node @memberTypes ""]
    if {$memberTypes ne ""} {
        foreach memberType $memberTypes {

        }
    } else {
        foreach child [$node selectNodes xsd:*] {
            [$child localName] $child
        }
    }
    incr level -1
    sput "\}"
}

rproc xsd::list {node} {
    sput "split \{"
    incr level
    set itemType [$node @itemType ""]
    if {$itemType ne ""} {
        
    } else {
        foreach child [$node selectNodes xsd:*] {
            [$child localName] $child
        }
    }
    incr level -1
    sput "\}"
}

proc xsd::processGlobalSimpleTypes {} {
    variable xsddata
    variable targetNS
    variable level
    
    incr level
    dict for {targetNS nsdata} [dict get $xsddata namespace] {
        if {![dict exists $nsdata simpleType]} continue
        dict for {simpleType stdata} [dict get $nsdata simpleType] {
            set xsd [dict get $stdata xsd]
            if {$targetNS ne ""} {
                set result "deftexttype [dict get $xsddata nsprefix $targetNS]:$simpleType \{\n"
            } else {
                set result "deftexttype $simpleType \{\n"
            }
            foreach child [$xsd selectNodes xsd:*] {
                [$child localName] $child
            }
            append result  "\}"
            out
        }
    }
    incr level -1
}

rproc xsd::sequence {node} {
    set insideChoice 0
    if {[[$node parentNode] localName] eq "choice"} {
        set insideChoice 1
        sput "group [getQuant $node] \{"
        incr level
    }
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    if {$insideChoice} {
        sput "\}"
        incr level -1
    }
}

rproc xsd::choice {node} {
    sput "choice [getQuant $node] \{"
    incr level
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    incr level -1
    sput "\}"
}

rproc xsd::all {node} {
    sput "interleave [getQuant $node] \{"
    incr level
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    incr level
    sput "\}"
}

rproc xsd::group {node} {
    set ref [$node @ref ""]
    lassign [resolveFQ $ref $node] ns name
    sput "ref $name [getQuant $node]"
}

rproc xsd::elementWorker {node} {
    variable xsddata
    variable targetNS

    set type [$node @type ""]
    if {$type ne ""} {
        # Refers to type definition
        # First check, if the type name is full qualified
        if {[string first : $type] > -1} {
            lassign [split $type :] prefix type
            set thisns [nsfromprefix $node $prefix]
        } else {
            set thisns $targetNS
        }
        # Check for text only element with basic data type
        if {$thisns eq "http://www.w3.org/2001/XMLSchema"} {
            set texttypecode [mapXsdTypeToSchema $type]
            if {$texttypecode eq ""} {
                append result "text\n"
            } elseif {[string first " " $texttypecode] > -1} {
                append result "\{\n"
                incr level
                sput "text \{$texttypecode\}"
                incr level -1
                sput "\}"
            } else {
                append result "\{text $texttypecode\}\n"
            }
            return
        }
        if {[dict exists $xsddata namespace $thisns simpleType $type]} {
            sput "\{text type [prefix $thisns]:$type\}"
            return
        } elseif {[dict exists $xsddata namespace $thisns complexType $type]} {
            set xsd [dict get $xsddata namespace $thisns complexType $type xsd]
            set attdefinitions [$xsd selectNodes {
                xsd:attribute
                | xsd:atttributeGroup
            }]
            set haveScriptStart 0
            if {[llength $attdefinitions]} {
                set haveScriptStart 1
                append result "\{\n"
                incr level
                foreach child $attdefinitions {
                    [$child localName] $child
                }
            }
            if {$thisns eq $targetNS} {
                if {$haveScriptStart} {
                    sput "ref $type"
                } else {
                    append result "\{ref $type\}\n"
                }
            } else {
                if {!$haveScriptStart} {
                    append result "\{\n"
                    incr level
                }
                sput "namespace [dict get $xsddata nsprefix $thisns] \{ref $type\}"
                if {!$haveScriptStart} {
                    incr level -1
                    sput "\}"
                }
            }
            if {$haveScriptStart} {
                incr level -1
                sput "\}"
            }
            return
        } else {
            sputce "# Element type [$node @type] $type $thisns not found"
        }
        return
    } else {
        # Local defined type
        append result "\{\n"
        incr level
        foreach child [$node selectNodes xsd:*] {
            [$child localName] $child
        }
        incr level -1
        sput "\}"
    }
}

rproc xsd::element {node} {
    variable xsddata
    variable targetNS
    
    if {[$node hasAttribute ref]} {
        lassign [resolveFQ [$node @ref] $node] ns name
        if {$targetNS eq $ns} {
            sput "element $name [getQuant $node]"
        } else {
            sput "namespace [$dict get $xsddata nsprefix $ns] \{"
            incr level
            sput "element $name [getQuant $node]"
            incr level
            sput "\}"
        }
        return
    }
    sputnnl "element [$node @name] [getQuant $node] "
    elementWorker $node
}

rproc xsd::simpleType {node} {
    set forattribute 1
    if {[[$node parentNode] localName] ne "attribute"} {
        set forattribute 0
        sput "text \{"
        incr level
    }
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    if {!$forattribute} {
        incr level -1
        sput "\}"
    }
}

rproc xsd::complexType {node} {
    if {[$node @mixed 0]} {
        set atts ""
        foreach child [$node selectNodes xsd:*] {
            switch [$child localName] {
                "attribute" -
                "attributeGroup" {
                    lappend atts $child
                }
                "choice" {
                    set quant [getQuant $child]
                    if {$quant eq "*"} {
                        sput "mixed \{"
                    } else {
                        sput "mixed $quant \{"
                    }
                    incr level
                    foreach childchild [$child selectNodes xsd:*] {
                        [$childchild localName] $childchild
                    }
                    incr level -1
                    sput "\}"
                }
                "sequence" {
                    foreach childchild [$child selectNodes xsd:*] {
                        [$childchild localName] $childchild
                    }
                }
                default {
                    [$child localName] $child
                }
            }
        }
        if {![$node selectNodes {
            count(xsd:*[local-name() != 'annotation'
                        and local-name() != 'attribute'
                        and local-name() != 'attributeGroup'])
        }]} {
            # mixed complexType with attributes only
            sput text
        }
        foreach child $atts {
            [$child localName] $child
        }
    } else {
        foreach child [$node selectNodes {
            xsd:*[local-name() != 'attribute' and local-name() != 'attributeGroup']
        }] {
            [$child localName] $child
        }
        foreach child [$node selectNodes {
            xsd:*[local-name() = 'attribute' or local-name() = 'attributeGroup']
        }] {
            [$child localName] $child
        }
    }
}

rproc xsd::simpleContent {node} {
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
}

rproc xsd::complexContent {node} {
    set mixed [$node @mixed 0]
    if {$mixed} {
        sput "mixed \{"
        incr level
    }
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
    if {$mixed} {
        incr level -1
        sput "\}"
    }
}

rproc xsd::anyAttribute {node} {
    sputce "anyAttribute not implemented"
}

proc xsd::getSimpleType {ns type} {

}

rproc xsd::extension {node} {
    variable xsddata
    variable currentBase
    
    set savedCurrentBase $currentBase
    set currentBase [$node @base $currentBase]
    set base $currentBase
    lassign [resolveFQ $base $node] typens type
    set context [[$node parentNode] localName] 
    switch $context {
        "simpleContent" {
            if {![dict exists $xsddata namespace $typens simpleType $type]} {
                sputce "simpleType $type in $namespace $typens not found for extension"
                return
            }
            set xsd [dict get $xsddata namespace $typens simpleType $type xsd]
        }
        "complexContent" {
            if {![dict exists $xsddata namespace $typens complexType $type]} {
                sputce "complexType $type in $namespace $typens not found for extension"
                return
            }
            set xsd [dict get $xsddata namespace $typens complexType $type xsd]
            
        }
    }
    foreach child [$xsd selectNodes {
        xsd:*[local-name() != 'attribute' and local-name() != 'attributeGroup']
    }] {
        [$child localName] $child
    }
    foreach child [$node selectNodes {
        xsd:*[local-name() = 'attribute' or local-name() = 'attributeGroup']
    }] {
        [$child localName] $child
    }
    set currentBase $savedCurrentBase
    sputce "extension not implemented"
}

rproc xsd::key {node} {
    sputce "key not implemented"
}

rproc xsd::keyref {node} {
    sputce "keyref not implemented"
}

rproc xsd::unique {node} {
    sputce "unique not implemented"
}

rproc xsd::any {node} {
    set quant [getQuant $node]
    # Could be only annotation, in valid schema
    foreach child [$node selectNodes xsd:*] {
        sput [[$child localName] $child]
    }
    set processContents [$node @processContents "strict"]
    if {$processContents ne "skip"} {
        sputce "Loading foreign schemas is not supported."
        sputce "Therefor the any processContents attribute value\
                              \"$processContents\" will be effectively\
                              the same as \"skip\"\n"
    }
    set namespace [$node @namespace "##any"]
    switch $namespace {
        "##any" {
            sput "any $quant"
        }
        "##other" {
            sputce "The any namespace attribute value\
                           \"##other\" not supported.\n"
            sputce  "Translated into \"##any\"\n"
            sput "any $quant"
        }
        default {
            set nsonly {}
            foreach ns $namespace {
                switch $ns {
                    "##targetNamespace" {
                        
                    }
                    "##local" {
                        
                    }
                    default {
                        lappend nsonly $ns
                    }
                }
            }
            if {[llength $nsonly]} {
                if {[llength $nsonly] == 1} {
                    sput "any [lindex $nsonly 0] $quant"
                } else {
                    append result "choice $quant \{\n"
                    foreach ns $nsonly {
                        append result "any $ns\n"
                    }
                    append result "\}"
                }
            }
        }
    }
    return $result
}

rproc xsd::attributeGroup {node} {
    variable xsddata

    if {[$node hasAttribute ref]} {
        lassign [resolveFQ [$node @ref] $node] ns name
        set node [dict get $xsddata namespace $ns attributeGroup $name xsd]
    }
    foreach child [$node selectNodes xsd:*] {
        [$child localName] $child
    }
}

rproc xsd::textType {node {start ""}} {
    variable xsddata
    
    set type [$node @type]
    lassign [resolveFQ $type $node] typens typename
    # Check for basic data type
    if {$typens eq "http://www.w3.org/2001/XMLSchema"} {
        set texttypecode [mapXsdTypeToSchema $typename]
        sputnnl $start
        if {$texttypecode ne ""} {
            if {[string first " " $texttypecode] > -1} {
                sput " \{"
                incr level
                foreach line [split $texttypecode "\n"] {
                    sput $line
                }
                incr level -1
                sput "\}"
            } else {
                append result " $texttypecode\n"
            }
        } else {
            append result "\n"
        }
    } else {
        if {$typens eq ""} {
            sput "$start type $typename"
        } else {
            sput "$start type [dict get $xsddata nsprefix $$typens]:$typename"
        }
    }
}    

rproc xsd::attribute {node} {
    variable xsddata
    
    switch [$node @use "optional"] {
        "prohibited" {
            return "# LOOK_AT: attribute use 'prohibited' not supported"
        }
        "optional" {
            set quant ?
        }
        "required" {
            set quant !
            
        }
    }
    if {[$node hasAttribute ref]} {
        lassign [resolveFQ [$node @ref] $node 1] ns name
        set node [dict get $xsddata namespace $ns attribute $name xsd]
    } else {
        lassign [resolveFQ [$node @name] $node 1] ns name
    }
    if {$ns eq ""} {
        set start "attribute $name $quant"
    } else {
        set start "nsattribute $name [dict get $xsddata nsprefix $ns] $quant"
    }
    if {[$node hasAttribute fixed]} {
        sput "$start \{fixed [::list [$node @fixed]]\}"
        return
    }
    if {[$node hasAttribute type]} {
        textType $node $start
    } else {
        set childs [$node selectNodes xsd:*]
        if {[llength $childs]} {
            # Local definition by simpleType
            sput "$start \{"
            incr level
            foreach child [$node selectNodes xsd:*] {
                [$child localName] $child
            }
            incr level -1
            sput "\}"
        } else {
            sput $start
        }
    }
}

proc xsd::notation {node} {
    return "# LOOK_AT: notation not supported"
}

proc xsd::processGlobalGroup {} {
    variable xsddata
    variable targetNS
    variable level
    
    incr level
    dict for {targetNS nsdata} [dict get $xsddata namespace] {
        if {![dict exists $nsdata group]} continue
        dict for {group data} [dict get $nsdata group] {
            set xsd [dict get $data xsd]
            append result "\ndefpattern $group [dict get $xsddata nsprefix $targetNS] \{\n"
            foreach child [$xsd selectNodes xsd:*] {
                append result "[[$child localName] $child]\n"
            }
            append result "\}"
            out
        }
    }
    incr level -1
}

proc xsd::processGlobalComplexTypes {} {
    variable xsddata
    variable targetNS
    variable level

    incr level
    dict for {targetNS nsdata} [dict get $xsddata namespace] {
        if {![dict exists $nsdata complexType]} continue
        dict for {complexType ctdata} [dict get $nsdata complexType] {
            set xsd [dict get $ctdata xsd]
            set result "defpattern $complexType [dict get $xsddata nsprefix $targetNS] \{\n"
            foreach child [$xsd selectNodes {
                xsd:*[local-name() != 'attribute' and local-name() != 'attributeGroup']
            }] {
                [$child localName] $child
            }
            append result "\}"
            out
        }
    }
    incr level -1
}

proc xsd::processGlobalElements {} {
    variable xsddata
    variable targetNS

    set result ""
    dict for {targetNS nsdata} [dict get $xsddata namespace] {
        if {![dict exists $nsdata element]} continue
        dict for {element elmdata} [dict get $nsdata element] {
            set xsd [dict get $elmdata xsd]
            set result "defelement $element $targetNS "
            elementWorker $xsd
            out 1
        }
    }
}

proc xsd::generateSchema {file} {
    variable level 0
    variable targetNS ""
    variable basedir
    variable xsddata  [dict create]
    variable output
    variable schema ""
    variable currentBase ""
    
    set basedir [file dirname [file normalize [lindex $file 0]]]
    set xsddoc [dom parse [xmlReadFile [lindex $file 0]]]
    $xsddoc selectNodesNamespaces {
        xsd "http://www.w3.org/2001/XMLSchema"
    }
    set xsd [$xsddoc documentElement]
    set targetNS [$xsd @targetNamespace ""]

    xsd::prolog
    xsd::processToplevel $xsd
    xsd::processNamespaces
    xsd::processGlobalSimpleTypes
    xsd::processGlobalGroup
    xsd::processGlobalComplexTypes
    xsd::processGlobalElements
    if {$output eq "collect"} {
        return $schema
    }
}

if {[info exists argv0] && [file tail [info script]] eq [file tail $argv0]} {
    if {$argc != 1} {
        puts stderr "Usage: $argv0 <xsd-file>"
        exit 1
    }
    set ::xsd::output ""
    xsd::generateSchema [lindex $argv 0]
}
