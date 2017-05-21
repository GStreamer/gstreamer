DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

ALWAYS_SUBDIRS =		\
	gst sys ext 		\
	tests			\
	docs			\
	po			\
	common			\
	m4			\
	pkgconfig

SUBDIRS = $(ALWAYS_SUBDIRS)

DIST_SUBDIRS = $(ALWAYS_SUBDIRS)

EXTRA_DIST = \
	depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-good.doap autogen.sh \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	meson_options.txt

DISTCLEANFILES = _stdint.h

noinst_HEADERS = \
	gst-libs/gst/gettext.h \
	gst-libs/gst/gst-i18n-plugin.h \
	gst-libs/gst/glib-compat-private.h

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
	$(top_builddir)/gst-plugins-good.spec \
	$(top_builddir)/win32/common/config.h-new \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/docs/plugins/xml/plugin-shout2send.xml \
	$(top_builddir)/ext/pulse/.libs/libgstpulse.so \
	$(top_builddir)/ext/soup/.libs/libgstsouphttpsrc.so \
	$(top_builddir)/gst/median/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/qtdemux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/quicktime/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*videoflip.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*videobalance.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*gamma.{so,dll,DLL,dylib} \
	$(top_builddir)/sys/oss4/.libs/libgstoss4audio.so

CRUFT_DIRS = \
	$(top_srcdir)/docs/plugins/tmpl \
	$(top_srcdir)/gst/median \
	$(top_srcdir)/gst/qtdemux \
	$(top_srcdir)/gst/quicktime \
	$(top_srcdir)/ext/annodex \
	$(top_srcdir)/tests/examples/pulse

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
