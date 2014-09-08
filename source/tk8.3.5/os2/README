Last update: 02 October 2003
Illya Vaes
Martin Luther Kingstate 23
6716 NA Ede
The Netherlands
illya@vaeshiep.demon.nl
http://www.vaeshiep.demon.nl/

Welcome to Tcl/Tk 8.3.5 for OS/2 PM.

Binaries Installation:
The installation program installs the packages below a directory of your choice
(default C:\Tcl835), with binaries (EXE/DLL) in subdirectory bin and the Tcl
and Tk library in lib. It offers to make the required settings in the profile
(OS2.INI) and CONFIG.SYS. You can choose to override already present settings
of the environment variables TCL_LIBRARY, TK_LIBRARY and TCLLIBPATH in the
config.sys; these take precedence over the setting of the path stored in the
profile. If you set them yourself, use forward slashes, eg.
SET TCL_LIBRARY=D:/Tcl835/lib/tcl8.3.  TCLLIBPATH is used to get Tcl to load
the correct DLLs for extensions ('package require') like tclreg83.dll.
The 'bin' directory below the chosen destination directory is added to the end
of both the PATH variable and LIBPATH statement; these entries therefore will
have lower precedence than any other Tcl directories that might be in the PATH
and LIBPATH already and contain the correctly named .exe and/or .dll files. A
second install of this package will result in two entries. It will, however,
*replace* the TCL_LIBRARY, TK_LIBRARY and TCLLIBPATH variable settings.
The install also offers to associate the installed binaries with script files
(both based on extension and on real WPS types), where you may choose to treat
Tk scripts differently from Tcl scripts.
The "application name" in the profile uses the full, patchlevel version (ie.
835), so you can install different versions next to eachother. It would seem
that Scriptics has changed its mind about the 8.0.3, 8.0.4, 8.0.5, 8.1.1 stuff
(going from 8.1.1 to 8.2 instead of 8.1.2), so this may not matter much in the
future.

Paths in Tcl/Tk use _forward_ slashes, in this port too! You can change to a
different drive (and, if you like to a different directory there at the same
time) by saying e.g. "cd e:/tk8.3.5/os2".
Tcl uses the environment variable TEMP (native OS/2 format, backslashes) to
determine the directory in which to make temporary files (eg. for use with
pipelines).
This port has been compiled with EMX 0.9d, and requires its EMXLIBCM.DLL and
EMX.DLL (or those of a higher version). Kudos to Eberhard Mattes for EMX.

