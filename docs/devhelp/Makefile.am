EXTRA_DIST = html2xml.py

all-am: devhelp

devhelp: gstreamer-@GST_MAJORMINOR@.devhelp gstreamer-libs-@GST_MAJORMINOR@.devhelp

gstreamer-@GST_MAJORMINOR@.devhelp:
	cd ../gst && make
	$(PYTHON) ./html2xml.py ../gst/html
	sed 's@base=""@base="@HTML_DIR@/gstreamer-@GST_MAJORMINOR@"@g' html.devhelp > gstreamer-@GST_MAJORMINOR@.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer-@GST_MAJORMINOR@"/' gstreamer-@GST_MAJORMINOR@.devhelp
	rm html.devhelp

gstreamer-libs-@GST_MAJORMINOR@.devhelp:
	cd ../libs && make
	$(PYTHON) ./html2xml.py ../libs/html
	sed 's@base=""@base="@HTML_DIR@/gstreamer-libs-@GST_MAJORMINOR@"@g' html.devhelp > gstreamer-libs-@GST_MAJORMINOR@.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer-libs-@GST_MAJORMINOR@"/' gstreamer-libs-@GST_MAJORMINOR@.devhelp
	rm html.devhelp
