# The interfaces directory has to be built before the others,
# otherwise some generated header files will be missing for the
# plugins in the other directories.
# Also, the tag directory has to be built before the cdda directory.
SUBDIRS = \
	interfaces \
	tag \
	audio \
	cdda \
	floatcast \
	netbuffer \
	riff \
	rtp \
	utils \
	video

noinst_HEADERS = gettext.h gst-i18n-plugin.h
