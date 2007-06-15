# Microsoft Developer Studio Project File - Name="autogen" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Generic Project" 0x010a

CFG=autogen - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "autogen.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "autogen.mak" CFG="autogen - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "autogen - Win32 Release" (based on "Win32 (x86) Generic Project")
!MESSAGE "autogen - Win32 Debug" (based on "Win32 (x86) Generic Project")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
MTL=midl.exe

!IF  "$(CFG)" == "autogen - Win32 Release"

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

!ELSEIF  "$(CFG)" == "autogen - Win32 Debug"

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

# Name "autogen - Win32 Release"
# Name "autogen - Win32 Debug"
# Begin Source File

SOURCE="..\..\gst\playback\gstplay-marshal.list"

!IF  "$(CFG)" == "autogen - Win32 Release"

# Begin Custom Build
InputPath="..\..\gst\playback\gstplay-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gstudp-marshal.c.tmp \
	echo #include "gstudp-marshal.h" >> gstudp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_udp_marshal ..\..\gst\udp\gstudp-marshal.list >> gstudp-marshal.c.tmp \
	move gstudp-marshal.c.tmp ..\..\gst\udp\gstudp-marshal.c \
	echo #include "gst/gstconfig.h" > gstudp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_udp_marshal ..\..\gst\udp\gstudp-marshal.list >> gstudp-marshal.h.tmp \
	move gstudp-marshal.h.tmp ..\..\gst\udp\gstudp-marshal.h \
	

"..\..\gst\udp\gstudp-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\udp\gstudp-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "autogen - Win32 Debug"

# Begin Custom Build
InputPath="..\..\gst\playback\gstplay-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gstudp-marshal.c.tmp \
	echo #include "gstudp-marshal.h" >> gstudp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_udp_marshal ..\..\gst\udp\gstudp-marshal.list >> gstudp-marshal.c.tmp \
	move gstudp-marshal.c.tmp ..\..\gst\udp\gstudp-marshal.c \
	echo #include "gst/gstconfig.h" > gstudp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_udp_marshal ..\..\gst\udp\gstudp-marshal.list >> gstudp-marshal.h.tmp \
	move gstudp-marshal.h.tmp ..\..\gst\udp\gstudp-marshal.h \
	

"..\..\gst\udp\gstudp-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\udp\gstudp-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\common\gstudp-enumtypes.c"

!IF  "$(CFG)" == "autogen - Win32 Release"

# Begin Custom Build
InputPath="..\common\gstudp-enumtypes.c"

BuildCmds= \
	copy /y ..\common\gstudp-enumtypes.c ..\..\gst\udp\gstudp-enumtypes.c \
	copy /y ..\common\gstudp-enumtypes.h ..\..\gst\udp\gstudp-enumtypes.h \
	

"..\..\gst\udp\gstudp-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\udp\gstudp-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "autogen - Win32 Debug"

# Begin Custom Build
InputPath="..\common\gstudp-enumtypes.c"

"..\..\gst\udp\gstudp-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy /y ..\common\gstudp-enumtypes.c ..\..\gst\udp\gstudp-enumtypes.c 
	copy /y ..\common\gstudp-enumtypes.h ..\..\gst\udp\gstudp-enumtypes.h 
	
# End Custom Build

!ENDIF 

# End Source File
# End Target
# End Project
