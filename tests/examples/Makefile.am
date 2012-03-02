if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if USE_GIO
GIO_SUBDIRS = gio
endif

if HAVE_GTK
GTK_SUBDIRS = playback
endif

SUBDIRS = app audio dynamic fft $(FT2_SUBDIRS) $(GIO_SUBDIRS) overlay $(GTK_SUBDIRS) playrec v4l encoding

DIST_SUBDIRS = app audio dynamic fft gio overlay seek snapshot playback playrec v4l encoding

include $(top_srcdir)/common/parallel-subdirs.mak

