# Microsoft Developer Studio Project File - Name="pygenfiles" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Generic Project" 0x010a

CFG=pygenfiles - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "pygenfiles.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "pygenfiles.mak" CFG="pygenfiles - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "pygenfiles - Win32 Release" (based on "Win32 (x86) Generic Project")
!MESSAGE "pygenfiles - Win32 Debug" (based on "Win32 (x86) Generic Project")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
MTL=midl.exe

!IF  "$(CFG)" == "pygenfiles - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""

!ELSEIF  "$(CFG)" == "pygenfiles - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""

!ENDIF 

# Begin Target

# Name "pygenfiles - Win32 Release"
# Name "pygenfiles - Win32 Debug"
# Begin Source File

SOURCE=..\..\gst\gst.override

!IF  "$(CFG)" == "pygenfiles - Win32 Release"

# Begin Custom Build
InputPath=..\..\gst\gst.override

"gst_output" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	C:\Python24\python.exe ..\..\codegen/codegen.py --load-types ..\..\gst\arg-types.py --register ..\..\gst\gst-types.defs --override ..\..\gst\gst.override --extendpath ..\..\gst\  --extendpath ..\..\gst\ --prefix pygst ..\..\gst\gst.defs > ..\..\gst\gen-gst.c 
	move ..\..\gst\gen-gst.c ..\..\gst\gst.c 
	
# End Custom Build

!ELSEIF  "$(CFG)" == "pygenfiles - Win32 Debug"

# Begin Custom Build
InputPath=..\..\gst\gst.override

"gst_output" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	C:\Python24\python.exe ..\..\codegen/codegen.py --load-types ..\..\gst\arg-types.py --register ..\..\gst\gst-types.defs --override ..\..\gst\gst.override --extendpath ..\..\gst\  --extendpath ..\..\gst\ --prefix pygst ..\..\gst\gst.defs > ..\..\gst\gen-gst.c 
	move ..\..\gst\gen-gst.c ..\..\gst\gst.c 
	
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\gst\interfaces.override

!IF  "$(CFG)" == "pygenfiles - Win32 Release"

# Begin Custom Build
InputPath=..\..\gst\interfaces.override

"interfaces_output" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	C:\Python24\python.exe ..\..\codegen/codegen.py --load-types ..\..\gst\arg-types.py --register ..\..\gst\gst-types.defs --override ..\..\gst\interfaces.override --extendpath ..\..\gst\  --extendpath ..\..\gst\ --prefix pyinterfaces ..\..\gst\interfaces.defs > ..\..\gst\gen-interfaces.c 
	move ..\..\gst\gen-interfaces.c ..\..\gst\interfaces.c
 
	
# End Custom Build

!ELSEIF  "$(CFG)" == "pygenfiles - Win32 Debug"

# Begin Custom Build
InputPath=..\..\gst\interfaces.override

"interfaces_output" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	C:\Python24\python.exe ..\..\codegen/codegen.py --load-types ..\..\gst\arg-types.py --register ..\..\gst\gst-types.defs --override ..\..\gst\interfaces.override --extendpath ..\..\gst\  --extendpath ..\..\gst\ --prefix pyinterfaces ..\..\gst\interfaces.defs > ..\..\gst\gen-interfaces.c 
	move ..\..\gst\gen-interfaces.c ..\..\gst\interfaces.c
 
	
# End Custom Build

!ENDIF 

# End Source File
# End Target
# End Project
