if USE_GCONFTOOL
GCONF_DIR = gconf
else
GCONF_DIR =
endif

SUBDIRS = 			\
	gst sys ext 		\
	$(GCONF_DIR) 		\
	m4 common po

DIST_SUBDIRS = 			\
	gst sys ext 		\
	gconf			\
	m4 common po

EXTRA_DIST = \
	gst-plugins-good.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
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
