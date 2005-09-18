DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXTERNAL
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif

SUBDIRS = 			\
	gst-libs 		\
	gst sys $(SUBDIRS_EXT) 	\
	$(SUBDIRS_EXAMPLES) 	\
	tools 			\
	$(SUBDIRS_CHECK)	\
	docs			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

DIST_SUBDIRS = 			\
	check			\
	docs			\
	gst-libs		\
	gst sys ext		\
	examples		\
	tools 			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

EXTRA_DIST = \
	gst-plugins-base.spec depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd check && make check-valgrind
