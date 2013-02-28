DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

COMMON_SUBDIRS = ext docs common tests
SUBDIRS = $(LIBAV_SUBDIRS) $(COMMON_SUBDIRS) pkgconfig
DIST_SUBDIRS = $(COMMON_SUBDIRS) gst-libs pkgconfig

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-libav.spec depcomp autogen.sh \
	AUTHORS COPYING NEWS README ChangeLog gst-libav.doap \
	$(win32)

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true

check-torture:
	@true

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