Compiling:
To compile the source yourself, you need the Tcl and Tk source from Scriptics
(http://www.scriptics.com/).
After extracting it (gzip -c -d tcl8.3.5.tar.gz | tar xvf - OR unzip tcl835.zip;
ditto for tk8.3.5.tar.gz or tk835.zip) on your drive, unzip the source-zip of
Tcl/Tk for OS/2 on the same drive, go to tcl8.3.5\os2, do what's described in
the file INSTALL (copying patched .h files to tcl8.3.5\generic and
tk8.3.5\generic and .tcl-files to tk8.3.5\library) and execute 'gmake'.
Obviously, you need gzip, tar, unzip and gmake for this, as well as EMX.
Also, \OS2\RC.EXE and IBM VisualAge 3.0's IPFC INF-cmpiler were used.
Making the tcl835.inf file is done by "gmake doc" in the tcl directory.

Using the DLLs in other applications:
To make use of tcl83.dll and/or tk83.dll in other applications, you have to
link the executables of those with tcl83.lib and/or tk83.lib (the import
libraries corresponding to the DLLs), and include code in your EXE to
initialize and use Tcl/Tk (or initializing your own package, depending on how
you look at it), as is shown in the files tclOS2AppInit.c (Tcl) and os2Main.c
(Tk). See the Makefile for files to link with your executable, taking note of
the way a command-line Tcl-application is built (eg. with or without defining
CLI_VERSION and using TclOS2SetUsePm(0) when making a CLI application).
NB! When stubs are to be used in/with Tk, OS/2's Presentation Manager must be
initialised from the EXE instead of the DLL. You have to call TkOS2XInit() for
that.
If you want to make and/or debug Tcl/Tk-based applications that use the a.out
style object code (eg. use GDB or PMGDB), you have to build Tcl/Tk in that
format. From the EMX Application Developer's Guide: "Linking with LINK386
(using GCC -Zomf) is recommended for dynamic link libraries. Use ld and emxbind
only if you want to debug the dynamic link library.". You can create and debug
an a.out-based executable that uses the OMF-based Tcl and/or Tk DLL; assuming I
didn't botch up, you don't need to debug the Tcl and Tk DLLs anyway.
If, for some reason, you only have tcl83.dll and/or tk83.dll and not the .lib
files, then you can generate the latter by using emxexp and emximp (2x). See
the Makefile (targets $(TKBASE).def, $(TKBASE).imp and $(TKLIB)) for how to.


NOTES
=====
- Tk obviously looks on a scrollbar as "this many lines of text", where eg.
  "glob *"'s output is counted as 1 line. This makes for some strange behaviour
  with the thumb of the scrollbar changing size dependent upon what you're
  showing in the text widget (seen in the console). The OS/2 native scrollbar
  faithfully represents what Tk is feeding it (the firstFraction and
  lastFraction numbers).
- When you are in TclshPM, you can say "load <full-path-to-tk83.dll>" and you
  have gotten all of Tk at your disposal (without wish's console).
  Eg. "load E:/TclTk/bin/tk83.dll".
- The native File Dialogs don't really support a "default extension" (like in
  Windows), probably because OS/2 has *real* file types not related to the
  filename. The code around the File dialogs will however add any given
  default extension to any given initial file if that has no extension yet.
  If the user then types in a file without an extension or choose one, then
  _no_ extension is added (since the file dialog gives no such control).
  This could probably be added after the dialog (adding the extension then),
  but who am I to second-guess the user... Anyway, the Mac ignores extensions.
- Because of difference in fonts and such, layout of scripts may turn out wrong.
  This includes the test script hscale.tcl for instance, where the arrow that
  follows the scale starts further to the left than the scale. Changing the
  "-wraplength" option to the label $w.msg from 3.5i to 300 makes it (approx.)
  correct.
- Fonts need to be specified in the same case as Presentation Manager knows,
  eg. Helvetica (not helvetica), Times New Roman (not times), ...
- Mouse buttons are those known to OS/2, ie. Button 2 is the *right* button,
  not the middle one as in X. Of course, for consistent lefties (I only write
  left-handed) Button 2 is the left button ;-)

Programs that are known to run (with or without minor modification)
-------------------------------------------------------------------
Some apps seem to get into an infinite loop upon closing the main window with
double clicking the window menu button (the icon topleft). At least Paradigm
Visual Make does this. Visual Tcl and XF correctly intercept the
WM_DELETE_WINDOW and/or other WM_PROTOCOLS.
- Impress 0.4a
- TkCon 1.03
  The switch statement setting the envHome variable ("Determine what directory
  the resource file should be in") must be augmented with an "os2" pattern:
  "windows - os2 - unix    { set envHome HOME }"   instead of
  "windows - unix    { set envHome HOME }"
  And set the HOME environment variable in CONFIG.SYS ofcourse...
- SpecTcl 1.1
  Several tcl_platform checks need to have OS/2 added, which is easiest in
  case statements by adding a line with "os2 -" before the "windows" case,
  thereby making the Windows code pertinent to OS/2 too.
- Visual Tcl 1.08
  A smaller font must be specified to get the texts to show in their entirety
  in the "Geometry Information" dialog, eg {8.Helv}.
- Paradigm Visual Make (pvm_12).
  Needs to have the testing for the platform to be augmented with OS/2, with
  the same actions as for Windows.
- XF 4.0pl1.
  Needs to have "exec command.com..." changed to "exec cmd.exe" in
  src/xfglobals.tcl and src/xfmisc.tcl (failure to do this will crash the
  program), and the scripts xfhardcopy/xfhardcopy.tcl and
  xfappdef/xfappdef.tcl need to have their wish modified to wish83.exe.

TESTS THAT FAIL
===============
Tk:
- Some tests fail because a default font is chosen instead of returning an
  error.
- Some tests fail because the fontname isn't capitalized (eg. helvetica
  instead of Helvetica) and so a default font is chosen.
