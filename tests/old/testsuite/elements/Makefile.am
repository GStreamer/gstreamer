include ../Rules

tests_pass = name tee property fake gst-compprep-check gst-inspect-check
tests_fail =

noinst_HEADERS = property.h


gst-inspect-check: $(srcdir)/gst-inspect-check.in
	sed s/@[G]ST_MAJORMINOR@/@GST_MAJORMINOR@/ \
		$(srcdir)/gst-inspect-check.in >gst-inspect-check
	chmod +x gst-inspect-check

gst-compprep-check$(EXEEXT):
	true	

