
if HAVE_GNOME
GNOME_SUBDS = autoplug
else
GNOME_SUBDS =
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

SUBDIRS = $(GNOME_SUBDS) $(GST_LOADSAVE_DIRS) \
          helloworld helloworld2 \
          queue queue2 queue3 queue4 \
          launch thread plugins mixer cutter

DIST_SUBDIRS = autoplug \
          helloworld helloworld2 \
          queue queue2 queue3 queue4 \
          launch thread xml plugins typefind mixer cutter

