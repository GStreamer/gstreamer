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
libgstlibav_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
libgstlibav_la_LIBADD =

if HAVE_LIBAV_UNINSTALLED
libgstlibav_la_LIBADD += $(LIBAV_DEPS)
libgstlibav_la_DEPENDENCIES = $(LIBAV_DEPS)
else
libgstlibav_la_LIBADD += $(LIBAV_LIBS)
endif

libgstlibav_la_LIBADD += $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	$(LIBM) $(WIN32_LIBS) -lz $(BZ2_LIBS) $(LZMA_LIBS)
libgstlibav_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)

if HAVE_LIBAV_UNINSTALLED
if GST_PLUGIN_BUILD_STATIC
libgstlibav.la: $(libgstlibav_la_OBJECTS) $(libgstlibav_la_DEPENDENCIES) $(EXTRA_libgstlibav_la_DEPENDENCIES)
	$(AM_V_CCLD)$(libgstlibav_la_LINK) -rpath $(plugindir) $(libgstlibav_la_OBJECTS) $(libgstlibav_la_LIBADD) $(LIBS)
	$(AM_V_at)cp $@ $@.tmp
	$(AM_V_at)cp .libs/$@i .libs/$@i.tmp
	$(AM_V_at)rm $@ .libs/$@i
	$(AM_V_at)$(AR) -d .libs/libgstlibav.a $(foreach dep,$(LIBAV_DEPS), $(notdir $(dep)))
	$(AM_V_at)$(RANLIB) .libs/libgstlibav.a
	$(AM_V_GEN)sed -e "s|^\(dependency_libs='\)|\1 $(LIBAV_LIBS)|" $@.tmp > $@ && rm $@.tmp
	$(AM_V_GEN)sed -e "s|^\(dependency_libs='\)|\1 $(LIBAV_LIBS)|" .libs/$@i.tmp > .libs/$@i && rm .libs/$@i.tmp
endif
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
