if HAVE_FT2
FT2_SUBDIRS = seek snapshot
else
FT2_SUBDIRS =
endif

if HAVE_GTK
GTK_SUBDIRS = playback
endif

SUBDIRS = app audio dynamic fft $(FT2_SUBDIRS) $(GTK_SUBDIRS) gio overlay playrec encoding
DIST_SUBDIRS = app audio dynamic fft gio playback overlay seek snapshot playrec encoding

include $(top_srcdir)/common/parallel-subdirs.mak

