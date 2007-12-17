DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXTERNAL
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif

SUBDIRS = 			\
	gst-libs 		\
	gst sys $(SUBDIRS_EXT) 	\
	tools 			\
	tests 			\
	docs			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

DIST_SUBDIRS = 			\
	docs			\
	gst-libs		\
	gst sys ext		\
	tools 			\
	tests 			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-plugins-base.spec depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-base.doap autogen.sh $(win32)

DISTCLEANFILES = _stdint.h

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

WIN32_COPY = \
	$(top_builddir)/gst-libs/gst/*/*-enumtypes.[ch] \
	$(top_builddir)/_stdint.h

win32-update:
	for f in $(WIN32_COPY); do cp $$f win32/common;done

include $(top_srcdir)/common/coverage/lcov.mak

check: check-exports