- Tests in winfo.test with winfo interps fail since X Properties are not
  supported (future implementation via Prf... ?).
- Some tests fail because font sizes / rounding may vary between X and PM (eg.
  entry xview or entry index @nr with a proportional font).
- imgPPM.test fails because the file isn't written in a cross-platform manner.
  Supposedly fixed in 8.0.
- canvPsText.tcl: fails to select the correct fonts because they are specified
  in lower case, while OS/2 font names start with a capital (eg. Helvetica),
  while 'times' should be 'Times New Roman'.
Tcl:
- The Tcl env.test fails (after hacking it to use printenv.exe and a path
  separator of ';' instead of ':') because EMX insert a variable _emx_sig,
  after the contents of the environment that Tcl gets have been determined,
  so all results are "off" by that variable.
- Format: values that would get truncated to 0 seem to be rounded upwards by
  EMX to the nearest non-0 value. This makes two format tests fail (ie. %3.1f
  and 0.01 gives 0.1 instead of 0.0, but %3.1f and 0.001 gives 0.0).
  Two other formats that fail concern "%#.0g" and "%#.4g", where EMX seems to
  take the number of digits after the decimal point more literally than the
  tests expect, rendering 0.0000 and 0. respectively (0.000 and 0.0 expected).
- The "file volumes" commands in cmdAH.test cause OS/2 to generate an "error"
  dialog if there is no disk in a floppy drive (SYS0039) or if there is no
  drive B (if you try to use B:, the OS temporarily assigns that letter to
  drive A, undoing this if you refer to A: again). This dialog should be
  dismissed by choosing "Return error to application" (or something like that).
Other than this, all Tcl tests work correctly.
Tk doesn't crash on the file bugs.tcl.

HISTORICAL CHANGES
==================

8.3 (8.3.5)
===========
Beta 1
- This is a quick re-port from 8.0.5 to 8.3.4. That means it doesn't include
  DDE. It also means that it is possibly not yet thread-safe, especially when
  compiling with VERBOSE on (global variables are shared but not protected).
- The Windows port added support for loading a system cursor from .cur or .ani
  files by filename only. This cannot be supported since we also need to
  specify an ID to load from a DLL, thereby killing script compatiblity anyway.

8.0 (8.0.5)
===========
Release
Beta 4
- When dynamically loading tcl80.dll (DosLoadModule), the "environ" variable
  isn't filled in, causing a crash in tclEnv.c. Fixed by filling it in
  tclOS2Dll.c (like EMX does it somewhere).
Beta 3
- Install program mistakenly overwrote TCL_LIBRARY instead of writing
  TK_LIBRARY.
Beta 2
- Native color dialog (tk_chooseColor), using the description from Rick Papo's
  "Undocumented Features of OS/2". This only really offers the color wheel and
  thumb, so for setting the color exactly via RGB numbers, a dialog button
  offers the possibility of using the Tk dialog (tkColorDialog) after all.
  An error in bringing up the native dialog should automatically give the Tk
  dialog.
- Use of actual system bitmaps for checkboxes and radio boxes.
Beta 1
- There's a loadable module for accessing profiles ("INI-files") now, which
  is implemented as compatible as possible as the Windows version's "registry"
  command. It loads the DLL tclreg80.dll when you use the commands
  "package require registry 1.0" and "registry [...]". The "registry" command
  is also known under the aliases "profile" and "ini".
  Note that OS/2 INI-files have a fixed structure ("applications" with "keys"
  with "values") unlike Windows' registry's near-free form tree-structure,
  though you can cheat by using path separators (like Lotus has in the
  SmartSuite, eg. "applications" LotusSystemServices\3.0 and
  LotusSystemServices\3.1).
- All Tcl/Tk changes to 8.0 are ofcourse there, ie. there's a bytecode compiler
  builtin and buttons, menus and scrollbars are now native.
- Files are now copied with preservation of attributes and time stamps.
- Without measures, the recursion limit tests in interp.test fail with a stack
  error. Lowering the limit from 50 to 25 made it go away, so obviously the
  stack size was too small. The DEF files now use a STACKSIZE of 0x00030000.
- Makefile.aout provided for building Tcl/Tk with a.out style object code,
  which enables the use of stuff that expects that (eg. GDB).
