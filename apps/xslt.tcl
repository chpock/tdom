#----------------------------------------------------------------------------
#   Copyright (c) 1999-2001 Jochen Loewer (loewerj@hotmail.com)   
#----------------------------------------------------------------------------
#
#   $Id$
#
#
#   A simple command line XSLT processor using tDOMs XSLT engine.
#
#
#   The contents of this file are subject to the Mozilla Public License
#   Version 2.0 (the "License"); you may not use this file except in
#   compliance with the License. You may obtain a copy of the License at
#   http://www.mozilla.org/MPL/
#
#   Software distributed under the License is distributed on an "AS IS"
#   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
#   License for the specific language governing rights and limitations
#   under the License.
#
#   The Original Code is tDOM.
#
#   The Initial Developer of the Original Code is Jochen Loewer
#   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
#   Jochen Loewer. All Rights Reserved.
#
#   Contributor(s):            
#
#
#
#   written by Rolf Ade
#   August, 2001
#
#----------------------------------------------------------------------------

package require tdom 0.8.1

# The following is not needed, given, that tDOM is correctly
# installed. This code only ensures, that the tDOM script library gets
# sourced, if the script is called with a tcldomsh out of the build
# dir of a complete tDOM source installation.
if {[lsearch [namespace children] ::tDOM] == -1} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

# Import the tDOM helper procs
namespace import tDOM::*

# Argument check
if {[llength $argv] != 2 && [llength $argv] != 3} {
    puts stderr "usage: $argv0 <xml-file> <xslt-file>\
                        ?output_method (asHTML|asXML|asText)?"
    exit 1
}
foreach { xmlFile xsltFile outputOpt } $argv break


# This is the callback proc for xslt:message elements. This proc is
# called once every time an xslt:message element is encountered during
# processing the stylesheet. The callback proc simply sends the text
# message to stderr.
proc xsltmsgcmd {msg terminate} {
    puts stderr "xslt message: $msg"
}

# Set this variable to a true value if you want to see on stderr with
# what arguments the scripted external entities handler will be
# called.
set ::tDOM::extRefHandlerDebug 0

set xmlfd [xmlOpenFile $xmlFile]
set xmldoc [dom parse -baseurl [baseURL $xmlFile] \
                      -externalentitycommand extRefHandler \
                      -keepEmpties \
                      -channel $xmlfd]
close $xmlfd

set xsltfd [xmlOpenFile $xsltFile]
dom setStoreLineColumn 1
set xsltdoc [dom parse -baseurl [baseURL $xsltFile] \
                       -externalentitycommand extRefHandler \
                       -keepEmpties \
                       -channel $xsltfd]
dom setStoreLineColumn 0
close $xsltfd

if {[catch {$xmldoc xslt -xsltmessagecmd xsltmsgcmd $xsltdoc resultDoc} \
         errMsg]} {
    puts stderr $errMsg
    exit 1
}

if {$outputOpt == ""} {
    set outputOpt [$resultDoc getDefaultOutputMethod]
}

set doctypeDeclaration 0
if {[$resultDoc systemId] != ""} {
    set doctypeDeclaration 1
}

switch $outputOpt {
    asXML -
    xml  {
        if {[$resultDoc indent]} {
            set indent 4
        } else {
            set indent no
        }
        $resultDoc asXML -indent $indent -escapeNonASCII \
            -channel stdout -doctypeDeclaration $doctypeDeclaration
    }
    asHTML -
    html {
        $resultDoc asHTML -escapeNonASCII -htmlEntities \
            -channel stdout -doctypeDeclaration $doctypeDeclaration
    }
    asText -
    text {
        puts [$resultDoc asText]
    }
    default {
        puts stderr "Unknown output method '$outputOpt'!"
        exit 1
    }
}

proc exit args {}
