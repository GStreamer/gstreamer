
if USE_OPENGL
GL_DIR = gl
endif
if USE_GLES2
GL_DIR = gl
endif

if USE_WAYLAND
WAYLAND_DIR=wayland
endif

if USE_OPENCV
OPENCV_DIR=opencv
endif

SUBDIRS = uridownloader adaptivedemux interfaces basecamerabinsrc codecparsers \
	 insertbin mpegts base video audio player allocators $(GL_DIR) $(WAYLAND_DIR) \
	 $(OPENCV_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = uridownloader adaptivedemux interfaces gl basecamerabinsrc \
	codecparsers insertbin mpegts wayland opencv base video audio player allocators

#dependencies
video, audio: base
gl: allocators

adaptivedemux: uridownloader

INDEPENDENT_SUBDIRS = \
	interfaces basecamerabinsrc codecparsers insertbin uridownloader \
	mpegts base player allocators $(GL_DIR) $(WAYLAND_DIR) $(OPENCV_DIR)

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
