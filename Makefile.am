DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = $(FFMPEG_SUBDIRS) ext docs common tests

win32 = $(shell cat $(top_srcdir)/win32/MANIFEST)

debug:
	echo $(win32)	

EXTRA_DIST = \
	gst-ffmpeg.spec depcomp ffmpegrev \
	AUTHORS COPYING NEWS README ChangeLog gst-ffmpeg.doap \
	$(win32)

ACLOCAL_AMFLAGS = -I common/m4

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true

check-torture:
	@true
