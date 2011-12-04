# The interfaces directory has to be built before the others,
# otherwise some generated header files will be missing for the
# plugins in the other directories.
# Also, the tag directory has to be built before the cdda directory.
SUBDIRS = \
	interfaces \
	tag \
	cdda \
	fft \
	floatcast \
	netbuffer \
	rtp \
	sdp \
	rtsp \
	video \
	pbutils \
	audio \
	riff \
	app

noinst_HEADERS = gettext.h gst-i18n-plugin.h glib-compat-private.h

# dependencies:
audio: interfaces pbutils

cdda: tag

riff: tag audio

rtsp: sdp

INDEPENDENT_SUBDIRS = \
	interfaces tag fft floatcast netbuffer pbutils rtp sdp video app

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
