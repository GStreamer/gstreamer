if USE_GCONF
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if USE_XFREE
X_DIR=xwindowlistener
else
X_DIR=
endif

SUBDIRS = audio colorbalance floatcast \
		$(GCONF_DIR) idct media-info \
		mixer navigation play propertyprobe \
		resample riff tuner video \
		xoverlay $(X_DIR)

DIST_SUBDIRS = audio colorbalance floatcast \
		gconf idct media-info \
		mixer navigation play propertyprobe \
		resample riff tuner video \
		xoverlay xwindowlistener


lib_LTLIBRARIES = libgst-interfaces-@GST_MAJORMINOR@.la

libgst_interfaces_@GST_MAJORMINOR@_la_SOURCES = 

libgst_interfaces_@GST_MAJORMINOR@_la_LIBADD = \
	colorbalance/libgstcolorbalance.la \
	mixer/libgstmixer.la \
	navigation/libgstnavigation.la \
	propertyprobe/libgstpropertyprobe.la \
	tuner/libgsttuner.la \
	xoverlay/libgstxoverlay.la \
	$(GST_LIBS)

libgst_interfaces_@GST_MAJORMINOR@_la_LDFLAGS = @GST_PLUGINS_LT_LDFLAGS@ -version-info @GST_PLUGINS_LIBVERSION@

