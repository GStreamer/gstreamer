plugin_LTLIBRARIES = libgstwaylandsink.la

libgstwaylandsink_la_SOURCES =  \
	gstwaylandsink.c \
	waylandpool.c \
	wldisplay.c \
	wlwindow.c \
	wlvideoformat.c \
	scaler-protocol.c

libgstwaylandsink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
                               $(WAYLAND_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
libgstwaylandsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(WAYLAND_LIBS) \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la
libgstwaylandsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwaylandsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstwaylandsink.h \
	waylandpool.h \
	wldisplay.h \
	wlwindow.h \
	wlvideoformat.h \
	scaler-client-protocol.h

EXTRA_DIST = scaler.xml
CLEANFILES = scaler-protocol.c scaler-client-protocol.h

%-protocol.c : %.xml
	$(wayland_scanner) code < $< > $@

%-client-protocol.h : %.xml
	$(wayland_scanner) client-header < $< > $@

gstwaylandsink.c: scaler-client-protocol.h

waylandpool.c: scaler-client-protocol.h

wldisplay.c: scaler-client-protocol.h

wlwindow.c: scaler-client-protocol.h
