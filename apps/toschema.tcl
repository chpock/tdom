
package require tdom
package require uri

proc fromDTD_startDoctypeDecl {args} {
    puts "fromDTD_startDoctypeDecl: $args"
}

proc fromDTD_endDoctypeDecl {args} {
    puts "fromDTD_endDoctypeDecl: $args"
}

proc fromDTD_elementDecl {args} {
    puts "fromDTD_elementDecl: $args"
}

proc fromDTD_attlistDecl {args} {
    puts "fromDTD_attlistDecl: $args"
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
