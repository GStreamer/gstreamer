# Project: libgstreamer
# Makefile to use with GNU Make
#
# Uses the Free Visual C++ 2003 compiler from Microsoft
# http://msdn.microsoft.com/visualc/vctoolkit2003/

# Compile with debug information ? IMPOSSIBLE with the free version of the compiler
DEBUG = no

# Link with MSVCRT as a DLL ? IMPOSSIBLE with the free version of the compiler
MSVCRT = yes

SRC_DIR=..\gst
GLIB_DIR=..\..\glib

#
# Don't change anything below this line.
#
CXX  = cl /Tp
CC   = cl /Tc
SRC  = dirent.c $(SRC_DIR)\parse\grammar.tab.c $(SRC_DIR)\gst.c $(SRC_DIR)\gstatomic.c $(SRC_DIR)\gstbin.c $(SRC_DIR)\gstbuffer.c \
$(SRC_DIR)\gstcaps.c $(SRC_DIR)\gstclock.c $(SRC_DIR)\gstcpu.c $(SRC_DIR)\gstdata.c $(SRC_DIR)\gstelement.c $(SRC_DIR)\gstelementfactory.c \
$(SRC_DIR)\gstenumtypes.c $(SRC_DIR)\gsterror.c $(SRC_DIR)\gstevent.c $(SRC_DIR)\gstfilter.c $(SRC_DIR)\gstformat.c $(SRC_DIR)\gstindex.c \
$(SRC_DIR)\gstinfo.c $(SRC_DIR)\gstmarshal.c $(SRC_DIR)\gstmemchunk.c $(SRC_DIR)\gstobject.c $(SRC_DIR)\gstpad.c $(SRC_DIR)\gstparse.c \
$(SRC_DIR)\gstpipeline.c $(SRC_DIR)\gstplugin.c $(SRC_DIR)\gstpluginfeature.c $(SRC_DIR)\gstprobe.c $(SRC_DIR)\gstquery.c $(SRC_DIR)\gstqueue.c \
$(SRC_DIR)\gstregistry.c $(SRC_DIR)\gstregistrypool.c $(SRC_DIR)\gstscheduler.c $(SRC_DIR)\gststructure.c $(SRC_DIR)\gstsystemclock.c \
$(SRC_DIR)\gsttag.c $(SRC_DIR)\gstthread.c $(SRC_DIR)\gsttrace.c $(SRC_DIR)\gsttypefind.c $(SRC_DIR)\gsturi.c $(SRC_DIR)\gsturitype.c \
$(SRC_DIR)\gstutils.c $(SRC_DIR)\gstvalue.c $(SRC_DIR)\gstxml.c $(SRC_DIR)\registries\gstxmlregistry.c $(SRC_DIR)\parse\lex._gst_parse_yy.c \
$(SRC_DIR)\..\libs\gst\control\control.c $(SRC_DIR)\..\libs\gst\control\unitconvert.c $(SRC_DIR)\..\libs\gst\control\dparammanager.c \
$(SRC_DIR)\..\libs\gst\control\dparam.c
OBJ  = $(patsubst %.c,%.obj,$(SRC))
INCS = /I. /I.. /I../libs /I../../popt/include /I../../libiconv/include /I../../libxml2/include/libxml2 /I$(GLIB_DIR) /I$(GLIB_DIR)\glib /I$(GLIB_DIR)\gmodule /I$(GLIB_DIR)\build\win32
LDFLAGS = /NOLOGO /DLL /IMPLIB:gstreamer-0.8.lib /MAP:gstreamer.map /LIBPATH:$(GLIB_DIR)/glib /LIBPATH:$(GLIB_DIR)/gobject /LIBPATH:$(GLIB_DIR)/gmodule /LIBPATH:$(GLIB_DIR)/gthread /LIBPATH:../../popt/lib /LIBPATH:../../libxml2/lib
OPTMIZ = /G6 /O2 /Oi /Wp64 /GL
CXXFLAGS = $(INCS) /DWIN32 /D_WINDOWS /DHAVE_CONFIG_H /D_USRDLL /DHAVE_WIN32 /DGST_EXPORTS /nologo


