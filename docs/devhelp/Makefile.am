EXTRA_DIST=html2xml.py

all-am: chmod devhelp

devhelp: gstreamer.devhelp gstreamer-libs.devhelp

chmod:
	chmod +x html2xml.py

gstreamer.devhelp:
	cd ../gst && make
	./html2xml.py ../gst/html
	sed 's@base=""@base="@HTML_DIR@/gstreamer"@g' html.devhelp > gstreamer.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer"/' gstreamer.devhelp
	rm html.devhelp

gstreamer-libs.devhelp:
	cd ../libs && make
	./html2xml.py ../libs/html
	sed 's@base=""@base="@HTML_DIR@/gstreamer-libs"@g' html.devhelp > gstreamer-libs.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer-libs"/' gstreamer-libs.devhelp
	rm html.devhelp
