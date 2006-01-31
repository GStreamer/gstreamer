SUBDIRS = gst-libs ext docs
EXTRA_DIST=gst-ffmpeg.spec depcomp \
	AUTHORS COPYING NEWS README ChangeLog

ACLOCAL_AMFLAGS = -I common/m4

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true

check-torture:
	@true
