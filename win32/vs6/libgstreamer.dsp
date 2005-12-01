# Microsoft Developer Studio Project File - Name="libgstreamer" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libgstreamer - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libgstreamer.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libgstreamer.mak" CFG="libgstreamer - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libgstreamer - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgstreamer - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libgstreamer - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTREAMER_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /O2 /I "." /I "../.." /I "../common" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTREAMER_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_WIN32" /FD /c
# SUBTRACT CPP /YX /Yc /Yu
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 glib-2.0.lib gobject-2.0.lib gthread-2.0.lib gmodule-2.0.lib libxml2.lib wsock32.lib intl.lib /nologo /dll /machine:I386 /out:"Release/libgstreamer-0.10.dll"
# Begin Special Build Tool
TargetPath=.\Release\libgstreamer-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=mkdir c:\gstreamer	mkdir c:\gstreamer\bin	copy /Y $(TargetPath) c:\gstreamer\bin
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgstreamer - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTREAMER_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /ZI /Od /I "." /I "../.." /I "../common" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTREAMER_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_WIN32" /FR /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "_DEBUG"
# ADD RSC /l 0x40c /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 glib-2.0D.lib gobject-2.0D.lib gthread-2.0D.lib gmodule-2.0D.lib libxml2.lib wsock32.lib intl.lib /nologo /dll /debug /machine:I386 /out:"Debug/libgstreamer-0.10.dll" /pdbtype:sept
# Begin Special Build Tool
TargetPath=.\Debug\libgstreamer-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=mkdir c:\gstreamer\debug	mkdir c:\gstreamer\debug\bin	copy /Y $(TargetPath) c:\gstreamer\debug\bin
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "libgstreamer - Win32 Release"
# Name "libgstreamer - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\common\dirent.c
# End Source File
# Begin Source File

SOURCE="..\..\gst\glib-compat.c"
# End Source File
# Begin Source File

SOURCE=..\..\gst\parse\grammar.tab.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gst.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbin.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbuffer.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbus.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstcaps.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstchildproxy.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstclock.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstelement.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstelementfactory.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstenumtypes.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsterror.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstevent.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstfilter.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstformat.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstghostpad.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstindex.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstindexfactory.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstinfo.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstinterface.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstiterator.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmarshal.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmessage.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstminiobject.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstobject.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpad.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpadtemplate.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstparse.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpipeline.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstplugin.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpluginfeature.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstquery.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstregistry.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstregistryxml.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstsegment.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gststructure.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstsystemclock.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttaglist.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttagsetter.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttask.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttrace.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttypefind.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttypefindfactory.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsturi.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstutils.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstvalue.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstxml.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\parse\lex._gst_parse_yy.c
# End Source File
# Begin Source File

SOURCE=..\common\libgstreamer.def
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\gst\gettext.h
# End Source File
# Begin Source File

SOURCE="..\..\gst\glib-compat.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst\gst-i18n-app.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst\gst-i18n-lib.h"
# End Source File
# Begin Source File

SOURCE=..\..\gst\gst.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gst_private.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbin.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbuffer.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstbus.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstcaps.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstchildproxy.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstclock.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstcompat.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstconfig.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstconfig.h.in
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstelement.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstelementfactory.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstenumtypes.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsterror.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstevent.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstfilter.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstformat.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstghostpad.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstindex.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstindexfactory.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstinfo.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstinterface.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstiterator.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmacros.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmarshal.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmemchunk.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmessage.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstminiobject.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstobject.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpad.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpadtemplate.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstparse.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpipeline.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstplugin.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstpluginfeature.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstquery.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstregistry.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstsegment.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gststructure.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstsystemclock.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttaglist.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttagsetter.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttask.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttrace.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttrashstack.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttypefind.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsttypefindfactory.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gsturi.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstutils.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstvalue.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstversion.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstxml.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
