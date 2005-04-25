if USE_GCONF
GCONF_DIR = gconf
else
GCONF_DIR =
endif

SUBDIRS = \
	audio \
	colorbalance \
	floatcast \
	$(GCONF_DIR) \
	media-info \
	mixer \
	navigation \
	propertyprobe \
	riff \
	tag \
	tuner \
	video \
	xoverlay \
	.

DIST_SUBDIRS = \
	audio \
	colorbalance \
	floatcast \
	gconf \
	media-info \
	mixer \
	navigation \
	propertyprobe \
	riff \
	tag \
	tuner \
	video \
	xoverlay


lib_LTLIBRARIES = libgstinterfaces-@GST_MAJORMINOR@.la

libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = 

libgstinterfaces_@GST_MAJORMINOR@_la_LIBADD = \
	colorbalance/libgstcolorbalance.la \
	mixer/libgstmixer.la \
	navigation/libgstnavigation.la \
	propertyprobe/libgstpropertyprobe.la \
	tuner/libgsttuner.la \
	xoverlay/libgstxoverlay.la \
	$(GST_LIBS)

libgstinterfaces_@GST_MAJORMINOR@_la_LDFLAGS = @GST_PLUGINS_LT_LDFLAGS@ -version-info @GST_PLUGINS_LIBVERSION@

noinst_HEADERS = gettext.h gst-i18n-plugin.h
