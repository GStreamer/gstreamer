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
- libpopt
- libxml
- gettext
- libiconv

The sources should be organised in folders as follow :
$(PROJECT_DIR)\glib
$(PROJECT_DIR)\gstreamer (this package)
$(PROJECT_DIR)\libiconv
$(PROJECT_DIR)\gettext
$(PROJECT_DIR)\libxml2
$(PROJECT_DIR)\popt

NOTE : you can find Win32 versions of these libraries on http://gettext.sourceforge.net/ and
http://gnuwin32.sourceforge.net/ (you will need the Binaries and Developer files for each package.)

NOTE : GLib can be found on ftp://ftp.gtk.org/pub/gtk/v2.4/ and should be compiled from the 
sources

NOTE : GNU tools needed that you can find on http://gnuwin32.sourceforge.net/
- GNU flex      (tested with 2.5.4)
- GNU bison     (tested with 1.35)
and http://www.mingw.org/
- GNU make      (tested with 3.80)

NOTE : the generated files from the -auto makefiles will be available soon separately on the net 
for convenience (people who don't want to install GNU tools).