- File handle limit incremented from default 20 to 25.
- All binary distribution filenames FAT-compatible:
  tclshpm76.exe => tclpm80.exe
  tcltest76.exe => tcltst80.exe
Fixes for 7.6/4.2:
- File handle leaking when executing external programs fixed.
- When creating a process in a pipeline with either the file for stdout or
  stderror not readable or NULL, the device "NUL" was opened for *reading*
  instead of writing. Whoops...
- The nasty side effect with the CLI tclsh76.exe was because it did a
  WinSetFocus(HWND_DESKTOP) anyway, despite not being a PM program (and it
  obviously worked too); put a test "if (usePm)" around it.

7.6/4.2 (p2)
============
- Tclsh76.exe is now a command line interface (CLI) version of tclsh.exe.
  In this version you can*NOT* say "load tk42" and have Tk at your disposal,
  since this version has PM usage switched off (mutually exclusive).
  The previous PM implementation is now called tclshpm76.exe.
  Note: the CLI version has a nasty side effect of making the window in which
  it is run lose focus; under FeelX the window even has the active titlebar so
  FeelX must think the focus is there, but typing in the window has no effect.
  You must move your mouse out of the window and back in.
- Native File Open/Save Dialog. If you use object types names as known to the
  WPS, you can select files based purely on their type (not extension).
  Note: for compatibility with existing (non-WPS aware) scripts, the initial
  selection is with "type" <All Files>, so the name is significant.
- Sockets are now handled (in/by code) like in the Unix version, making the
  reliance on EMX more obvious (it was already present) because it makes the
  use of sockets transparent, like on Unix.
- NotifyVisibility events are now only generated for windows that are showing
  according to PM (WinIsWindowShowing) and Tk considers mapped (TK_MAPPED).
  Previously, the Windows port of generating it for every TK_MAPPED child
  (because Windows has no equivalent of WinIsWindowShowing, only of
  WinIsWindowVisible, which returns true for windows that are completely
  obscured too) was used unchanged.
  Any event still has a state of VisibilityUnobscured. More accurate transition
  info could be generated, but in my opinion would be too expensive (both in
  computation and memory) compared to its use.
  Some changes from the 8.0 Windows version have been incorporated to fix the
  double Map/Unmap binding triggering in two tests in textWind.test.
- The names for temporary files are now determined with tempnam(), which made
  eg. "exec cat << {Sample text}" work correctly. This, however, left the
  temporary behind since that is closed (and tried to be deleted) before the
  program that is reading from it (cat) exits, giving a sharing violation.
  Therefore, when a temp file cannot be deleted, it is added to a list of
  temp files as yet undeleted, to be deleted on exit. An exit procedure is
  registered for that, which tries to close and delete every file in the list,
  ignoring any errors. If that doesn't do it.... you're out of luck :-).
  Oh... and EMX 0.9c has TMP_MAX defined as 1000, so after so many temp files
  it starts over, so make sure you don't make that much at the same time... ;-)
  NB: tempname makes temp files in the directory given by the TMP environment
  variable, or C:\ when that doesn't exist.
- Winfo rooty and winfo containing gave incorrect results, fixed.
- Bug in XCopyPlane fixed.
- Moving rectangles left spurious lines to the right (eg. widget demo,
  arrowhead for canvas lines editor); fixed.
- Optional compiling of key-handling fixes suggested by Ilya Zakharevich (break
  e.g. Ctrl-X).
