
if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
else
GST_REGISTRY_SRC = gstreamer-register
endif

bin_PROGRAMS = 	gstreamer-launch 	\
		$(GST_REGISTRY_SRC) 	\
		gstreamer-inspect 	\
		gstreamer-compprep 	\
		gstreamer-complete

man_MANS = gstreamer-launch.1 gstreamer-register.1 gstreamer-inspect.1 \
	gstreamer-complete.1 gstreamer-compprep.1

LDADD = $(GST_LIBS)
CFLAGS = $(LIBGST_CFLAGS) -DGST_CONFIG_DIR=\""@sysconfdir@/gstreamer"\"

EXTRA_DIST = $(man_MANS)
