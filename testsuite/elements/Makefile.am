include ../Rules

tests_pass = name tee property fake gst-compprep-check gst-inspect-check
tests_fail =

noinst_HEADERS = property.h


gst_inspect_check_SOURCES =
gst-inspect-check$(EXEEXT): $(srcdir)/gst-inspect-check.in
	sed s/@[G]ST_MAJORMINOR@/@GST_MAJORMINOR@/ \
		$(srcdir)/gst-inspect-check.in >gst-inspect-check$(EXEEXT)
	chmod +x gst-inspect-check$(EXEEXT)

gst_compprep_check_SOURCES =
gst-compprep-check$(EXEEXT):
	true	
	
