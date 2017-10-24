plugin_LTLIBRARIES = libgstlibav.la

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

libgstlibav_la_CPPFLAGS = $(LIBAV_CPPFLAGS) $(GST_PLUGINS_BASE_CPPFLAGS) $(GST_CPPFLAGS)
libgstlibav_la_CFLAGS = $(LIBAV_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstlibav_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	 $(LIBAV_LIBS) $(WIN32_LIBS) -lz $(BZ2_LIBS) $(LZMA_LIBS)
libgstlibav_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)

if HAVE_LIBAV_UNINSTALLED
libgstlibav_la_DEPENDENCIES = $(LIBAV_DEPS)

# FIXME: For some reason libtool ignores libavfilter.la, add it
install-data-hook:
	$(AM_V_at)cd $(DESTDIR)$(plugindir) \
		&& sed -e "s|\($(libdir)/libavformat.la\)|$(libdir)/libavfilter.la \1|" \
		libgstlibav.la > libgstlibav.la.tmp && cp libgstlibav.la.tmp libgstlibav.la && rm libgstlibav.la.tmp
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
