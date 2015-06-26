
plugin_LTLIBRARIES = libgstisomp4.la

libgstisomp4_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstisomp4_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgstriff-@GST_API_VERSION@ \
    -lgstaudio-@GST_API_VERSION@ \
    -lgstvideo-@GST_API_VERSION@ \
    -lgstrtp-@GST_API_VERSION@ \
    -lgsttag-@GST_API_VERSION@ \
    -lgstpbutils-@GST_API_VERSION@ \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgstisomp4_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstisomp4_la_SOURCES = isomp4-plugin.c gstrtpxqtdepay.c \
	qtdemux.c qtdemux_types.c qtdemux_dump.c qtdemux_lang.c \
	gstqtmux.c gstqtmoovrecover.c atoms.c atomsrecovery.c descriptors.c \
	properties.c gstqtmuxmap.c gstisoff.c
libgstisomp4_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstQTMux.prs

noinst_HEADERS = \
	qtatomparser.h   \
	qtdemux.h        \
	qtdemux_types.h  \
	qtdemux_dump.h   \
	qtdemux_lang.h   \
	qtpalette.h      \
	gstrtpxqtdepay.h \
	gstqtmux.h       \
	gstqtmoovrecover.h \
	atoms.h          \
	atomsrecovery.h  \
	descriptors.h    \
	properties.h     \
	fourcc.h         \
	gstisoff.h       \
	gstqtmuxmap.h

EXTRA_DIST = \
	gstqtmux-doc.c \
	gstqtmux-doc.h \
	$(preset_DATA)
