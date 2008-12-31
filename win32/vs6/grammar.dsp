# Microsoft Developer Studio Project File - Name="grammar" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Generic Project" 0x010a

CFG=grammar - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "grammar.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "grammar.mak" CFG="grammar - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "grammar - Win32 Release" (based on "Win32 (x86) Generic Project")
!MESSAGE "grammar - Win32 Debug" (based on "Win32 (x86) Generic Project")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
MTL=midl.exe

!IF  "$(CFG)" == "grammar - Win32 Release"

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

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

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

# Name "grammar - Win32 Release"
# Name "grammar - Win32 Debug"
# Begin Source File

SOURCE="..\..\gst\playback\gstplay-marshal.list"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\..\gst\playback\gstplay-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gstplay-marshal.c.tmp \
	echo #include "gstplay-marshal.h" >> gstplay-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_play_marshal ..\..\gst\playback\gstplay-marshal.list >> gstplay-marshal.c.tmp \
	move gstplay-marshal.c.tmp ..\..\gst\playback\gstplay-marshal.c \
	echo #include "gst/gstconfig.h" > gstplay-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_play_marshal ..\..\gst\playback\gstplay-marshal.list >> gstplay-marshal.h.tmp \
	move gstplay-marshal.h.tmp ..\..\gst\playback\gstplay-marshal.h	

"..\..\gst\playback\gstplay-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\playback\gstplay-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\..\gst\playback\gstplay-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gstplay-marshal.c.tmp \
	echo #include "gstplay-marshal.h" >> gstplay-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_play_marshal ..\..\gst\playback\gstplay-marshal.list >> gstplay-marshal.c.tmp \
	move gstplay-marshal.c.tmp ..\..\gst\playback\gstplay-marshal.c \
	echo #include "gst/gstconfig.h" > gstplay-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_play_marshal ..\..\gst\playback\gstplay-marshal.list >> gstplay-marshal.h.tmp \
	move gstplay-marshal.h.tmp ..\..\gst\playback\gstplay-marshal.h 

"..\..\gst\playback\gstplay-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\playback\gstplay-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\gst\tcp\gsttcp-marshal.list"
!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\..\gst\tcp\gsttcp-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gsttcp-marshal.c.tmp \
	echo #include "gsttcp-marshal.h" >> gsttcp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_tcp_marshal ..\..\gst\tcp\gsttcp-marshal.list >> gsttcp-marshal.c.tmp \
	move gsttcp-marshal.c.tmp ..\..\gst\tcp\gsttcp-marshal.c \
	echo #include "gst/gstconfig.h" > gsttcp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_tcp_marshal ..\..\gst\tcp\gsttcp-marshal.list >> gsttcp-marshal.h.tmp \
	move gsttcp-marshal.h.tmp ..\..\gst\tcp\gsttcp-marshal.h
	

"..\..\gst\tcp\gsttcp-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\tcp\gsttcp-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\..\gst\tcp\gsttcp-marshal.list"

BuildCmds= \
	echo #include "glib-object.h" > gsttcp-marshal.c.tmp \
	echo #include "gsttcp-marshal.h" >> gsttcp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_tcp_marshal ..\..\gst\tcp\gsttcp-marshal.list >> gsttcp-marshal.c.tmp \
	move gsttcp-marshal.c.tmp ..\..\gst\tcp\gsttcp-marshal.c \
	echo #include "gst/gstconfig.h" > gsttcp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_tcp_marshal ..\..\gst\tcp\gsttcp-marshal.list >> gsttcp-marshal.h.tmp \
	move gsttcp-marshal.h.tmp ..\..\gst\tcp\gsttcp-marshal.h
	

"..\..\gst\tcp\gsttcp-marshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\tcp\gsttcp-marshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\common\interfaces-enumtypes.c"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\common\interfaces-enumtypes.c"

BuildCmds= \
	copy ..\common\interfaces-enumtypes.h ..\..\gst-libs\gst\interfaces \
	copy ..\common\interfaces-enumtypes.c ..\..\gst-libs\gst\interfaces
	

"..\..\gst-libs\gst\interfaces\interfaces-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\interfaces\interfaces-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\common\interfaces-enumtypes.c"

BuildCmds= \
	copy ..\common\interfaces-enumtypes.h ..\..\gst-libs\gst\interfaces \
	copy ..\common\interfaces-enumtypes.c ..\..\gst-libs\gst\interfaces
	

"..\..\gst-libs\gst\interfaces\interfaces-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\interfaces\interfaces-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\interfaces-marshal.list"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\..\gst-libs\gst\interfaces\interfaces-marshal.list"

BuildCmds= \
	echo #include "interfaces-marshal.h" > interfaces-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_interfaces_marshal ..\..\gst-libs\gst\interfaces\interfaces-marshal.list >> interfaces-marshal.c.tmp \
	move interfaces-marshal.c.tmp ..\..\gst-libs\gst\interfaces\interfaces-marshal.c \
	echo #include "gst/gstconfig.h" > interfaces-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_interfaces_marshal ..\..\gst-libs\gst\interfaces\interfaces-marshal.list >> interfaces-marshal.h.tmp \
	move interfaces-marshal.h.tmp ..\..\gst-libs\gst\interfaces\interfaces-marshal.h
	

