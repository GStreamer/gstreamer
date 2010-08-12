if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if USE_GIO
GIO_SUBDIRS = gio
endif

SUBDIRS = app  dynamic $(FT2_SUBDIRS) $(GIO_SUBDIRS) overlay playback volume v4l

DIST_SUBDIRS = app dynamic gio overlay playback seek snapshot volume v4l

include $(top_srcdir)/common/parallel-subdirs.mak
