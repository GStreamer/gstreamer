if HAVE_GTK
GTK_SUBDIRS=dynparams level $(FT2_SUBDIRS)
else
GTK_SUBDIRS=
endif

if USE_GCONF
GCONF_SUBDIRS=gstplay
else
GCONF_SUBDIRS=
endif

SUBDIRS=$(GTK_SUBDIRS) $(GCONF_SUBDIRS) switch
DIST_SUBDIRS=capsfilter dynparams indexing gstplay switch level
