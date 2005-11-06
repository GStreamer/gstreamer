#if HAVE_GTK
#GTK_SUBDIRS=dynparams level $(FT2_SUBDIRS)
#else
#GTK_SUBDIRS=
#endif

if USE_DIRECTFB
DIRECTFB_DIR=directfb
else
DIRECTFB_DIR=
endif

SUBDIRS=$(GTK_SUBDIRS) $(GCONF_SUBDIRS) $(DIRECTFB_DIR)
DIST_SUBDIRS=capsfilter dynparams indexing gstplay directfb switch level
