set TCL_LIBRARY=e:/tcl8.3.5/library
set TK_LIBRARY=e:/tk8.3.5/library
set TCLLIBPATH=e:/tcl8.3.5/os2
set DISPLAY=naam
start "Tcl1" /n /win 4os2.exe
start "Tcl2" /n /win 4os2.exe
start "Tcl3" /n /win 4os2.exe
start "Debug" /n /win cmd.exe
start "log" /n /win vim.exe
start "Release Notes" /n /win vim.exe \tk8.3.5\os2\README
start "Problem Reports" /n /win vim.exe \tk8.3.5\os2\pr.txt
cd \tcl8.0.5\os2
start "Tcl 8.0.5" /n /win vim.exe
cd \tcl8.3.5\win
start "Windows" /n /win vim.exe
cd ..\generic
start "Generic" /n /win vim.exe
cd ..\os2
start /b /n /pm /min view GPIREF
start /b /n /pm /min view PMREF
start /b /n /pm /min view CPREF
rem start /b /n /pm /min view EMXDOC
start /b /n /pm /min view EMXBOOK
REM start /b /n /pm pmtree
start /b /n /pm pspm2
REM start /n /pm pmprintf
emxload -gcc -omf
rem start /b /n /pm /min explore file:///D:\os2utils\html\tcltk.html
start /b /n /pm /min view tcl835.inf
rem start /b /n /pm /min d:\acrobat3\reados2\acroread.exe D:\os2utils\pdf\TclTkElRef803.pdf
