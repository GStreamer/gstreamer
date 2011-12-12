DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if USE_GCONFTOOL
GCONF_DIR = gconf
else
GCONF_DIR =
endif

ALWAYS_SUBDIRS =		\
	gst sys ext 		\
	tests			\
	docs			\
	po			\
	common			\
	m4			\
	pkgconfig

SUBDIRS = 			\
	$(ALWAYS_SUBDIRS)	\
	$(GCONF_DIR)

DIST_SUBDIRS = 			\
	$(ALWAYS_SUBDIRS)	\
	gconf


# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-plugins-good.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-good.doap autogen.sh \
	$(win32)

DISTCLEANFILES = _stdint.h

noinst_HEADERS = \
	gst-libs/gst/gettext.h \
	gst-libs/gst/gst-i18n-plugin.h \
	gst-libs/gst/glib-compat-private.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

win32-update:
	for f in gst/udp/gstudp-marshal.c \
		 gst/udp/gstudp-marshal.h \
		 gst/udp/gstudp-enumtypes.c \
		 gst/udp/gstudp-enumtypes.h \
		 gst/rtpmanager/gstrtpbin-marshal.c \
		 gst/rtpmanager/gstrtpbin-marshal.h ; do \
		cp $(top_builddir)/$$f win32/common; done
	$(top_srcdir)/common/gst-indent win32/common/gstudp-marshal.c
	$(top_srcdir)/common/gst-indent win32/common/gstudp-marshal.c
	$(top_srcdir)/common/gst-indent win32/common/gstudp-enumtypes.c
	$(top_srcdir)/common/gst-indent win32/common/gstudp-enumtypes.c
	$(top_srcdir)/common/gst-indent gst/rtpmanager/gstrtpbin-marshal.c
	$(top_srcdir)/common/gst-indent gst/rtpmanager/gstrtpbin-marshal.c
	cp $(top_builddir)/win32/common/config.h-new \
	    $(top_srcdir)/win32/common/config.h

include $(top_srcdir)/common/coverage/lcov.mak

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/gst/qtdemux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/quicktime/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*videoflip.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*videobalance.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videofilter/.libs/*gamma.{so,dll,DLL,dylib}

CRUFT_DIRS = \
	$(top_srcdir)/gst/qtdemux \
	$(top_srcdir)/gst/quicktime

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
