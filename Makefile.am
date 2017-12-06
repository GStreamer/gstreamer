DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

if BUILD_TESTS
SUBDIRS_TESTS = tests
else
SUBDIRS_TESTS =
endif

SUBDIRS = \
	gst \
	common \
	pkgconfig \
	docs \
	$(SUBDIRS_EXAMPLES) \
	$(SUBDIRS_TESTS)

DIST_SUBDIRS = gst common pkgconfig docs examples tests

EXTRA_DIST = \
	ChangeLog autogen.sh depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	docs/design/gst-rtp-server-design \
	gst-rtsp-server.doap \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	meson_options.txt

ACLOCAL_AMFLAGS = -I m4 -I common/m4

DISTCLEANFILES = _stdint.h

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

include $(top_srcdir)/common/coverage/lcov.mak

check-valgrind:
	$(MAKE) -C tests/check check-valgrind

if HAVE_CHECK
check-torture:
	$(MAKE) -C tests/check torture
build-checks:
	$(MAKE) -C tests/check  build-checks
else
check-torture:
	true
build-checks:
	true
endif

# cruft: plugins that have been merged or moved or renamed
CRUFT_FILES = \
	$(top_builddir)/gst-rtsp.spec \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/codeset.m4 \
	$(top_builddir)/common/m4/gettext.m4 \
	$(top_builddir)/common/m4/glibc2.m4 \
	$(top_builddir)/common/m4/glibc21.m4 \
	$(top_builddir)/common/m4/iconv.m4 \
	$(top_builddir)/common/m4/intdiv0.m4 \
	$(top_builddir)/common/m4/intl.m4 \
	$(top_builddir)/common/m4/intldir.m4 \
	$(top_builddir)/common/m4/intlmacosx.m4 \
	$(top_builddir)/common/m4/intmax.m4 \
	$(top_builddir)/common/m4/inttypes-pri.m4 \
	$(top_builddir)/common/m4/inttypes_h.m4 \
	$(top_builddir)/common/m4/lcmessage.m4 \
	$(top_builddir)/common/m4/lib-ld.m4 \
	$(top_builddir)/common/m4/lib-link.m4 \
	$(top_builddir)/common/m4/lib-prefix.m4 \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/lock.m4 \
	$(top_builddir)/common/m4/longlong.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4 \
	$(top_builddir)/common/m4/nls.m4 \
	$(top_builddir)/common/m4/po.m4 \
	$(top_builddir)/common/m4/printf-posix.m4 \
	$(top_builddir)/common/m4/progtest.m4 \
	$(top_builddir)/common/m4/size_max.m4 \
	$(top_builddir)/common/m4/stdint_h.m4 \
	$(top_builddir)/common/m4/uintmax_t.m4 \
	$(top_builddir)/common/m4/visibility.m4 \
	$(top_builddir)/common/m4/wchar_t.m4 \
	$(top_builddir)/common/m4/wint_t.m4 \
	$(top_builddir)/common/m4/xsize.m4

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
