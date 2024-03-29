# man2ipf2.tcl --
#
# This file defines procedures that are used during the second pass of the
# man page to IPF conversion process. It is sourced by man2ipf.tcl.
#
# Copyright (c) 1996 by Sun Microsystems, Inc.
# Copyright (c) 1999-2002 by Illya Vaes
#
# SCCS: @(#) man2ipf2.tcl 1.2 96/03/21 10:48:30
#

# Global variables used by these scripts:
#
# NAME_file -	array indexed by NAME and containing file names used
#		for hyperlinks.
#
# textState -	state variable defining action of 'text' proc.
#
# nestStk -	stack oriented list containing currently active
#		IPF tags (UL, OL, DL). Local to 'nest' proc.
#
# inDT -	set by 'TPmacro', cleared by 'newline'. Used to insert
#		the <DT> tag while in a dictionary list <DL>.
#
# curFont -	Name of special font that is currently in
#		use.  Null means the default paragraph font
#		is being used.
#
# file -	Where to output the generated IPF.
#
# fontStart -	Array to map font names to starting sequences.
#
# fontEnd -	Array to map font names to ending sequences.
#
# noFillCount -	Non-zero means don't fill the next $noFillCount
#		lines: force a line break at each newline.  Zero
#		means filling is enabled, so don't output line
#		breaks for each newline.
#
# footer -	info inserted at bottom of each page. Normally read
#		from the xref.tcl file
	
# initGlobals --
#
# This procedure is invoked to set the initial values of all of the
# global variables, before processing a man page.
#
# Arguments:
# None.

proc initGlobals {} {
    global file noFillCount textState
    global fontStart fontEnd curFont inTable inPRE charCnt dummyHeader

    nest init
    set dummyHeader 0
    set inTable 0
    set inPRE 0
    set textState 0
    set curFont ""
    set fontStart(Code) ":hp2."
    set fontStart(Emphasis) ":hp1."
    set fontEnd(Code) ":ehp2."
    set fontEnd(Emphasis) ":ehp1."
    set noFillCount 0
    set charCnt 0
    setTabs 0.5i
}


# beginFont --
#
# Arranges for future text to use a special font, rather than
# the default paragraph font.
#
# Arguments:
# font -		Name of new font to use.

proc beginFont font {
    global curFont file fontStart

# Tclsh 8.3.3: if {[string equal $curFont $font]} { #}
    if {$curFont == $font} {
	return
    }
    endFont
    puts -nonewline $file $fontStart($font)
    set curFont $font
}


# endFont --
#
# Reverts to the default font for the paragraph type.
#
# Arguments:
# None.

proc endFont {} {
    global curFont file fontEnd

# Tcl 8.3.3:  if {[string compare $curFont ""]} { #}
    if {$curFont != ""} {
    puts -nonewline $file $fontEnd($curFont)
    set curFont ""
    }
}



# text --
#
# This procedure adds text to the current paragraph.  If this is
# the first text in the paragraph then header information for the
# paragraph is output before the text.
# Keep length of lines below 255.
#
# Arguments:
# string -		Text to output in the paragraph.

