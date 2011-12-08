# The interfaces directory has to be built before the others,
# otherwise some generated header files will be missing for the
# plugins in the other directories.
SUBDIRS = \
	interfaces \
	tag \
	fft \
	rtp \
	sdp \
	rtsp \
	video \
	audio \
	pbutils \
	riff \
	app

noinst_HEADERS = gettext.h gst-i18n-plugin.h glib-compat-private.h

# dependencies:
audio: interfaces tag

riff: tag audio

rtsp: sdp

pbutils: video audio

INDEPENDENT_SUBDIRS = \
	interfaces tag audio fft rtp sdp video app

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