- Fixes suggested by Ilya Zakharevich (fonts):
  + Consider the "10" in e.g. "10.Courier" to be pixels, not points.
  + Use lMaxBaselineExt as height indicator for pixmap fonts instead of
    emHeight.
  + Compile option for "display resolution correction" (ignoring what PM tells
    us and using a given resolution, default 72 dpi.
  + Default text font "10.Courier" when that "correction" is used.
- The format of $tcl_platform(platform) has been changed to "os2" to make it
  more consistent with those of other platforms ("unix", "macintosh",
  "windows"). $tcl_platform(os) still returns "OS/2".
- The Tclsh console now shows output sent to "stdout" or "stderr" when running
  interactively. The tests make use of these explicit channel names.
- Transients now get a dialogborder.

7.5/4.1
=======
Fix 1
-----
- Opening files for appending truncated them. Fixed.
- Bitmaps got reversed (again). XCopyPlane has to do the colours "reversed"
  like the Windows port, contrary to what the PM Guide and Reference states.

Release
-------
- Execs (usually with pretty much output) gave an OS/2 error message that the
  process tried to write to a non-existent pipe. Fixed by not using a named
  pipe but an unnamed pipe.
- The socket code works.
- The Tclsh console tries its best to add all output to the end, no matter
  where the cursor happens to be.
  Insert output from commands with MLM_IMPORT instead of via the clipboard,
  thereby not being limited to 64K.
  Control-C can be used to (try to) break off a script (like is necessary to
  regain control in the test-script remote.tcl).
- Traced a (not-so) subtle bug in TclSetSystemEnv that reared its ugly head
  later, by running Visual Tcl 1.08 (malloc-ing space for VARIABLE=VARIABLE
  instead of VARIABLE=VALUE, for a lengthy value, ergo using memory that wasn't
  malloc-ed). Stoooopid.
- Use of the command processor in environment variable COMSPEC instead of
  always using CMD for things not in the path (ie. likely internal commands
  like DIR). Still uses CMD.EXE when this can't be determined.
- Array subscripts in determining system values were one off, rendering OS/2
  Warp 4 as major version 4, minor version 0, revision <some-number> instead
  of the (documented) required 20, 40 and 0. Fixed, but "hacked" in the same
  way as CMD.EXE, ie. returning version 4.0 for 20.40.0.
- Globbing was broken for non-current directory searches.
  It accepts volume-relative specs like "e:*.tk" and fills in the current
  directory on the drive between the ':' and '*'.
  It now also determines case (in)sensitivity and case (non)preservation from
  the file system that the directory resides on. FAT, VINES and anything it
  doesn't recognize are taken to be insensitive and non-preserving. NFS and
  EXT2FS (Linux) are taken to be sensitive and preserving, HPFS and NTFS
  insensitive and preserving.
- Fixed "put" with a channelId in the Tclsh console.
- Flashing of a new window in the corner (being displayed there and then
  immediately moved) fixed. Was caused by a superfluous WS_VISIBLE in the
  Window Style). By default windows start in upper left corner.
- Overzealous looking at VK_ALTGRAF in tkOS2X.c removed. This caused any key
  pressed with Alt Gr (German, Dutch keyboards) to be ignored.
- First code for a CLI (Command Line Interface) version of tclsh put in, but
  not pursued further. #ifdef-ed with CLI_VERSION.

Beta 5 - 11 sept.1997
------
- Unlike the Windows port ;-), the functionality of stippling for ARCs is
  implemented.
- Fixed inconsistency and typo in tkOS2Pointer.c wrt. Y coordinate lastMousePos.
- The format of $tcl_platform(os) has been modified again to make them more
  logical though now the array elements for platform and OS return the same,
  ie. "OS/2"; Other values: osVersion e.g. "4.0", machine "intel".
  Similarly, "winfo server ." returns e.g. "OS/2 4.0".
  No marketing names (Warp) allowed here...
- Menus appear at the correct height (aligned with bottom of button).
- Binding to ALT works. NB: A binding is case-sensitive: Control-Shift-x will
  not match anything while Control-Shift-X will. The latter is equivalent to
  Control-X, since you will only get X (not x) by pressing Shift.
- Withdrawn windows completely disappear, ie. from the task list.
- Stippling works (apart from stippled ARCs, which don't get stippled in the
  Windows port either).
- Windows port did not look at GC values for CapStyle, JoinStyle, LineStyle.
  This port does.
- Lines have (specified) width. This needs the "geometric line width" in OS/2,
  which is only used by GpiStrokePath (and GpiModifyPath), so use that.
- Ctrl-X and the likes work.
- Focus problem after grab fixed. I was too correct in following the "Results"
  section for XGrabPointer, which says to return AlreadyGrabbed if the grab
  didn't succeed. This should be ignored, like in the Windows version, and
  GrabSuccess should always be returned.