proc text string {
    global file textState inTable inDT charCnt beforeSynopsis

    set pos [string first "\t" $string]
    if {$pos >= 0} {
    	text [string range $string 0 [expr $pos-1]]
    	tab
    	text [string range $string [expr $pos+1] end]
	return    	
    }
    incr charCnt [string length $string]
# Tcl 8.3.3:
#set string [string map [list \
#      "\\"        "\\\\" \
#      "\{"        "\\\{" \
#      "\}"        "\\\}" \
#      "\t"        {\tab } \
#      ''          "\\rdblquote " \
#      ``          "\\ldblquote " \
#      ] $string]
    regsub -all {&} $string {\&amp.}  string
    regsub -all {\.\.\.} $string {\&period.\&period.\&period.}  string
    regsub -all {<} $string {\&lt.}  string
    regsub -all {>} $string {\&gt.}  string
    regsub -all {``} $string {\&odq.}  string
    regsub -all {''} $string {\&cdq.}  string
    regsub -all {"} $string {\&cdq.}  string
    regsub -all {:} $string {\&colon.}  string
    if {$inTable == 1} {
        #regsub -all {  +} $string "\n:c." string
        #set string ":row.\n:c.$string"
        regsub -all {  +} $string "\t" string
        set string ":li.$string"
    }
    switch $textState {
        NAME {
            # Only put in index if this is the "NAME" section before SYNOPSIS
            if {$beforeSynopsis} {
                foreach i [split $string ","] {
#                    lappend NAME_file([string trim $i]) $curFile
                     puts $file ":i1.[string trim $i]"
                }
            }
        }
	REF { 
	    if {$inDT == {}} {
		set string [insertRef $string]
	    }
	}
	SEE { 
	    global NAME_file
	    foreach i [split $string] {
		if {![regexp -nocase {^[a-z_]+} [string trim $i] i ]} {
# 		    puts "Warning: $i in SEE ALSO not found"
		    continue
		}
		if {![catch {set ref $NAME_file($i)} ]} {
		    regsub $i $string ":link reftype=hd refid=$ref.$i:elink." string
		}
	    }
	}
    }
    #!!! Don't puts more than 255 characters, since IPFC can't handle that
    # but break on a space
    # string length on the list of functions in the manpage of CrtChannel.3
    # returns 248 while it is > 255, so use 176.
    while {[string length $string] > 176} {
        set indexSpace [ string last { } [string range $string 0 175] ]
        puts $file [ string range $string 0 [ expr $indexSpace - 1]]
        set string [ string range $string $indexSpace end]
    }
    puts -nonewline $file "$string"
}



# insertRef --
#
#
# Arguments:
# string -		Text to output in the paragraph.

proc insertRef string {
    global NAME_file self
    set path {}
    if ![catch {set ref $NAME_file([string trim $string])} ] {
	if {"$ref" != $self} {
	    set string ":link reftype=hd ref='$ref'.$string:elink."
#	    puts "insertRef: $self $ref.html ---$string--"
	}
    }
    return $string
}



# macro --
#
# This procedure is invoked to process macro invocations that start
# with "." (instead of ').
#
# Arguments:
# name -		The name of the macro (without the ".").
# args -		Any additional arguments to the macro.

proc macro {name args} {
    switch $name {
	AP {
            if {[llength $args] != 3 && [llength $args] != 2} {
                puts stderr "Bad .AP macro: .$name [join $args " "]"
            }
	    setTabs {1.25i 2.5i 3.75i}
	    TPmacro {}
	    font B
	    text "[lindex $args 0]  "
	    font I
	    text "[lindex $args 1]"
	    font R
            if {[llength $args] == 3} {
                text "([lindex $args 2])"
            }
	    newline
	}
	AS {}				;# next page and previous page
	br {
	    lineBreak	
	}
        B  {
	    font B
	    formattedText "$args"
            font R
        }
	BS {}
	BE {}
	CS {				;# code section
	    global file noFillCount inPRE dummyHeader
            #puts $file {:font facename=Courier.}
	    puts $file ":h3 res=${dummyHeader}.:cgraphic."
	    set inPRE 1
            incr dummyHeader
	}
	CE {	;# code section END
	    global file noFillCount inPRE dummyHeader
	    puts $file ":ecgraphic."
            #puts $file {:font facename=default.}
            set dummyHeader [expr $dummyHeader - 1]
	    set inPRE 0
	}
	DS {
	    global file noFillCount inPRE
	    puts -nonewline $file ":cgraphic."
	    set noFillCount 10000000
	    set inPRE 1
	}
	DE {
	    global file noFillCount inPRE
	    puts $file ":ecgraphic."
	    set inPRE 0
	    set noFillCount 0
	}
	fi {
	    global noFillCount
	    set noFillCount 0
	}
        I  {
	    font I
	    formattedText "$args"
            font R
        }
	IP {
	    IPmacro $args
	}
	LP {
	    nest decr
	    nest incr
	    newPara
	}
	ne {
	}
	nf {
	    global noFillCount
	    set noFillCount 1000000
	}
	OP {
	    global inDT file inPRE 
	    if {[llength $args] != 3} {
		puts stderr "Bad .OP macro: .$name [join $args " "]"
	    }
	    nest para dl dt
	    set inPRE 1
	    puts -nonewline $file ":cgraphic."
	    setTabs 4c
	    text "Command-Line Name:"
	    tab
	    font B
	    set x [lindex $args 0]
	    regsub -all {\\-} $x - x
	    text $x
	    newline
	    font R
	    text "Database Name:"
	    tab
	    font B
	    text [lindex $args 1]
	    newline
	    font R
	    text "Database Class:"
	    tab
	    font B
	    text [lindex $args 2]
	    font R
	    puts -nonewline $file ":ecgraphic.\n.br"
	    set inDT "\n:dd."			;# next newline writes inDT 
	    set inPRE 0
	    newline
	}
        P {
            newPara
        }
	PP {
	    nest decr
	    nest incr
	    newPara
	}
	RE {
            global file
	    nest decr    
            puts $file {:font facename=default.}
	}
	RS {
            global file
            puts $file {:font facename=Courier.}
	    nest incr
	}
	SE {
	    global noFillCount textState inPRE file

	    font R
	    puts -nonewline $file ":ecgraphic."
	    set inPRE 0
	    set noFillCount 0
	    nest reset
	    newPara
	    text "See the "
	    font B
	    set temp $textState
	    set textState REF
	    text options
	    set textState $temp
	    font R
	    text " manual entry for detailed descriptions of the above options."
	}
	SH {
	    SHmacro $args
	}
	SO {
	    global noFillCount inPRE file

	    SHmacro "STANDARD OPTIONS"
	    setTabs {4c 8c 12c}
	    set noFillCount 1000000
	    puts -nonewline $file ":cgraphic."
	    set inPRE 1
	    font B
	}
	TS {	;# Table Start
	    global file noFillCount inTable dummyHeader
            #puts $file {:font facename=Courier.}
	    #puts $file ":table cols='27 5 6'."
            puts $file ":ul compact."
	    set inTable 1
            incr dummyHeader
	}
	TE {	;# Table End
	    global file noFillCount inTable dummyHeader
	    puts $file ":eul."
	    #puts $file ":etable."
            #puts $file {:font facename=default.}
            set dummyHeader [expr $dummyHeader - 1]
	    set inTable 0
	}
	so {
	    if {$args != "man.macros"} {
		puts stderr "Unknown macro: .$name [join $args " "]"
	    }
	}
	sp {					;# needs work
	    if {$args == ""} {
		set count 1
	    } else {
		set count [lindex $args 0]
	    }
	    while {$count > 0} {
		lineBreak
		incr count -1
	    }
	}
	ta {
	    setTabs $args
	}
	TH {
	    THmacro $args
	}
	TP {
	    TPmacro $args
	}
	UL {					;# underline
	    global file
	    puts -nonewline $file ":hp7."	;# Bold and underlined
	    text [lindex $args 0]
	    puts -nonewline $file ":ehp7."
	    if {[llength $args] == 2} {
		text [lindex $args 1]
	    }
	}
	VE {
#	    global file
#	    puts -nonewline $file "</FONT>"
	}
	VS {
#	    global file
#	    if {[llength $args] > 0} {
#		puts -nonewline $file "<BR>"
#	    }
#	    puts -nonewline $file "<FONT COLOR=\"GREEN\">"
	}
	default {
	    puts stderr "Unknown macro: .$name [join $args " "]"
	}
    }

#	global nestStk; puts "$name [format "%-20s" $args] $nestStk"
#	flush stdout; flush stderr
}


# font --
#
# This procedure is invoked to handle font changes in the text
# being output.
#
# Arguments:
# type -		Type of font: R, I, B, or S.

proc font type {
    global textState
    switch $type {
	P -
	R {
	    endFont
	    if {$textState == "REF"} {
		set textState INSERT
	    }
	}
	B {
	    beginFont Code
	    if {$textState == "INSERT"} {
		set textState REF
	    }
	}
	I {
	    beginFont Emphasis
	}
	S {
	}
	default {
	    puts stderr "Unknown font: $type"
	}
    }
}



# formattedText --
#
# Insert a text string that may also have \fB-style font changes
# and a few other backslash sequences in it.
#
# Arguments:
# text -		Text to insert.

proc formattedText text {
#	puts "formattedText: $text"
    while {$text != ""} {
	set index [string first \\ $text]
	if {$index < 0} {
	    text $text
	    return
	}
	text [string range $text 0 [expr $index-1]]
	set c [string index $text [expr $index+1]]
	switch -- $c {
	    f {
		font [string index $text [expr $index+2]]
		set text [string range $text [expr $index+3] end]
	    }
	    e {
		text \\
		set text [string range $text [expr $index+2] end]
	    }
	    - {
		dash
		set text [string range $text [expr $index+2] end]
	    }
	    | {
		set text [string range $text [expr $index+2] end]
	    }
	    default {
		puts stderr "Unknown sequence: \\$c"
		set text [string range $text [expr $index+2] end]
	    }
	}
    }
}



# dash --
#
# This procedure is invoked to handle dash characters ("\-" in
# troff).  It outputs a special dash character.
#
# Arguments:
# None.

proc dash {} {
    global textState charCnt
    if {$textState == "NAME"} {
    	set textState 0
    }
    incr charCnt
    text "-"
}


# tab --
#
# This procedure is invoked to handle tabs in the troff input.
#
# Arguments:
# None.

proc tab {} {
    global inPRE charCnt tabString
#	? charCnt
    if {$inPRE == 1} {
	set pos [expr $charCnt % [string length $tabString] ]
	set spaces [string first "1" [string range $tabString $pos end] ]
	text [format "%*s" [incr spaces] " "]
    } else {
#	puts "tab: found tab outside of <PRE> block"
    }
}


# setTabs --
#
# This procedure handles the ".ta" macro, which sets tab stops.
#
# Arguments:
# tabList -	List of tab stops, each consisting of a number
#			followed by "i" (inch) or "c" (cm).

proc setTabs {tabList} {
    global file breakPending tabString

#	puts "setTabs: --$tabList--"
    set last 0
    set tabString {}
    set charsPerInch 14.
    set numTabs [llength $tabList]
    foreach arg $tabList {
	if {[scan $arg "%f%s" distance units] != 2} {
	    puts stderr "bad distance \"$arg\""
	    return 0
    	}
	switch -- $units {
	    c	{
		set distance [expr $distance * $charsPerInch / 2.54 ]
	    }
	    i	{
		set distance [expr $distance * $charsPerInch]
	    }
	    default {
		puts stderr "bad units in distance \"$arg\""
		continue
	    }
    	}
#		? distance
    	lappend tabString [format "%*s1" [expr round($distance-$last-1)] " "]
    	set last $distance
    }
    set tabString [join $tabString {}]
#	puts "setTabs: --$tabString--"
}



# lineBreak --
#
# Generates a line break in the IPF output.
#
# Arguments:
# None.

proc lineBreak {} {
    global file inPRE

    if {$inPRE} {
        puts $file "\n"
    } else {
        puts $file "\n.br\n"
    }
}



# newline --
#
# This procedure is invoked to handle newlines in the troff input.
# It outputs either a space character or a newline character, depending
# on fill mode.
#
# Arguments:
# None.

proc newline {} {
    global noFillCount file inDT inPRE charCnt

    if {$inDT != {} } {
    	puts $file "\n$inDT"
    	set inDT {}
    } elseif {$noFillCount == 0 || $inPRE == 1} {
	puts $file {}
    } else {
	lineBreak
	incr noFillCount -1
    }
    set charCnt 0
}



# char --
#
# This procedure is called to handle a special character.
#
# Arguments:
# name -		Special character named in troff \x or \(xx construct.

proc char name {
    global file charCnt

    incr charCnt
#	puts "char: $name"
    # In comments explanations from NROFF/TROFF User's Manual (Oct.11, 1976)
    switch -exact $name {
	\\0 { ; # Digit width space
	    puts -nonewline $file " "
	}
	\\\\ { ; # Escaped backslash
	    puts -nonewline $file "\\"
	}
	\\(+- { ; # Character named +- (plusminus)
	    puts -nonewline $file "&plusmin."
	}
	\\% - ; # Default optionale hyphenation character
	\\| { ; # 1/6 em narrow space character (zero width in NROFF)
	}
	default {
	    puts stderr "Unknown character: $name"
	}
    }
}


# macro2 --
#
# This procedure handles macros that are invoked with a leading "'"
# character instead of space.  Right now it just generates an
# error diagnostic.
#
# Arguments:
# name -		The name of the macro (without the ".").
# args -		Any additional arguments to the macro.

proc macro2 {name args} {
    puts stderr "Unknown macro: '$name [join $args " "]"
}



# SHmacro --
#
# Subsection head; handles the .SH macro.
#
# Arguments:
# name -		Section name.

proc SHmacro argList {
    global file noFillCount textState charCnt beforeSynopsis

    set args [join $argList " "]
    if {[llength $argList] < 1} {
	puts stderr "Bad .SH macro: .$name $args"
    }

    set noFillCount 0
    nest reset

    if {$args != {NAME} || !$beforeSynopsis} {
        puts -nonewline $file ":p."
    }
    puts -nonewline $file ":hp4."
    text  $args
    puts $file ":ehp4.\n:p."

#	? args textState

    # control what the text proc does with text
    
    switch $args {
	NAME {set textState NAME}
	DESCRIPTION {set textState INSERT}
	INTRODUCTION {set textState INSERT}
	"WIDGET-SPECIFIC OPTIONS" {set textState INSERT}
	"SEE ALSO" {set textState SEE}
	KEYWORDS {set textState 0}
        SYNOPSIS {set beforeSynopsis 0}
    }
    set charCnt 0
}



# IPmacro --
#
# This procedure is invoked to handle ".IP" macros, which may take any
# of the following forms:
#
# .IP [1]		Translate to a "1Step" paragraph.
# .IP [x] (x > 1)	Translate to a "Step" paragraph.
# .IP			Translate to a "Bullet" paragraph.
# .IP text count	Translate to a FirstBody paragraph with special
#			indent and tab stop based on "count", and tab after
#			"text".
#
# Arguments:
# argList -		List of arguments to the .IP macro.
#
# IPF limitations: 'count' in '.IP text count' is ignored.

proc IPmacro argList {
    global file

    setTabs 0.5i
    set length [llength $argList]
    if {$length == 0} {
    	nest para UL LI
	return
    }
    if {$length == 1} {
    	nest para OL LI
	    return
	}
    if {$length > 1} {
    	nest para dl dt
	    formattedText [lindex $argList 0]
	    puts $file "\n:dd."
	    return
    }
    puts stderr "Bad .IP macro: .IP [join $argList " "]"
}


# TPmacro --
#
# This procedure is invoked to handle ".TP" macros, which may take any
# of the following forms:
#
# .TP x		Translate to an indented paragraph with the
# 			specified indent (in 100 twip units).
# .TP		Translate to an indented paragraph with
# 			default indent.
#
# Arguments:
# argList -		List of arguments to the .IP macro.
#
# IPF limitations: 'x' in '.TP x' is ignored.


proc TPmacro {argList} {
    global inDT file
    nest para dl dt
    set inDT "\n:dd."			;# next newline writes inDT 
    setTabs 0.5i
}



# THmacro --
#
# This procedure handles the .TH macro.  It generates the non-scrolling
# header section for a given man page, and enters information into the
# table of contents.  The .TH macro has the following form:
#
# .TH name section date footer header
#
# Arguments:
# argList -		List of arguments to the .TH macro.

proc THmacro {argList} {
    global file nextres res beforeSynopsis

    if {[llength $argList] != 5} {
	set args [join $argList " "]
	puts stderr "Bad .TH macro: .TH $args"
    }
    set name  [lindex $argList 0]		;# Tcl_UpVar
    set page  [lindex $argList 1]		;# 3
    set vers  [lindex $argList 2]		;# 7.4
    set lib   [lindex $argList 3]		;# Tcl
    set pname [lindex $argList 4]		;# {Tcl Library Procedures}
    set beforeSynopsis 1
#puts "name \[$name\] page \[$page\] vers \[$vers\] lib \[$lib\] pname \[$pname\]"
    
    # This is what gets put in the contents of the INF-file
#    set res($name) $nextres
#    puts -nonewline $file "\n:h1 res=$nextres."
    puts -nonewline $file "\n:h2 name='$name'."
#    set nextres [ expr $nextres + 1 ]
    text $name
    puts $file "\n"

#    if {$vers != {}} {
#        puts $file ":i1.$name ($page) - $lib v$vers"
#    } else {
#        puts $file ":i1.$name ($page) - $lib"
#    }
#    puts $file ":link reftype=hd refid=$res($name).:elink."
#    puts $file ":link reftype=hd refid='$pname'.:elink."
}



# newPara --
#
# This procedure sets the left and hanging indents for a line.
# Indents are specified in units of inches or centimeters, and are
# relative to the current nesting level and left margin.
#
# Arguments:
# None

proc newPara {} {
    global file nestStk
	
    if {[lindex $nestStk end] != "NEW" } {
	nest decr    
    }
    puts -nonewline $file ":p."
}



# nest --
#
# This procedure takes care of inserting the tags associated with the
# IP, TP, RS, RE, LP and PP macros. Only 'nest para' takes arguments.
#
# Arguments:
# op -				operation: para, incr, decr, reset, init
# listStart -		begin list tag: OL, UL, DL.
# listItem -		item tag:       LI, LI, DT.

proc nest {op {listStart "NEW"} {listItem {} } } {
    global file nestStk inDT charCnt
#	puts "nest: $op $listStart $listItem"
    switch $op {
	para {
	    set top [lindex $nestStk end]
	    if {$top == "NEW" } {
		set nestStk [lreplace $nestStk end end $listStart]
                if {$listStart == "dl"} {
		    puts $file ":dl tsize=30."
                } else {
		    puts $file ":$listStart."
                }
	    } elseif {$top != $listStart} {
		puts stderr "nest para: bad stack"
		exit 1
	    }
	    puts $file ":$listItem."
	    set charCnt 0
	}
	incr {
	   lappend nestStk NEW
	}
	decr {
	    if {[llength $nestStk] == 0} {
		puts stderr "nest error: nest length is zero"
		set nestStk NEW
	    }
	    set tag [lindex $nestStk end]
	    if {$tag != "NEW"} {
		puts $file ":e$tag."
	    }
	    set nestStk [lreplace $nestStk end end]
	}
	reset {
	    while {[llength $nestStk] > 0} {
		nest decr
	    }
	    set nestStk NEW
	}
	init {
	    set nestStk NEW
	    set inDT {}
	}
    }
    set charCnt 0
}



# do --
#
# This is the toplevel procedure that translates a man page
# to IPF.  It runs the man2tcl program to turn the man page
# into a script, then it evals that script.
#
# Arguments:
# fileName -		Name of the file to translate.

proc do fileName {
    global file self html_dir package footer
    set self "[file tail $fileName]"
#    set file [open "$html_dir/$package/$self" w]
#    puts "  Pass 2 -- $fileName"
    flush stdout
    initGlobals
    if [catch {eval [exec man2tcl [glob $fileName]]} msg] {
	global errorInfo
	puts stderr $msg
	puts "in"
	puts stderr $errorInfo
	exit 1
    }
    nest reset
#    puts $file $footer
#    puts $file "</BODY></HTML>"
#    close $file
}



