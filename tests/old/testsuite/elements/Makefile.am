include ../Rules

# disable gst-compprep-check until it doesn't leave stray files for distcheck
tests_pass = name tee property fake gst-inspect-check struct_size
tests_fail =
tests_ignore =

noinst_HEADERS = property.h struct_i386.h

gst_inspect_check_SOURCES =
gst-inspect-check$(EXEEXT): $(srcdir)/gst-inspect-check.in
	sed s/@[G]ST_MAJORMINOR@/@GST_MAJORMINOR@/ \
		$(srcdir)/gst-inspect-check.in >gst-inspect-check$(EXEEXT)
	chmod +x gst-inspect-check$(EXEEXT)

#gst_compprep_check_SOURCES =
#gst-compprep-check$(EXEEXT):
#	cp $(srcdir)/gst-compprep-check.in gst-compprep-check$(EXEEXT)
	
EXTRA_DIST = gst-inspect-check.in gst-compprep-check.in
