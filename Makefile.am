DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

COMMON_SUBDIRS = ext docs common tests
SUBDIRS = $(LIBAV_SUBDIRS) $(COMMON_SUBDIRS) pkgconfig
DIST_SUBDIRS = $(COMMON_SUBDIRS) gst-libs pkgconfig

EXTRA_DIST = \
	depcomp autogen.sh \
	AUTHORS COPYING NEWS README ChangeLog gst-libav.doap \
	meson_options.txt \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" )

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak

if HAVE_GST_CHECK
check-valgrind:
	$(MAKE) -C tests/check check-valgrind

check-torture:
	$(MAKE) -C tests/check torture

build-checks:
	$(MAKE) -C tests/check build-checks
else
check-valgrind:
	echo "'check' library not installed, skipping"

check-torture:
	echo "'check' library not installed, skipping"

build-checks:
	echo "'check' library not installed, skipping"
endif

CRUFT_FILES = \
	$(top_builddir)/gst-libav.spec \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4

CRUFT_DIRS = \
	$(top_srcdir)/docs/plugins/tmpl

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
