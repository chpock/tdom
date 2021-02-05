# This helper and test script works with the w3c xsd test suite.
# Get it from
# https://github.com/w3c/xsdtests/archive/master.zip

package require tdom
namespace import tdom::*

if {[info commands ::tdom::xmlReadFile] == ""} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

if {$argc < 1} {
    puts stderr "usage: $argv0 <catalog-file> ?args?"
    exit 1
}

source xsd2schema.tcl

set schemaFileCounter 0

proc worker {xsddoc path xmlfiles} {
    
    # if {[$doc selectNodes {count(//xsd:simpleContent/xsd:restriction[xsd:attribute])}]} {
    #     puts $path
    # }
}

array set query {
    allxsd {/ts:testSet/ts:testGroup/ts:schemaTest/ts:schemaDocument}
    validxsd {/ts:testSet/ts:testGroup/ts:schemaTest[ts:expected/@validity = 'valid']/ts:schemaDocument}
    invalidxsd {/ts:testSet/ts:testGroup/ts:schemaTest[ts:expected/@validity = 'invalid']/ts:schemaDocument}
    invalidxml {/ts:testSet/ts:testGroup
        [ts:schemaTest[ts:expected/@validity = 'valid']
         and ts:instanceTest[ts:expected/@validity = 'invalid']]}
    this {/ts:testSet/ts:testGroup/ts:schemaTest[count(ts:expected) > 1]/ts:schemaDocument}
}

proc walktestSet {file} {
    global query
    
    set base [file dir $file]
    set set [dom parse [xmlReadFile $file]]
    $set selectNodesNamespaces {
        ts "http://www.w3.org/XML/2004/xml-schema-test-suite/"
    }
    foreach testGroup [$set selectNodes $query(invalidxml)] {
        set xsdfile [file normalize \
                         [file join $base [$testGroup selectNodes {
                             string(ts:schemaTest/ts:schemaDocument/@xlink:href)
                         }]]]
        tdom::schema s
        if {[catch {
            s define [xsd::generateSchema $xsdfile]
        } errMsg]} {
            puts $::schemaFileCounter
            puts "xsdfile: $xsdfile"
            puts $::errorInfo
            exit
        }
        incr ::schemaFileCounter
        foreach instanceTest [$testGroup selectNodes {ts:intanceTest[ts:expected/@validity = 'invalid']}] {
            if {[s validatefile [file normalize [file join $base [$instanceTest selectNodes {string(ts:instanceDocument/@xlink:href)}]]]]} {
                incr ::wrongOK
            }
            s reset
        }
        # $doc selectNodesNamespaces {
        #     xsd "http://www.w3.org/2001/XMLSchema"
        # }
        # worker $doc $xsd
        # $doc delete
    }
    $set delete
}

proc walksuite {file} {
    set base [file dir $file]
    set doc [dom parse [xmlReadFile $file]]
    $doc selectNodesNamespaces {
        ts "http://www.w3.org/XML/2004/xml-schema-test-suite/"
    }
    puts "# of testsuites: [$doc selectNodes count(/ts:testSuite/ts:testSetRef)]"
    foreach testSet [$doc selectNodes /ts:testSuite/ts:testSetRef] {
        walktestSet [file join $base [$testSet @xlink:href]]
    }
}

walksuite [lindex $argv 0]
puts "# of looked at xsd files: $schemaFileCounter"
