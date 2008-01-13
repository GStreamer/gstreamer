# Microsoft Developer Studio Project File - Name="libgstpython" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libgstpython - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libgstpython.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libgstpython.mak" CFG="libgstpython - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libgstpython - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgstpython - Win32 Release python24" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libgstpython - Win32 Release python25" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libgstpython - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /ZI /Od /I "../../../gst-plugins-base/" /I "../../../gstreamer" /I "../../../gstreamer/libs" /I "../common" /I "../../../gst-plugins-base/gst-libs" /I "C:\Python24\include" /I "C:\Python24\include\pygtk-2.0" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_VIDEO_ORIENTATION_INTERFACE" /FD /GZ /c
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
# ADD LINK32 glib-2.0D.lib gobject-2.0D.lib libgstreamer-0.10.lib libgstbase-0.10.lib libgstinterfaces-0.10.lib libgstdataprotocol-0.10.lib libgstcontroller-0.10.lib libxml2.lib libgstnet-0.10.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /libpath:"../../../gstreamer/win32/vs6/debug" /libpath:"../../../gst-plugins-base/win32/vs6/debug" /libpath:"C:\Python24\libs\\"
# Begin Special Build Tool
TargetPath=.\Debug\libgstpython.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\debug\lib\gstreamer-0.10
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgstpython - Win32 Release python24"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "libgstpython___Win32_Release_python24"
# PROP BASE Intermediate_Dir "libgstpython___Win32_Release_python24"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release_python24"
# PROP Intermediate_Dir "Release_python24"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /O1 /I "../../../gstreamer" /I "../../../gstreamer/libs" /I "../common" /I "../../../gst-plugins-base/gst-libs" /I "C:\Python24\include\pygtk-2.0" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_VIDEO_ORIENTATION_INTERFACE" /FR /FD /c
# SUBTRACT BASE CPP /YX
# ADD CPP /nologo /MD /W3 /O1 /I "../../../gstreamer" /I "../../../gstreamer/libs" /I "../common" /I "../../../gst-plugins-base/gst-libs" /I "C:\Python24\include\pygtk-2.0" /I "C:\Python24\include\\" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_VIDEO_ORIENTATION_INTERFACE" /FR /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 libgstreamer-0.10.lib libgstbase-0.10.lib libgstinterfaces-0.10.lib libgstdataprotocol-0.10.lib libgstcontroller-0.10.lib glib-2.0.lib gobject-2.0.lib libxml2.lib /nologo /dll /machine:I386 /libpath:"../../../gstreamer/win32/vs6/release" /libpath:"../../../gst-plugins-base/win32/vs6/release"
# ADD LINK32 glib-2.0.lib gobject-2.0.lib libgstreamer-0.10.lib libgstbase-0.10.lib libgstinterfaces-0.10.lib libgstdataprotocol-0.10.lib libgstcontroller-0.10.lib libxml2.lib libgstnet-0.10.lib /nologo /dll /machine:I386 /libpath:"../../../gstreamer/win32/vs6/release" /libpath:"../../../gst-plugins-base/win32/vs6/release" /libpath:"C:\Python24\libs\\"
# Begin Special Build Tool
TargetPath=.\Release_python24\libgstpython.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\lib\gstreamer-0.10
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libgstpython - Win32 Release python25"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "libgstpython___Win32_Release_python25"
# PROP BASE Intermediate_Dir "libgstpython___Win32_Release_python25"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release_python25"
# PROP Intermediate_Dir "Release_python25"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /O1 /I "../../../gstreamer" /I "../../../gstreamer/libs" /I "../common" /I "../../../gst-plugins-base/gst-libs" /I "C:\Python24\include\pygtk-2.0" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_VIDEO_ORIENTATION_INTERFACE" /FR /FD /c
# SUBTRACT BASE CPP /YX
# ADD CPP /nologo /MD /W3 /O1 /I "../../../gstreamer" /I "../../../gstreamer/libs" /I "../common" /I "../../../gst-plugins-base/gst-libs" /I "C:\Python25\include\pygtk-2.0" /I "C:\Python25\include" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBGSTPYTHON_EXPORTS" /D "HAVE_CONFIG_H" /D "HAVE_VIDEO_ORIENTATION_INTERFACE" /FR /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x40c /d "NDEBUG"
# ADD RSC /l 0x40c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 libgstreamer-0.10.lib libgstbase-0.10.lib libgstinterfaces-0.10.lib libgstdataprotocol-0.10.lib libgstcontroller-0.10.lib glib-2.0.lib gobject-2.0.lib libxml2.lib /nologo /dll /machine:I386 /libpath:"../../../gstreamer/win32/vs6/release" /libpath:"../../../gst-plugins-base/win32/vs6/release"
# ADD LINK32 glib-2.0.lib gobject-2.0.lib libgstreamer-0.10.lib libgstbase-0.10.lib libgstinterfaces-0.10.lib libgstdataprotocol-0.10.lib libgstcontroller-0.10.lib libxml2.lib libgstnet-0.10.lib /nologo /dll /machine:I386 /libpath:"../../../gstreamer/win32/vs6/release" /libpath:"../../../gst-plugins-base/win32/vs6/release" /libpath:"C:\Python25\libs"
# Begin Special Build Tool
TargetPath=.\Release_python25\libgstpython.dll
SOURCE="$(InputPath)"
PostBuild_Cmds=copy /Y $(TargetPath) c:\gstreamer\lib\gstreamer-0.10
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "libgstpython - Win32 Debug"
# Name "libgstpython - Win32 Release python24"
# Name "libgstpython - Win32 Release python25"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE="..\..\gst\gst-argtypes.c"
# End Source File
# Begin Source File

SOURCE=..\..\gst\gst.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\gstmodule.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\interfaces.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\interfacesmodule.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstexception.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstiterator.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstminiobject.c
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstvalue.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\gst\common.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstexception.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstminiobject.h
# End Source File
# Begin Source File

SOURCE=..\..\gst\pygstvalue.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
