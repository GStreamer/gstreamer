SUBDIRS = \
	tag \
	fft \
	sdp \
	rtsp \
	video \
	audio \
	rtp \
	pbutils \
	riff \
	app \
	allocators

noinst_HEADERS = gettext.h gst-i18n-app.h gst-i18n-plugin.h glib-compat-private.h

# dependencies:
audio: tag

riff: tag audio

rtsp: sdp

pbutils: video audio

rtp: audio

INDEPENDENT_SUBDIRS = \
	tag audio fft sdp video app

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
