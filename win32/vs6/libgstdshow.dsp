# Microsoft Developer Studio Project File - Name="libgstdshow" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libgstdshow - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libgstdshow.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libgstdshow.mak" CFG="libgstdshow - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libgstdshow - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgstdshow - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libgstdshow - Win32 Release"

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
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTDSHOW_EXPORTS" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBDSHOW_EXPORTS" /Yu"stdafx.h" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 libgstreamer-0.10.lib glib-2.0.lib C:\DX90SDK\Samples\C++\DirectShow\BaseClasses\Release\strmbase.lib quartz.lib kernel32.lib user32.lib gdi32.lib comdlg32.lib ole32.lib winmm.lib msacm32.lib olepro32.lib uuid.lib strmiids.lib advapi32.lib olepro32.lib /nologo /dll /machine:I386 /out:"Release/libgstdshow-0.10.dll"
# SUBTRACT LINK32 /nodefaultlib
# Begin Special Build Tool
TargetPath=.\Release\libgstdshow-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\bin
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgstdshow - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTDSHOW_EXPORTS" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBDSHOW_EXPORTS" /Yu"stdafx.h" /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "_DEBUG"
# ADD RSC /l 0x40c /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 libgstreamer-0.10.lib glib-2.0D.lib strmbasd.lib quartz.lib kernel32.lib user32.lib gdi32.lib comdlg32.lib ole32.lib winmm.lib msacm32.lib olepro32.lib oleaut32.lib advapi32.lib uuid.lib strmiids.lib /nologo /dll /debug /machine:I386 /out:"Debug/libgstdshow-0.10.dll" /pdbtype:sept
# SUBTRACT LINK32 /nodefaultlib
# Begin Special Build Tool
TargetPath=.\Debug\libgstdshow-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\debug\bin
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "libgstdshow - Win32 Release"
# Name "libgstdshow - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshow.cpp"
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowfakesink.cpp"
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowfakesrc.cpp"
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowinterface.cpp"
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=..\common\libgstdshow.def
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshow.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowfakesink.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowfakesrc.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\dshow\gstdshowinterface.h"
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
