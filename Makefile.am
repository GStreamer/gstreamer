SUBDIRS = gst-libs ext
EXTRA_DIST=gst-ffmpeg.spec

# to do a release, run "make release"
# this will generate all archives and md5sums
                                                                                
# make bz2 as well
AUTOMAKE_OPTIONS = dist-bzip2
                                                                                
release: dist
	make @PACKAGE@-@VERSION@.tar.gz.md5
	make @PACKAGE@-@VERSION@.tar.bz2.md5
                                                                                
# generate md5 sum files
%.md5: %
	md5sum $< > $@

