
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
if HAVE_LIBGLADE_GNOME
GST_GUI_LAUNCH = gstreamer-guilaunch
gstreamer_guilaunch_CFLAGS = \
	$(LIBGLADE_GNOME_CFLAGS) $(CFLAGS)
gstreamer_guilaunch_LDADD = \
	$(LIBGLADE_GNOME_LIBS) $(LDADD)
else
GST_GUI_LAUNCH =
endif
endif

bin_PROGRAMS = 	gstreamer-launch 	\
		$(GST_GUI_LAUNCH)	\
		$(GST_REGISTRY_SRC) 	\
		gstreamer-inspect 	\
		$(GST_LOADSAVE_SRC) 	\
		gstreamer-complete

man_MANS = gstreamer-launch.1 gstreamer-register.1 gstreamer-inspect.1 \
	gstreamer-complete.1 gstreamer-compprep.1 gstreamer-guilaunch.1

gstreamer_launch_LDADD = $(GST_LIBS) #-lefence
gstreamer_launch_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

if !USE_GLIB2
gstreamer_guilaunch_LDADD = $(GST_LIBS)
gstreamer_guilaunch_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 
endif

if !GST_DISABLE_REGISTRY
gstreamer_register_LDADD = $(GST_LIBS)
gstreamer_register_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 
endif

gstreamer_inspect_LDADD = $(GST_LIBS)
gstreamer_inspect_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gstreamer_complete_LDADD = $(GST_LIBS)
gstreamer_complete_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

if !GST_DISABLE_LOADSAVE
gstreamer_compprep_LDADD = $(GST_LIBS)
gstreamer_compprep_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 
endif

EXTRA_DIST = $(man_MANS)
