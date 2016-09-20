plugin_LTLIBRARIES = libgstwaylandsink.la

BUILT_SOURCES = viewporter-protocol.c viewporter-client-protocol.h

libgstwaylandsink_la_SOURCES =  \
	gstwaylandsink.c \
	wlshmallocator.c \
	wlbuffer.c \
	wldisplay.c \
	wlwindow.c \
	wlvideoformat.c

nodist_libgstwaylandsink_la_SOURCES = viewporter-protocol.c

libgstwaylandsink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
                               $(WAYLAND_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
libgstwaylandsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstallocators-$(GST_API_VERSION) \
	$(WAYLAND_LIBS) \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la
libgstwaylandsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwaylandsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstwaylandsink.h \
	wlshmallocator.h \
	wlbuffer.h \
	wldisplay.h \
	wlwindow.h \
	wlvideoformat.h

CLEANFILES = $(BUILT_SOURCES)

.SECONDEXPANSION:

define protostability
$(if $(findstring unstable,$1),unstable,stable)
endef

define protoname
$(shell echo $1 | sed 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(wayland_scanner) code < $< > $@

%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(wayland_scanner) client-header < $< > $@
