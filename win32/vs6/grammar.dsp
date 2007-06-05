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

SOURCE=..\..\gst\parse\grammar.y

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath=..\..\gst\parse\grammar.y

BuildCmds= \
	copy /y ..\..\gst\parse\grammar.tab.pre.c ..\..\gst\parse\grammar.tab.c \
	copy /y ..\..\gst\parse\grammar.tab.pre.h ..\..\gst\parse\grammar.tab.h \
	copy /y ..\..\gst\parse\lex._gst_parse_yy.pre.c ..\..\gst\parse\lex._gst_parse_yy.c \
	

"..\..\gst\parse\lex._gst_parse_yy.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\parse\grammar.tab.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\parse\grammar.tab.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath=..\..\gst\parse\grammar.y

BuildCmds= \
	copy /y ..\..\gst\parse\grammar.tab.pre.c ..\..\gst\parse\grammar.tab.c \
	copy /y ..\..\gst\parse\grammar.tab.pre.h ..\..\gst\parse\grammar.tab.h \
	copy /y ..\..\gst\parse\lex._gst_parse_yy.pre.c ..\..\gst\parse\lex._gst_parse_yy.c \
	

"..\..\gst\parse\lex._gst_parse_yy.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\parse\grammar.tab.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\parse\grammar.tab.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\gst\gst.h
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=..\common\gstconfig.h

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath=..\common\gstconfig.h

"..\..\gstconfig.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy /y ..\common\gstconfig.h ..\..\gst

# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath=..\common\gstconfig.h

"..\..\gstconfig.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy /y ..\common\gstconfig.h ..\..\gst

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\gstenumtypes.c

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath=..\common\gstenumtypes.c

BuildCmds= \
	copy /y ..\common\gstenumtypes.c ..\..\gst\gstenumtypes.c \
	copy /y ..\common\gstenumtypes.h ..\..\gst\gstenumtypes.h \
	

"..\..\gst\gstenumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\gstenumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath=..\common\gstenumtypes.c

BuildCmds= \
	copy /y ..\common\gstenumtypes.c ..\..\gst\gstenumtypes.c \
	copy /y ..\common\gstenumtypes.h ..\..\gst\gstenumtypes.h \
	

"..\..\gst\gstenumtypes.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\gstenumtypes.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmarshal.list

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath=..\..\gst\gstmarshal.list

BuildCmds= \
	echo #include "glib-object.h" > gstmarshal.c.tmp \
	echo #include "gstmarshal.h" >> gstmarshal.c.tmp \
	glib-genmarshal --body --prefix=gst_marshal ..\..\gst\gstmarshal.list >> gstmarshal.c.tmp \
	move gstmarshal.c.tmp ..\..\gst\gstmarshal.c \
	echo #include "gst/gstconfig.h" > gstmarshal.h.tmp \
	glib-genmarshal --header --prefix=gst_marshal ..\..\gst\gstmarshal.list >> gstmarshal.h.tmp \
	move gstmarshal.h.tmp ..\..\gst\gstmarshal.h \
	

"..\..\gst\gstmarshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\gstmarshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath=..\..\gst\gstmarshal.list

BuildCmds= \
	echo #include "glib-object.h" > gstmarshal.c.tmp \
	echo #include "gstmarshal.h" >> gstmarshal.c.tmp \
	glib-genmarshal --body --prefix=gst_marshal ..\..\gst\gstmarshal.list >> gstmarshal.c.tmp \
	move gstmarshal.c.tmp ..\..\gst\gstmarshal.c \
	echo #include "gst/gstconfig.h" > gstmarshal.h.tmp \
	glib-genmarshal --header --prefix=gst_marshal ..\..\gst\gstmarshal.list >> gstmarshal.h.tmp \
	move gstmarshal.h.tmp ..\..\gst\gstmarshal.h \
	

"..\..\gst\gstmarshal.c" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)

"..\..\gst\gstmarshal.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
   $(BuildCmds)
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\gstversion.h

!IF  "$(CFG)" == "grammar - Win32 Release"

# Begin Custom Build
InputPath=..\common\gstversion.h

"..\..\gst\gstversion.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy /y ..\common\gstversion.h ..\..\gst

# End Custom Build

!ELSEIF  "$(CFG)" == "grammar - Win32 Debug"

# Begin Custom Build
InputPath=..\common\gstversion.h

"..\..\gst\gstversion.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	copy /y ..\common\gstversion.h ..\..\gst

# End Custom Build

!ENDIF 

# End Source File
# End Target
# End Project
