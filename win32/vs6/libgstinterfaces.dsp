# Microsoft Developer Studio Project File - Name="libgstinterfaces" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libgstinterfaces - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libgstinterfaces.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libgstinterfaces.mak" CFG="libgstinterfaces - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libgstinterfaces - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgstinterfaces - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libgstinterfaces - Win32 Release"

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
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTINTERFACES_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /O2 /I "../../gst-libs" /I "../../../gstreamer" /I "../common" /I "../../../gstreamer/libs" /I "../../gst-libs/gst/interfaces" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTINTERFACES_EXPORTS" /D "HAVE_CONFIG_H" /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 libgstreamer-0.10.lib libgstbase-0.10.lib glib-2.0.lib gobject-2.0.lib /nologo /dll /machine:I386 /out:"Release/libgstinterfaces-0.10.dll" /libpath:"../../../gstreamer/win32/vs6/release" /libpath:"./release"
# Begin Special Build Tool
TargetPath=.\Release\libgstinterfaces-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\bin
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgstinterfaces - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTINTERFACES_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /ZI /Od /I "../../gst-libs" /I "../../../gstreamer" /I "../common" /I "../../../gstreamer/libs" /I "../../gst-libs/gst/interfaces" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTINTERFACES_EXPORTS" /D "HAVE_CONFIG_H" /FD /GZ /c
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
# ADD LINK32 libgstreamer-0.10.lib libgstbase-0.10.lib glib-2.0D.lib gobject-2.0D.lib /nologo /dll /debug /machine:I386 /out:"Debug/libgstinterfaces-0.10.dll" /pdbtype:sept /libpath:"../../../gstreamer/win32/vs6/debug" /libpath:"./debug"
# Begin Special Build Tool
TargetPath=.\Debug\libgstinterfaces-0.10.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\debug\bin
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "libgstinterfaces - Win32 Release"
# Name "libgstinterfaces - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\colorbalance.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\colorbalancechannel.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\interfaces-enumtypes.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\interfaces-marshal.c"
# End Source File
# Begin Source File

SOURCE=..\common\libgstinterfaces.def
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixer.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixeroptions.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixertrack.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\navigation.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\propertyprobe.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tuner.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tunerchannel.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tunernorm.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\videoorientation.c"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\xoverlay.c"
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\colorbalance.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\colorbalancechannel.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\interfaces.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixer.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixeroptions.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\mixertrack.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\navigation.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\propertyprobe.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tuner.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tunerchannel.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\tunernorm.h"
# End Source File
# Begin Source File

SOURCE="..\..\gst-libs\gst\interfaces\xoverlay.h"
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
