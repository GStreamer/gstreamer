if HAVE_FT2
FT2_SUBDIRS=seeking
else
FT2_SUBDIRS=
endif

if HAVE_GTK
GTK_SUBDIRS=dynparams $(FT2_SUBDIRS)
else
GTK_SUBDIRS=
endif

if USE_GCONF
GCONF_SUBDIRS=gstplay
else
GCONF_SUBDIRS=
endif

SUBDIRS=$(GTK_SUBDIRS) $(GCONF_SUBDIRS) switch
DIST_SUBDIRS=capsfilter dynparams seeking indexing gstplay switch
