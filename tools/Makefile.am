bin_PROGRAMS = gstreamer-launch gstreamer-register gstreamer-inspect \
	gstreamer-compprep gstreamer-complete

man_MANS = gstreamer-launch.1 gstreamer-register.1 gstreamer-inspect.1 \
	gstreamer-complete.1 gstreamer-compprep.1

LDADD = $(GST_LIBS)
CFLAGS = $(LIBGST_CFLAGS) -DGST_CONFIG_DIR=\""@sysconfdir@/gstreamer"\"

EXTRA_DIST = $(man_MANS)
