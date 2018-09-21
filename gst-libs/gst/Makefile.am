if USE_WAYLAND
WAYLAND_DIR=wayland
endif

if USE_OPENCV
OPENCV_DIR=opencv
endif

SUBDIRS = uridownloader adaptivedemux interfaces basecamerabinsrc codecparsers \
	 insertbin mpegts video audio sctp player isoff webrtc $(WAYLAND_DIR) \
	 $(OPENCV_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = uridownloader adaptivedemux interfaces basecamerabinsrc \
	codecparsers insertbin mpegts wayland opencv video audio player isoff sctp webrtc

adaptivedemux: uridownloader

INDEPENDENT_SUBDIRS = \
	interfaces basecamerabinsrc codecparsers insertbin uridownloader \
	mpegts player isoff sctp $(WAYLAND_DIR) $(OPENCV_DIR)

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
