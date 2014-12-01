
if USE_OPENGL
GL_DIR = gl
endif
if USE_GLES2
GL_DIR = gl
endif

if USE_WAYLAND
WAYLAND_DIR=wayland
endif

SUBDIRS = adaptivedemux interfaces basecamerabinsrc codecparsers \
	 insertbin uridownloader mpegts base video $(GL_DIR) $(WAYLAND_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = adaptivedemux interfaces gl basecamerabinsrc codecparsers \
	insertbin uridownloader mpegts wayland base video

#dependencies
video: base

adaptivedemux: uridownloader

INDEPENDENT_SUBDIRS = \
	interfaces basecamerabinsrc codecparsers insertbin uridownloader \
	mpegts base $(GL_DIR) $(WAYLAND_DIR)

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
