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
	pbutils \
	riff \
	rtp \
	sdp \
	rtsp \
	video \
	app

noinst_HEADERS = gettext.h gst-i18n-plugin.h

# dependencies:
audio: interfaces

cdda: tag

riff: tag

rtsp: sdp