"..\..\gst-libs\gst\interfaces\interfaces-marshal.list.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\interfaces\interfaces-marshal.list.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\..\gst-libs\gst\interfaces\interfaces-marshal.list"

BuildCmds= \
	echo #include "interfaces-marshal.h" > interfaces-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_interfaces_marshal ..\..\gst-libs\gst\interfaces\interfaces-marshal.list >> interfaces-marshal.c.tmp \
	move interfaces-marshal.c.tmp ..\..\gst-libs\gst\interfaces\interfaces-marshal.c \
	echo #include "gst/gstconfig.h" > interfaces-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_interfaces_marshal ..\..\gst-libs\gst\interfaces\interfaces-marshal.list >> interfaces-marshal.h.tmp \
	move interfaces-marshal.h.tmp ..\..\gst-libs\gst\interfaces\interfaces-marshal.h
	

"..\..\gst-libs\gst\interfaces\interfaces-marshal.list.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\interfaces\interfaces-marshal.list.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\common\audio-enumtypes.c"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\common\audio-enumtypes.c"

BuildCmds= \
	copy ..\common\audio-enumtypes.h ..\..\gst-libs\gst\audio \
	copy ..\common\audio-enumtypes.c ..\..\gst-libs\gst\audio
	

"..\..\gst-libs\gst\audio\audio-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\audio\audio-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\common\audio-enumtypes.c"

BuildCmds= \
	copy ..\common\audio-enumtypes.h ..\..\gst-libs\gst\audio \
	copy ..\common\audio-enumtypes.c ..\..\gst-libs\gst\audio
	

"..\..\gst-libs\gst\audio\audio-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\audio\audio-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\rtsp\rtsp-marshal.list"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\..\gst-libs\gst\rtsp\rtsp-marshal.list"

BuildCmds= \
	echo #include "rtsp-marshal.h" > rtsp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_rtsp_marshal ..\..\gst-libs\gst\rtsp\rtsp-marshal.list >> rtsp-marshal.c.tmp \
	move rtsp-marshal.c.tmp ..\..\gst-libs\gst\rtsp\rtsp-marshal.c \
	echo #include "gst/gstconfig.h" > rtsp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_rtsp_marshal ..\..\gst-libs\gst\rtsp\rtsp-marshal.list >> rtsp-marshal.h.tmp \
	move rtsp-marshal.h.tmp ..\..\gst-libs\gst\rtsp\rtsp-marshal.h
	

"..\..\gst-libs\gst\rtsp\rtsp-marshal.list.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\rtsp\rtsp-marshal.list.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\..\gst-libs\gst\rtsp\rtsp-marshal.list"

BuildCmds= \
	echo #include "rtsp-marshal.h" > rtsp-marshal.c.tmp \
	glib-genmarshal --body --prefix=gst_rtsp_marshal ..\..\gst-libs\gst\rtsp\rtsp-marshal.list >> rtsp-marshal.c.tmp \
	move rtsp-marshal.c.tmp ..\..\gst-libs\gst\rtsp\rtsp-marshal.c \
	echo #include "gst/gstconfig.h" > rtsp-marshal.h.tmp \
	glib-genmarshal --header --prefix=gst_rtsp_marshal ..\..\gst-libs\gst\rtsp\rtsp-marshal.list >> rtsp-marshal.h.tmp \
	move rtsp-marshal.h.tmp ..\..\gst-libs\gst\rtsp\rtsp-marshal.h
	

"..\..\gst-libs\gst\rtsp\rtsp-marshal.list.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

..\..\gst-libs\gst\rtsp\rtsp-marshal.list.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\common\gstrtsp-enumtypes.c"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\common\gstrtsp-enumtypes.c"

BuildCmds= \
	copy ..\common\gstrtsp-enumtypes.h ..\..\gst-libs\gst\rtsp \
	copy ..\common\gstrtsp-enumtypes.c ..\..\gst-libs\gst\rtsp
	

"..\..\gst-libs\gst\rtsp\gstrtsp-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\rtsp\gstrtsp-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\common\gstrtsp-enumtypes.c"

BuildCmds= \
	copy ..\common\gstrtsp-enumtypes.h ..\..\gst-libs\gst\rtsp \
	copy ..\common\gstrtsp-enumtypes.c ..\..\gst-libs\gst\rtsp
	

"..\..\gst-libs\gst\rtsp\gstrtsp-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\rtsp\gstrtsp-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\common\pbutils-enumtypes.c"

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath="..\common\pbutils-enumtypes.c"

BuildCmds= \
	copy ..\common\pbutils-enumtypes.h ..\..\gst-libs\gst\pbutils \
	copy ..\common\pbutils-enumtypes.c ..\..\gst-libs\gst\pbutils
	

"..\..\gst-libs\gst\pbutils\pbutils-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\pbutils\pbutils-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath="..\common\pbutils-enumtypes.c"

BuildCmds= \
	copy ..\common\pbutils-enumtypes.h ..\..\gst-libs\gst\pbutils \
	copy ..\common\pbutils-enumtypes.c ..\..\gst-libs\gst\pbutils
	

"..\..\gst-libs\gst\pbutils\pbutils-enumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst-libs\gst\pbutils\pbutils-enumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# End Target
# End Project
