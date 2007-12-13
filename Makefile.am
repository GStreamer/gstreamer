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

win32 = $(shell cat $(top_srcdir)/win32/MANIFEST)

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

# make sure all symbols we export on linux are defined in the win32 .def too
# (don't care about other unixes for now, it's enough if it works on one of
# the linux build bots; we assume .so )
baselibs='libgstaudio libgstrtp libgstpbutils libgsttag libgstnetbuffer \
libgstinterfaces libgstvideo libgstriff libgstrtsp libgstcdda'
check-exports:
	for l in `eval echo $(baselibs)`; do \
	  libso=`find "$(top_builddir)" -name "$$l-0.10.so"`; \
	  libdef="$(top_srcdir)/win32/common/$$l.def"; \
	  if test "x$$libso" != "x"; then \
	    echo Checking symbols in $$libso; \
	    $(top_srcdir)/common/check-exports $$libdef $$libso || exit 1; \
	  fi; \
	done

check: check-exports


