# Project: libgstreamer
# Makefile to use with GNU Make
#
# Uses the Free Visual C++ 2003 compiler from Microsoft
# http://msdn.microsoft.com/visualc/vctoolkit2003/

# Compile with debug information? IMPOSSIBLE with the free version of the compiler
#DEBUG = yes


#
# Don't change anything below this line.
#
CXX  = cl /Tp
CC   = cl /Tc
SRC  = dirent.c ../gst/parse/grammar.tab.c ../gst/gst.c ../gst/gstatomic.c ../gst/gstbin.c ../gst/gstbuffer.c ..\gst\gstcaps.c ..\gst\gstclock.c ..\gst\gstcpu.c ..\gst\gstdata.c ..\gst\gstelement.c ..\gst\gstelementfactory.c ..\gst\gstenumtypes.c ..\gst\gsterror.c ..\gst\gstevent.c ..\gst\gstfilter.c ..\gst\gstformat.c ..\gst\gstindex.c ..\gst\gstinfo.c ..\gst\gstmarshal.c ..\gst\gstmemchunk.c ..\gst\gstobject.c ..\gst\gstpad.c ..\gst\gstparse.c ..\gst\gstpipeline.c ..\gst\gstplugin.c ..\gst\gstpluginfeature.c ..\gst\gstprobe.c ..\gst\gstquery.c ..\gst\gstqueue.c ..\gst\gstregistry.c ..\gst\gstregistrypool.c ..\gst\gstscheduler.c ..\gst\gststructure.c ..\gst\gstsystemclock.c ..\gst\gsttag.c ..\gst\gstthread.c ..\gst\gsttrace.c ..\gst\gsttypefind.c ..\gst\gsturi.c ..\gst\gsturitype.c ..\gst\gstutils.c ..\gst\gstvalue.c ..\gst\gstxml.c ..\gst\registries\gstxmlregistry.c ..\gst\parse\lex._gst_parse_yy.c
OBJ  = $(patsubst %.c,%.obj,$(SRC))
INCS = /I. /I.. /I../libs /I../../popt/include /I../../glib/gmodule /I../../libiconv/include /I../../libxml2/include/libxml2 /I../../glib /I../../glib/glib /I../../glib/build/win32
LDFLAGS = /NOLOGO /DLL /MAP:gstreamer.map /LIBPATH:../../glib/win32 /LIBPATH:../../popt/lib /LIBPATH:../../libxml2/lib
OPTMIZ = /G6 /O2 /Oi /Wp64 /GL
CXXFLAGS = $(INCS) /DWIN32 /D_WINDOWS /D_USRDLL /DHAVE_CONFIG_H /DGSTREAMER_EXPORTS /nologo


ifeq (yes,$(DEBUG))
CXXFLAGS += /Zi /DDEBUG /D_DEBUG /MTd /RTC1
LDFLAGS += /DEBUG
else
CXXFLAGS += /MT /DGST_DISABLE_GST_DEBUG
LDFLAGS += /OPT:REF
endif

LIBS = libgstreamer.dll

.PHONY: all all-before all-after clean clean-custom

%.obj : %.cpp
	$(CXX) $< /c $(CXXFLAGS) /Fo$@

%.obj : %.c
	$(CC) $< /c $(CXXFLAGS) /Fo$@

all: lib

lib: $(LIBS)

clean:
	$(RM) $(OBJ) libgstreamer.lib libgstreamer.dll

libgstreamer.dll: $(OBJ) $(SRC)
	link $(LDFLAGS) /OUT:$@ $(OBJ) libglib-2.0.lib libgobject-2.0.lib libgthread-2.0.lib libgmodule-2.0.lib libxml2.lib libpopt.lib wsock32.lib

depend:
	$(CXX) $(CXXFLAGS) -MM $(SRC) > .depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
