plugin_LTLIBRARIES = libgstwaylandsink.la

BUILT_SOURCES = \
	viewporter-protocol.c \
	viewporter-client-protocol.h \
	linux-dmabuf-unstable-v1-protocol.c \
	linux-dmabuf-unstable-v1-client-protocol.h \
	fullscreen-shell-unstable-v1-protocol.c \
	fullscreen-shell-unstable-v1-client-protocol.h

libgstwaylandsink_la_SOURCES =  \
	gstwaylandsink.c \
	wlshmallocator.c \
	wlbuffer.c \
	wldisplay.c \
	wlwindow.c \
	wlvideoformat.c \
	wllinuxdmabuf.c

nodist_libgstwaylandsink_la_SOURCES = \
	viewporter-protocol.c \
	linux-dmabuf-unstable-v1-protocol.c \
	fullscreen-shell-unstable-v1-protocol.c

libgstwaylandsink_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(WAYLAND_CFLAGS)
libgstwaylandsink_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstallocators-$(GST_API_VERSION) \
	$(WAYLAND_LIBS)
libgstwaylandsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstwaylandsink.h \
	wlshmallocator.h \
	wlbuffer.h \
	wldisplay.h \
	wlwindow.h \
	wlvideoformat.h \
	wllinuxdmabuf.h

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
