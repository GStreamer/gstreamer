SUBDIRS = 			\
	gst ext m4 common

EXTRA_DIST = \
	gst-plugins-ugly.spec depcomp \
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
