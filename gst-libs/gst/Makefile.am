# The interfaces directory has to be built before the others,
# otherwise some generated header files will be missing for the
# plugins in the other directories.
# Also, the tag directory has to be built before the cdda directory.
SUBDIRS = \
	interfaces \
	tag \
	audio \
	cdda \
	fft \
	floatcast \
	netbuffer \
	riff \
	rtp \
	sdp \
	rtsp \
	video \
	pbutils \
	app

noinst_HEADERS = gettext.h gst-i18n-plugin.h

# dependencies:
audio: interfaces

cdda: tag

riff: tag audio

rtsp: sdp

pbutils: video

INDEPENDENT_SUBDIRS = \
	interfaces tag fft floatcast netbuffer rtp sdp video app

.PHONY: independent-subdirs $(INDEPENDENT_SUBDIRS)

independent-subdirs: $(INDEPENDENT_SUBDIRS)

$(INDEPENDENT_SUBDIRS):
	$(MAKE) -C $@

all-recursive: independent-subdirs
