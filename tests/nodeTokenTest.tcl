package require tdom

proc traverse4s {node} {
    set thisnode [$node firstChild]
    while {$thisnode ne ""} {
        if {[$thisnode hasChildNodes]} {
            traverse4s $thisnode
        }
        set thisnode [$thisnode nextSibling]
    }
}

proc traverse4e {node} {
    set thisnode [$node firstChild]
    while {isNodeToken($thisnode)} {
        if {[$thisnode hasChildNodes]} {
            traverse4e $thisnode
        }
        set thisnode [$thisnode nextSibling]
    }
}

proc traverse4 {node} {
    set thisnode [$node firstChild]
    while {[dom isNodeToken $thisnode]} {
        if {[$thisnode hasChildNodes]} {
            traverse4 $thisnode
        }
        set thisnode [$thisnode nextSibling]
    }
}

proc traverse3s {node} {
    $node firstChild thisnode
    while {$thisnode ne ""} {
        if {[$thisnode hasChildNodes]} {
            traverse3s $thisnode
        }
        $thisnode nextSibling thisnode
    }
}

proc traverse3e {node} {
    $node firstChild thisnode
    while {isNodeToken($thisnode)} {
        if {[$thisnode hasChildNodes]} {
            traverse3e $thisnode
        }
        set thisnode [$thisnode nextSibling]
    }
}

proc traverse3 {node} {
    $node firstChild thisnode
    while {[dom isNodeToken $thisnode]} {
        if {[$thisnode hasChildNodes]} {
            traverse3 $thisnode
        }
        $thisnode nextSibling thisnode
    }
}

proc traverse2e {node} {
    set thisnode [domNode $node firstChild]
    while {isNodeToken($thisnode)} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse2e $thisnode
        }
        set thisnode [domNode $thisnode nextSibling]
    }
}

proc traverse2s {node} {
    set thisnode [domNode $node firstChild]
    while {$thisnode ne ""} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse2s $thisnode
        }
        set thisnode [domNode $thisnode nextSibling]
    }
}

proc traverse2 {node} {
    set thisnode [domNode $node firstChild]
    while {[dom isNodeToken $thisnode]} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse2 $thisnode
        }
        set thisnode [domNode $thisnode nextSibling]
    }
}


proc traverse1e {node} {
    domNode $node firstChild thisnode
    while {isNodeToken($thisnode)} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse1e $thisnode
        }
        domNode $thisnode nextSibling thisnode
    }
}

proc traverse1as {node} {
    # Variant of traverse1 - don't check for
    # childNodes but recurse always
    domNode $node firstChild thisnode
    while {$thisnode ne ""} {
        traverse1as $thisnode
        domNode $thisnode nextSibling thisnode
    }
}

proc traverse1a {node} {
    # Variant of traverse1 - don't check for
    # childNodes but recurse always
    domNode $node firstChild thisnode
    while {[dom isNodeToken $thisnode]} {
        traverse1a $thisnode
        domNode $thisnode nextSibling thisnode
    }
}

proc traverse1s {node} {
    domNode $node firstChild thisnode
    while {$thisnode ne ""} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse1s $thisnode
        }
        domNode $thisnode nextSibling thisnode
    }
}

proc traverse1 {node} {
    domNode $node firstChild thisnode
    while {[dom isNodeToken $thisnode]} {
        if {[domNode $thisnode hasChildNodes]} {
            traverse1 $thisnode
        }
        domNode $thisnode nextSibling thisnode
    }
}

set fd [open ../tests/data/mondial-europe.xml]
set xml [read $fd]
foreach func {
    traverse1
    traverse1s
    traverse1e
    traverse2
    traverse2s
    traverse2e
    traverse3
    traverse3s
    traverse3e
    traverse4
    traverse4s
    traverse4e
} desc {
    "token, nodeObjVar, dom isNodeToken test"
    "token, nodeObjVar, string rep test"
    "token, nodeObjVar, expr test"
    "token, return value, dom isNodeToken test"
    "token, return value, string rep test"
    "token, return value, expr test"
    "nodeCmd, nodeObjVar, dom isNodeToken test"
    "nodeCmd, nodeObjVar, string rep test"
    "nodeCmd, nodeObjVar, expr test"
    "nodeCmd, return value, dom isNodeToken test"
    "nodeCmd, return value, string rep test"
    "nodeCmd, return value, expr test"
} token {
    1
    1
    1
    1
    1
    1
    0
    0
    0
    0
    0
    0
} {
    set doc [dom parse $xml]
    if {$token} {
        set root [domDoc $doc documentElement]
    } else {
        set root [$doc documentElement]
    }
    puts "$desc ($func)"
    puts [time {$func $root}]
    puts ""
    $doc delete
}    
