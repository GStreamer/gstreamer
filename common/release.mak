# include this snippet to add a common release: target by using
# include $(top_srcdir)/common/release.mak

# make bz2 as well
AUTOMAKE_OPTIONS = dist-bzip2

release: dist
	make $(PACKAGE)-$(VERSION).tar.gz.md5
	make $(PACKAGE)-$(VERSION).tar.bz2.md5

# generate md5 sum files
%.md5: %
	md5sum $< > $@

# check that no marshal or enumtypes files are included
# this in turn ensures that distcheck fails for missing .list files which is currently
# shadowed when the corresponding .c and .h files are included.
distcheck-hook:
	@test "x" = "x`find $(distdir) -name \*-enumtypes.[ch] | grep -v win32`" && \
	test "x" = "x`find $(distdir) -name \*-marshal.[ch]`" || \
	( $(ECHO) "*** Leftover enumtypes or marshal files in the tarball." && \
          $(ECHO) "*** Make sure the following files are not disted:" && \
          find $(distdir) -name \*-enumtypes.[ch] | grep -v win32 && \
          find $(distdir) -name \*-marshal.[ch] && \
          false )
