==== Windows support ====

== Building GStreamer under Win32 ==

There are different makefiles that can be used to build GStreamer with the usual Microsoft 
compiling tools.

The Makefile is meant to be used with the GNU make program and the free 
version of the Microsoft compiler (http://msdn.microsoft.com/visualc/vctoolkit2003/). You also 
have to modify your system environment variables to use it from the command-line. You will also 
need a working Platform SDK for Windows that is available for free from Microsoft.

The projects/makefiles will generate automatically some source files needed to compile 
GStreamer. That requires that you have installed on your system some GNU tools and that they are 
available in your system PATH.

The GStreamer project depends on other libraries, namely :
- GLib
- popt
- libxml2
- gettext
- libiconv

There is now an existing package that has all these dependencies built with MSVC7.1. It exists either
a precompiled librairies and headers in both Release and Debug mode, or as the source package to build
it yourself. You can find it on http://mukoli.free.fr/gstreamer/deps/.

NOTE : GNU tools needed that you can find on http://gnuwin32.sourceforge.net/
- GNU flex      (tested with 2.5.4)
- GNU bison     (tested with 1.35)
and http://www.mingw.org/
- GNU make      (tested with 3.80)

NOTE : the generated files from the -auto makefiles will be available soon separately on the net 
for convenience (people who don't want to install GNU tools).

== Installation on the system ==

By default, GStreamer needs a registry. You have to generate it using "gst-register.exe". It will create
the file in c:\gstreamer\registry.xml that will hold all the plugins you can use.

You should install the GStreamer core in c:\gstreamer\bin and the plugins in c:\gstreamer\plugins. Both
directories should be added to your system PATH. The library dependencies should be installed in c:\usr.

For example, my current setup is :

C:\gstreamer\registry.xml
C:\gstreamer\bin\gst-inspect.exe
C:\gstreamer\bin\gst-launch.exe
C:\gstreamer\bin\gst-register.exe
C:\gstreamer\bin\gstbytestream.dll
C:\gstreamer\bin\gstelements.dll
C:\gstreamer\bin\gstoptimalscheduler.dll
C:\gstreamer\bin\gstspider.dll
C:\gstreamer\bin\libgtreamer-0.8.dll
C:\gstreamer\plugins\gst-libs.dll
C:\gstreamer\plugins\gstmatroska.dll

C:\usr\bin\iconv.dll
C:\usr\bin\intl.dll
C:\usr\bin\libglib-2.0-0.dll
C:\usr\bin\libgmodule-2.0-0.dll
C:\usr\bin\libgobject-2.0-0.dll
C:\usr\bin\libgthread-2.0-0.dll
C:\usr\bin\libxml2.dll
C:\usr\bin\popt.dll
