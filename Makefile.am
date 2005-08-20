if USE_GCONFTOOL
SUBDIRS_GCONF = gconf
else
SUBDIRS_GCONF =
endif

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
	$(SUBDIRS_GCONF) 	\
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
	gconf 			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

EXTRA_DIST = \
	gst-plugins-base.spec depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

# check that no marshal or enumtypes files are included
# this in turn ensures that distcheck fails for missing .list files which is currently
# shadowed when the corresponding .c and .h files are included.
distcheck-hook:
	@test "x" = "x`find $(distdir) -name \*-enumtypes.[ch]`" && \
	test "x" = "x`find $(distdir) -name \*-marshal.[ch]`" || \
	( $(ECHO) "*** Leftover enumtypes or marshal files in the tarball." && \
          $(ECHO) "*** Make sure the following files are not disted:" && \
          find $(distdir) -name \*-enumtypes.[ch] && \
          find $(distdir) -name \*-marshal.[ch] && \
          false )

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak
