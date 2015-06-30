plugin_LTLIBRARIES = libgstlibav.la

if HAVE_BZ2
    BZ2_LIBS = -lbz2
else
    BZ2_LIBS =
endif

if HAVE_LZMA
    LZMA_LIBS = -llzma
else
    LZMA_LIBS =
endif

libgstlibav_la_SOURCES = gstav.c	\
			  gstavprotocol.c	\
			  gstavcodecmap.c	\
			  gstavutils.c	\
			  gstavaudenc.c	\
			  gstavvidenc.c	\
			  gstavauddec.c	\
			  gstavviddec.c	\
			  gstavcfg.c	\
			  gstavdemux.c	\
			  gstavmux.c    \
			  gstavdeinterlace.c
#\
#			  gstavaudioresample.c
# 	\
# 			  gstavscale.c

libgstlibav_la_CFLAGS = $(LIBAV_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstlibav_la_LIBADD = $(LIBAV_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	$(LIBM) $(WIN32_LIBS) -lz $(BZ2_LIBS) $(LZMA_LIBS)
libgstlibav_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstlibav_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

if HAVE_LIBAV_UNINSTALLED
libgstlibav_la_DEPENDENCIES = $(LIBAV_DEPS)
endif


noinst_HEADERS = \
	gstav.h \
	gstavcodecmap.h \
	gstavutils.h \
	gstavauddec.h \
	gstavviddec.h \
	gstavaudenc.h \
	gstavvidenc.h \
	gstavcfg.h \
	gstavprotocol.h
