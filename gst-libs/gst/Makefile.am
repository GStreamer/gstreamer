if USE_GCONF
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if USE_X
X_DIR=xwindowlistener xoverlay
else
X_DIR=
endif

SUBDIRS = audio colorbalance floatcast \
		$(GCONF_DIR) idct media-info \
		mixer navigation play propertyprobe \
		resample riff tuner video \
		$(X_DIR)

DIST_SUBDIRS = audio colorbalance floatcast \
		gconf idct media-info \
		mixer navigation play propertyprobe \
		resample riff tuner video \
		xoverlay xwindowlistener


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

