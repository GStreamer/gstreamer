
if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
else
GST_REGISTRY_SRC = gstreamer-register
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
else
GST_LOADSAVE_SRC = gstreamer-compprep
endif

if USE_GLIB2
GST_GUI_LAUNCH = 
else
GST_GUI_LAUNCH = gstreamer-guilaunch
endif

bin_PROGRAMS = 	gstreamer-launch 	\
		$(GST_GUI_LAUNCH)	\
		$(GST_REGISTRY_SRC) 	\
		gstreamer-inspect 	\
		$(GST_LOADSAVE_SRC) 	\
		gstreamer-complete

man_MANS = gstreamer-launch.1 gstreamer-register.1 gstreamer-inspect.1 \
	gstreamer-complete.1 gstreamer-compprep.1

gstreamer_launch_LDADD = $(GST_LIBS)
gstreamer_launch_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_guilaunch_LDADD = $(GST_LIBS)
gstreamer_guilaunch_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_register_LDADD = $(GST_LIBS)
gstreamer_register_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_inspect_LDADD = $(GST_LIBS)
gstreamer_inspect_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_complete_LDADD = $(GST_LIBS)
gstreamer_complete_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_compprep_LDADD = $(GST_LIBS)
gstreamer_compprep_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

EXTRA_DIST = $(man_MANS)
