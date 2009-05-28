MAINTAINERCLEANFILES = Makefile.in

api:
	$(MONO) $(top_builddir)/parser/gst-gapi-parser.exe \
		gstreamer-sharp-source.xml
	grep -v "public const string " \
		$(top_srcdir)/gstreamer-sharp/Tag.custom \
		> $(top_srcdir)/gstreamer-sharp/Tag.custom.tmp
	mv $(top_srcdir)/gstreamer-sharp/Tag.custom.tmp \
		$(top_srcdir)/gstreamer-sharp/Tag.custom
	$(MONO) $(top_builddir)/parser/gst-generate-tags.exe \
		--header=../../gstreamer/gst/gsttaglist.h \
		--namespace=Gst \
		--class=Tags \
		--only-body \
		>> $(top_srcdir)/gstreamer-sharp/Tag.custom

