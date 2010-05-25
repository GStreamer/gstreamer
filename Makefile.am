DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

COMMON_SUBDIRS = ext docs common tests
SUBDIRS = $(FFMPEG_SUBDIRS) $(COMMON_SUBDIRS)
DIST_SUBDIRS = $(COMMON_SUBDIRS) gst-libs

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

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
