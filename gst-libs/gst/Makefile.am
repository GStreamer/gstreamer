if USE_GCONF
GCONF_DIR = gconf
else
GCONF_DIR =
endif

if USE_X
X_DIR = xwindowlistener xoverlay
X_INTERFACE_LIBS = xoverlay/libgstxoverlay.la
else
X_DIR =
X_INTERFACE_LIBS =
endif

SUBDIRS = \
	audio \
	colorbalance \
	floatcast \
	$(GCONF_DIR) \
	idct \
	media-info \
	mixer \
	navigation \
	play \
	propertyprobe \
	resample \
	riff \
	tag \
	tuner \
	video \
	$(X_DIR)

DIST_SUBDIRS = \
	audio \
	colorbalance \
	floatcast \
	gconf \
	idct \
	media-info \
	mixer \
	navigation \
	play \
	propertyprobe \
	resample \
	riff \
	tag \
	tuner \
	video \
	xoverlay \
	xwindowlistener


lib_LTLIBRARIES = libgstinterfaces-@GST_MAJORMINOR@.la

libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = 

libgstinterfaces_@GST_MAJORMINOR@_la_LIBADD = \
	colorbalance/libgstcolorbalance.la \
	mixer/libgstmixer.la \
	navigation/libgstnavigation.la \
	propertyprobe/libgstpropertyprobe.la \
	tuner/libgsttuner.la \
        $(X_INTERFACE_LIBS) \
	$(GST_LIBS)

libgstinterfaces_@GST_MAJORMINOR@_la_LDFLAGS = @GST_PLUGINS_LT_LDFLAGS@ -version-info @GST_PLUGINS_LIBVERSION@

noinst_HEADERS = gst-i18n-plugin.h
