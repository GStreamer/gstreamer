if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
else
GST_REGISTRY_SRC = gst-register
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
else
GST_LOADSAVE_SRC = gst-compprep
endif

bin_PROGRAMS = 	gst-launch 	\
		$(GST_REGISTRY_SRC) 	\
		gst-inspect 	\
		$(GST_LOADSAVE_SRC) 	\
		gst-complete

man_MANS = gst-launch.1 gst-register.1 gst-inspect.1 \
	gst-complete.1 gst-compprep.1

gst_launch_LDADD = $(GST_LIBS) #-lefence
gst_launch_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

if !GST_DISABLE_REGISTRY
gst_register_LDADD = $(GST_LIBS)
gst_register_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 
endif

gst_inspect_LDADD = $(GST_LIBS)
gst_inspect_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

gst_complete_LDADD = $(GST_LIBS)
gst_complete_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 

if !GST_DISABLE_LOADSAVE
gst_compprep_LDADD = $(GST_LIBS)
gst_compprep_CFLAGS = $(GST_CFLAGS) -DGST_CONFIG_DIR=\"$(GST_CONFIG_DIR)\" 
endif

EXTRA_DIST = $(man_MANS)
