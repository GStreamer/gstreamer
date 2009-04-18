MAINTAINERCLEANFILES = Makefile.in

api:
	$(GAPI_PARSER) gstreamer-sharp-source.xml
	$(MONO) $(top_builddir)/parser/gst-generate-tags.exe \
		--header=../../gstreamer/gst/gsttaglist.h \
		--namespace=Gst \
		--class=Tags \
		--only-body \
		> $(top_srcdir)/gstreamer-sharp/Tag.custom

