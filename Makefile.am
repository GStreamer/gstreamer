DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst-libs gst ext docs m4 tests common pkgconfig po
	
EXTRA_DIST = \
	depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-ugly.doap autogen.sh \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	meson_options.txt config.h.meson

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	$(MAKE) -C tests/check check-valgrind

if HAVE_GST_CHECK
check-torture:
	$(MAKE) -C tests/check torture
build-checks:
	$(MAKE) -C tests/check build-checks
else
check-torture:
	true
build-checks:
	true
endif

include $(top_srcdir)/common/coverage/lcov.mak

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/gst-plugins-ugly.spec \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool

CRUFT_DIRS = \
	$(top_srcdir)/docs/plugins/tmpl \
	$(top_builddir)/win32 \
	$(top_srcdir)/win32

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
