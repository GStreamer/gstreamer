if USE_OPENGL
GL_DIR = gl
endif
if USE_GLES2
GL_DIR = gl
endif

SUBDIRS = \
	tag \
	fft \
	audio \
	rtp \
	sdp \
	rtsp \
	video \
	pbutils \
	riff \
	app \
	allocators \
	$(GL_DIR)

DIST_SUBDIRS = \
	tag \
	fft \
	audio \
	rtp \
	sdp \
	rtsp \
	video \
	pbutils \
	riff \
	app \
	allocators \
	gl

noinst_HEADERS = gettext.h gst-i18n-app.h gst-i18n-plugin.h glib-compat-private.h

# dependencies:
audio: tag

riff: tag audio

rtsp: sdp

pbutils: video audio

rtp: audio

sdp: rtp

gl: video allocators

INDEPENDENT_SUBDIRS = \
	tag audio fft video app

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