(Font patch by Ilya Zakharevich assimilated)
- Fonts (sigh ;-}): calculation of size was wrong.
  Furthermore, because of unclear / confusing GPI programming info, the check
  of the sizes of the available fonts wasn't done on the right value. It turns
  out you have to *request* the size via the lMaxBaselineExt member, while the
  actually used value is in lEmHeight.
  Am I dense or what?? Actually, the returned nominal pointsize is OK.
  Compile-time option (via #define) provided for ignoring a PM-provided font
  resolution of 120dpi, taking 96dpi instead, which is the resolution in VGA
  and upto ?800x600?; requested by Ilya Zakharevich.
- Color images now work, including those that get split up into several before
  drawing (LCOLF_RGB instead of LCOLF_CONSECRGB in GpiCreateLogColorTable;
  LCOLF_CONSECRGB should (only) be used with Palette Manager).
- Width calculation of fonts improved by using TXTBOX_BOTTOMRIGHT instead of
  TXTBOX_CONCAT.
- Tcl: File globbing was broken because DosQueryPathInfo doesn't like ".\" for
  the file/directory argument. Similarly, directories weren't seen.
- "option readfile" wasn't succesful with OS/2 files, but was with an .Xdefaults
  copied from Linux. Choked on the Return&Newline being replaced by just the
  Newline, yielding a "nr.of chars read" smaller than the file size, aborting
  the entire process. Fix necessitated changing the generic file tkOption.c,
  function ReadOptionFile with the corresponding (fixed) code of Tk 4.2.
- Compile time option (set in makefile.emx) to use "9.WarpSans" as default
  font.

Beta 4
------
- A "wm withdraw" now also works when running or sourcing a script (i.e.
  mapping/creating in withdrawn state).
  Switching to a withdrawn window (eg. by using the task list) works okay.
  Previously only a "wm deiconify" would do the trick of exposing the client
  area (restoring contents).
- Resizing in vertical direction fixed.
  When resizing to a smaller height with a menu bar involved, the menu bar
  disappeared (partly or entirely, depending on the size).
  Cause was the use of CS_SIZEREDRAW for child windows.
- The default screen name is determined from the DISPLAY environment variable,
  if available, so scripts asking for toplevels with -screen <name> don't
  abort (and any toplevels without specifying a screen in an invocation of
  wish with the -screen <name> argument).
- Double-clicking now works correctly, OS/2 PM's WM_BUTTONxDBLCLK messages get
  changed to both a ButtonDown and a ButtonUp event.
- Fonts changed again. Presentation Parameter-style specification now fully
  supported:  PointSize.FontName[.Attribute][.Attribute]... where Attribute is
  one of bold, italic, outline, strikeout, underline.
  Some fonts (eg. -*-Helvetica-Bold-R-Normal-*-240-*-*-*-*-*-*) didn't show up.
  This was because they were scaled to pointsize 0 because the 240 is in the
  "pixels" spot, which should be a substitute for the point size (or at least
  was in the Windows version), but wasn't implemented identically. An explicit
  point size will override it.
  The ascenders and descenders of scalable fonts are fixed, so they won't get
  progressively larger when enlarging the font.
- Double ckfree because of WM_DESTROY message (caused by WinDestroyWindow in
  XDestroyWindow, tkOS2Window.c) fixed, care of sleuthing by Ilya Zakharevich.
- Resizing works, wm minsize and wm maxsize likewise.

Beta 3
------
- Merged resources for additional pointers into the DLL.
- Clicking in the console gets the focus now too (previously, only clicking the
  titlebar worked).
- Fonts work a little bit more correct. ;-)  Height, width and actually used
  pointsize are correct now. Without special handling, every scalable font is
  of point size 12!
  Code by Ilya Zakharevich (ilya@math.ohio-state.edu) incorporated into
  tkOS2Font.c, enabling the use of e.g. "10.Courier".
- Images work. Funnily, they are (exquisite) greyscale on a 64K display but
  color on a 256 palette management. The OS/2 part is doing what Tk tells it
  to with regard to color though, it just gets handed grey values, so I have to
  assume it is working as advertised.
- 'exec' was broken in Beta 2. Fixed. PM apps should be exec-ed with '&' unless
  they are very short-lived. Tclsh will be "listening" for output from the app
  and this will "lock up the queue" because of the single-threadedness.
