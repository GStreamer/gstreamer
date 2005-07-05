if BUILD_DOCS
# disabled until it actually works
# SUBDIRS_DOCS = docs
SUBDIRS_DOCS =
else
SUBDIRS_DOCS =
endif

# if USE_GCONFTOOL
# GCONF_DIR = gconf
# else
GCONF_DIR =
# endif

SUBDIRS = 			\
	gst sys ext gst-libs	\
	$(GCONF_DIR) 		\
	m4 common		
# disabled
#	$(SUBDIRS_DOCS)

DIST_SUBDIRS = 			\
	gst sys ext gst-libs	\
	m4 common

# disabled
#	docs

EXTRA_DIST = \
	gst-plugins.spec depcomp \
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

# include $(top_srcdir)/common/release.mak
# include $(top_srcdir)/common/po.mak