HEADERS = $(SRC_DIR)\gstversion.h $(SRC_DIR)\gstconfig.h $(SRC_DIR)\gstenumtypes.h $(SRC_DIR)\gstenumtypes.c $(SRC_DIR)\gstmarshal.h

ifeq (yes,$(DEBUG))

ifeq (yes,$(MSVCRT))
 CXXFLAGS += /MDd
else
 CXXFLAGS += /MTd
endif
CXXFLAGS += /Zi /DDEBUG /D_DEBUG /RTC1
LDFLAGS += /DEBUG

else

ifeq (yes,$(MSVCRT))
 CXXFLAGS += /MD
else
 CXXFLAGS += /MT
endif
CXXFLAGS += /DGST_DISABLE_GST_DEBUG
LDFLAGS += /OPT:REF
endif

LIBS = libgstreamer-0.8.dll

.PHONY: all all-before all-after clean clean-custom

%.obj : %.cpp
	$(CXX) $< /c $(CXXFLAGS) /Fo$@

%.obj : %.c
	$(CC) $< /c $(CXXFLAGS) /Fo$@

all: lib
	make -f Makefile.inspect
	make -f Makefile.launch
	make -f Makefile.register

lib: $(LIBS)

clean:
	make -f Makefile.inspect  clean
	make -f Makefile.launch   clean
	make -f Makefile.register clean
	$(RM) $(OBJ) $(HEADERS) gstreamer-0.8.lib libgstreamer-0.8.dll gstreamer.map gstreamer-0.8.exp lex._gst_parse_yy.c gst.def

libgstreamer-0.8.dll: gst.def $(HEADERS) $(OBJ)
	link $(LDFLAGS) /OUT:$@ $(OBJ) /DEF:gst.def glib-2.0.lib gobject-2.0.lib gthread-2.0.lib gmodule-2.0.lib libxml2.lib libpopt.lib wsock32.lib


ifeq (yes,$(DEBUG))
gst.def:
	type gstreamer.def > gst.def
	type gstreamer-dbg.def >> gst.def
else
gst.def:
	type gstreamer.def > gst.def
endif

$(SRC_DIR)\gstversion.h: gstversion.h
	copy $< $@

$(SRC_DIR)\gstconfig.h: gstconfig.h
	copy $< $@

$(SRC_DIR)\gstenumtypes.c: gstenumtypes.c
	copy $< $@

$(SRC_DIR)\gstenumtypes.h: gstenumtypes.h
	copy $< $@


$(SRC_DIR)\gstmarshal.h:
	echo #include "gst/gstconfig.h"              > $(SRC_DIR)\gstmarshal.h
	$(GLIB_DIR)\gobject\glib-genmarshal --header --prefix=gst_marshal $(SRC_DIR)/gstmarshal.list >> $(SRC_DIR)/gstmarshal.h
$(SRC_DIR)\gstmarshal.c:
	echo #include "glib-object.h" > $(SRC_DIR)\gstmarshal.c
	echo #include "gstmarshal.h" >> $(SRC_DIR)\gstmarshal.c
	$(GLIB_DIR)\gobject\glib-genmarshal --body --prefix=gst_marshal $(SRC_DIR)/gstmarshal.list >> $(SRC_DIR)/gstmarshal.c

# generated sources, otherwise you should get these files from somewhere else
$(SRC_DIR)\parse\grammar.tab.c:
	bison -d -v -p_gst_parse__yy $(SRC_DIR)\parse\grammar.y -o $(SRC_DIR)\parse\grammar.tab.c

$(SRC_DIR)\parse\lex._gst_parse_yy.c:
	flex -P_gst_parse_yy $(SRC_DIR)\parse\parse.l
	copy lex._gst_parse_yy.c $(SRC_DIR)\parse\lex._gst_parse_yy.c

depend:
	$(CXX) $(CXXFLAGS) -MM $(SRC) > .depend

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
