if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if USE_GIO
GIO_SUBDIRS = gio
endif

SUBDIRS = app audio dynamic fft $(FT2_SUBDIRS) $(GIO_SUBDIRS) overlay playrec v4l encoding

DIST_SUBDIRS = app audio dynamic fft gio overlay seek snapshot playrec v4l encoding

include $(top_srcdir)/common/parallel-subdirs.mak

