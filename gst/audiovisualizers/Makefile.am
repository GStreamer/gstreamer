plugin_LTLIBRARIES = libgstaudiovisualizers.la

libgstaudiovisualizers_la_SOURCES = plugin.c \
    gstspacescope.c gstspacescope.h \
    gstspectrascope.c gstspectrascope.h \
    gstsynaescope.c gstsynaescope.h \
    gstwavescope.c gstwavescope.h

libgstaudiovisualizers_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaudiovisualizers_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgstvideo-$(GST_API_VERSION) -lgstfft-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS)  $(GST_LIBS) $(LIBM)
libgstaudiovisualizers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiovisualizers_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdrawhelpers.h \
	gstspacescope.h \
	gstspectrascope.h \
	gstsynaescope.h \
	gstwavescope.h
