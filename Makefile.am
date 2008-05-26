DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = $(FFMPEG_SUBDIRS) ext docs common tests

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-ffmpeg.spec depcomp ffmpegrev autogen.sh \
	AUTHORS COPYING NEWS README ChangeLog gst-ffmpeg.doap \
	$(win32)

ACLOCAL_AMFLAGS = -I common/m4

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true

check-torture:
	@true
