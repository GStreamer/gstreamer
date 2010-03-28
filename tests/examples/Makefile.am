if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if USE_GIO
GIO_SUBDIRS = gio
endif

SUBDIRS = app $(FT2_SUBDIRS) $(GIO_SUBDIRS) volume dynamic v4l overlay

DIST_SUBDIRS = app seek volume dynamic snapshot gio v4l overlay

include $(top_srcdir)/common/parallel-subdirs.mak
