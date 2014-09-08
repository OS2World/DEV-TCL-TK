#!/proj/tcl/install/5.x-sparc/bin/tclsh7.5

if [catch {

# man2ipf.tcl --
#
# This file contains procedures that work in conjunction with the
# man2tcl program to generate an IPF file from Tcl manual entries.
#
# Copyright (c) 1996 by Sun Microsystems, Inc.
#
# SCCS: @(#) man2ipf.tcl 1.5 96/04/11 20:21:43
#

set homeDir [pwd]

# sarray -
#
# Save an array to a file so that it can be sourced.
#
# Arguments:
# file -		Name of the output file
# args -		Name of the arrays to save
#
proc sarray {file args} {
    set file [open $file w]
    foreach a $args {
	upvar $a array
	if ![array exists array] {
	    puts "sarray: \"$a\" isn't an array"
	    break
	}	
    
	foreach name [lsort [array names array]] {
	    regsub -all " " $name "\\ " name1
	    puts $file "set ${a}($name1) \{$array($name)\}"
	}
    }
    close $file
}



# footer --
#
# Builds footer info for HTML pages
#
# Arguments:
# None

proc footer {files} {
    lappend f ":cgraphic.----------------------------------------:ecgraphic."
    set h {[}
    foreach file $files {
	lappend h ":link reftype=hd refid='$file'.$file:elink."
	lappend h "|"
    }
    lappend f [join [lreplace $h end end {]} ] " "]
    lappend f ":cgraphic.----------------------------------------"
    lappend f "Copyright &#169; 1989-1994 The Regents of the University of California."
    lappend f "Copyright &#169; 1994-1996 Sun Microsystems, Inc."
    lappend f "Copyright &#169; 1998-2002 Illya Vaes"
    lappend f ":ecgraphic."
    return [join $f "\n"]
}




# doDir --
#
# Given a directory as argument, translate all the man pages in
# that directory.
#
# Arguments:
# dir -			Name of the directory.

proc doDir dir {
    foreach f [lsort [glob $dir/*.\[13n\]]] {
	do $f	;# defined in man2ipf1.tcl & man2ipf2.tcl
    }
#    Tclsh 8.3.3:
#    foreach f [lsort [glob -directory $dir "*.\[13n\]"]] {
#	do $f	;# defined in man2ipf1.tcl & man2ipf2.tcl
#    }
}

# doSet --
# Given a list of files and a section title, generate a section in
# the IPF file and generate the manual pages in it.

proc doSet {fileList title} {
    global file homeDir

    if {$fileList != [list] } {
        puts $file "\n:h1 name='$title'.$title"
#        puts $file "\n:i1 id='$title'.$title"
        foreach manpg $fileList {
            source $homeDir/man2ipf2.tcl
            puts "Building IPF from man page $manpg..."
            do $manpg
        }
    }
}


if {$argc < 3} {
    puts stderr "usage: $argv0 projectName fullVersion manFiles..."
    puts stderr "example: $argv0 tcl 8.0.5 e:/tcl8.0.5/doc e:/tk8.0.5/doc"
    exit 1
}
	
#set nextres {001}
set baseName [lindex $argv 0]
set fullVersion [lindex $argv 1]
regsub -all {\.} $fullVersion {} shortVersion
regsub -all {0$} $shortVersion {} shortVersion
set tclfiles {}
set tkfiles {}
set addfiles {}
# divide into entries for sections 1, 3 and n
set tclfiles1 {}
set tclfiles3 {}
set tclfilesn {}
set tkfiles1 {}
set tkfiles3 {}
set tkfilesn {}
set addfiles1 {}
set addfiles3 {}
set addfilesn {}
foreach i [lrange $argv 2 end] {
    set i [file join $i]
    if [ regexp tk $i ] {
puts "i \[$i\], regexp tk \$i \[[regexp tk $i]\]"
       set pkg tk
    } elseif [ regexp tcl $i ] {
puts "i \[$i\], regexp tcl \$i \[[regexp tcl $i]\]"
       set pkg tcl
    } else {
puts "i \[$i\], other"
       set pkg add
    }
    if [file isdir $i] {
        foreach f [lsort [glob [file join $i *.1]]] {
#    Tclsh 8.3.3:
#        foreach f [lsort [glob -directory $i "*.1"]] { #}
            lappend ${pkg}files1 $f
        }
        foreach f [lsort [glob [file join $i *.3]]] {
#    Tclsh 8.3.3:
#        foreach f [lsort [glob -directory $i "*.3"]] { #}
            lappend ${pkg}files3 $f
        }
        foreach f [lsort [glob [file join $i *.n]]] {
#    Tclsh 8.3.3:
#        foreach f [lsort [glob -directory $n "*.1"]] { #}
            lappend ${pkg}filesn $f
        }
    } elseif {[file exists $i]} {
        set ext [ file extension $i ]
        switch $ext {
           {.1} {lappend ${pkg}files1 $i}
           {.3} {lappend ${pkg}files3 $i}
           {.n} {lappend ${pkg}filesn $i}
           default {lappend ${pkg}files $i}
        }
    }
}

#set footer [footer $files]

set file [ open "$baseName$shortVersion.ipf" w ]
fconfigure $file -translation crlf
if {$baseName == {tcl}} {
  puts $file ":userdoc.\n:docprof toc=12.\n:title.Tcl/Tk $fullVersion Reference"
} else {
  puts $file ":userdoc.\n:docprof toc=12.\n:title.$baseName $fullVersion Reference"
}

# make the hyperlink arrays and contents for all files
	
doSet $tclfiles1 {Tcl Applications}
doSet $tclfiles3 {Tcl Library Procedures}
doSet $tclfilesn {Tcl Built-In Commands}
doSet $tclfiles {Tcl Other Manual Pages}
doSet $tkfiles1 {Tk Applications}
doSet $tkfiles3 {Tk Library Procedures}
doSet $tkfilesn {Tk Built-In Commands}
doSet $tkfiles {Tk Other Manual Pages}
doSet $addfiles1 {Additional Applications}
doSet $addfiles3 {Additional Library Procedures}
doSet $addfilesn {Additional Built-In Commands}
doSet $addfiles {Additional Other Manual Pages}
doSet $addfiles {Other Manual Pages}

puts $file ":euserdoc."
close $file

} result] {
    global errorInfo
    puts stderr $result
    puts stderr "in"
    puts stderr $errorInfo
}

