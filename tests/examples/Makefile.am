if HAVE_GTK
GTK_SUBDIRS = playback seek snapshot
endif

SUBDIRS = app audio decodebin_next dynamic fft $(GTK_SUBDIRS) gio overlay playrec encoding
DIST_SUBDIRS = app audio dynamic decodebin_next fft gio playback overlay seek snapshot playrec encoding

include $(top_srcdir)/common/parallel-subdirs.mak