- The format of $tcl_platform(os) has been modified slightly; return values are
  now "Warp 3" and "Warp 4" instead of "Warp v3" and "Warp 4 (Merlin)".
  Other values: platform "OS/2", osVersion e.g. "4.0", machine "intel".
- Temporary files are cleaned up when closed by specifying a DosClose wrapper
  to be called on file closure.
- Humongous memory leak in TkOS2SelectPalette fixed.

Beta 2
------
- Font selection works correctly now, so that any font available through the
  font dialog is available. It first determines all fonts in the system (#ifdef
  DEBUG) / all fonts with the specified facename (#ifndef DEBUG) via
  GpiQueryFonts and then determines the most suitable font based on the
  size. Preference is given to bitmap fonts over outline (scalable) fonts for
  purposes of performance in using the font. When many fonts of the same
  facename exist, the scalable one is at the start of the "list" and the
  correctly sized bitmap font is at the end of the "list", this may exact a
  performance penalty at the time of determining the font. This is a small
  one-time penalty though.
  Fonts with a '-' in their name have to be specified by length-3-array (eg.
  "Conga-Normal 180 normal").
  To not get stuck with a default font (ie Courier), you will need to use font
  names that OS/2 knows *and* use the right case:
  helvetica      ->   Helvetica   or   Helv
  Times          ->   Times New Roman   or   Tms Rmn
  (eg: ... -font "-Adobe-Times New Roman-medium-r-normal-*-180-*-*-*-*-*-*" )
- Colors on non-Palette management system work. Not tested on VGA but you will
  not get any better there...
- Choosing "Close" (Alt-F4) on a torn-off menu or otherwise non-"main" window
  will only close that particular window, not the whole application.

Beta 1
------
No record has been maintained for bugfixes previous to this.


THANKS TO
=========
- Ilya Zakharevich for his critical testing.
- Anna Chiara Bellini for putting an FTP server at my disposal for 7.5/4.1b.
- Anyone not named that has assisted by beta testing and/or doing suggestions.


BUGS
====

8.0.5
-----
Embedded windows.

TO DO (?) (Extending port)
==========================
- Have a native color dialog. This *is* available (on what versions? Warp 3
  and higher?) but not in a documented class etc.
- Handle WM_CHORD by translating to WM_BUTTON3.
- Accept Drag-and-Drop of files to be sourced into the console.
- Have TraversalCopy and TclpCopyFile also copy Extended Attributes (EAs).
- Have Shift-Insert, Shift-Delete and Ctrl-Insert act on clipboard.
  This means extending the "Tk Clipboard" only approach to use PRIMARY and/or
  SECONDARY too. Just replacing CLIPBOARD with PRIMARY has Shift-Insert working.
- Global Grab doesn't turn pointer into clock outside of dialog. Possible??
- Make button 2 and 3 of the mouse switchable. To OS/2, button 2 is the right
  mouse button and generates a WM_BUTTON2* message, 3 is an optional middle
  mouse button (WM_BUTTON3*). For X, and thus XFree86 in an OS/2 Full Screen
  session, button 2 is the ("always available") middle button and 3 the right.
- Implement a "send", either via DDE or via Pipes/Queues?
- Multithreading (dual-threading: one extra thread for the message queue to
  stay responding to the system, coomunicating with the one that now exists
  which might be computing for a longer time).
  When introducing multi-threading (8.1 does this already), the access to the
  anchor block handle (HAB) must be differentiated by thread instead of just
  returning the value of a global variable. Where possible, this can easily be
  achieved by replacing the call to TclOS2GetHAB() by WinQueryAnchorBlock(hwnd).
  Where a HWND isn't available (eg. GpiCreatePalette() in InitBoxes()), the
  function TclOS2GetHAB should be called in a modified form with thread-specific
  information (ie. thread-id) and there has to be a mechanism for maintaining
  the thread-id to HAB relationship (simple array might be possible if threads
  are guaranteed to start at 0 or 1 and the number of threads possible can be
  queried, but the Tcl Hash mechanism might be more applicable).
- The list of occupied logical font IDs is a straight array. This method will
  leave "holes" when freeing another font than the last one.
  Solution: convert to a linked list or use Tcl's hash table mechanism.
