if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if USE_GIO
GIO_SUBDIRS = gio
endif

SUBDIRS = app audio dynamic $(FT2_SUBDIRS) $(GIO_SUBDIRS) overlay playrec volume encoding

DIST_SUBDIRS = app dynamic gio overlay seek snapshot playrec volume encoding

include $(top_srcdir)/common/parallel-subdirs.mak
