EXTRA_DIST=gstreamer.devhelp gstreamer-libs.devhelp

devhelp: gstreamer.devhelp gstreamer-libs.devhelp

gstreamer.devhelp:
	cd ../gst && make
	./html2xml.py ../gst/html
	sed 's/base=""/base="\/usr\/share\/doc\/gstreamer-devel-@VERSION@\/html\/gstreamer"/g' html.devhelp > gstreamer.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer"/' gstreamer.devhelp
	rm html.devhelp

gstreamer-libs.devhelp:
	cd ../gst && make
	./html2xml.py ../libs/html
	sed 's/base=""/base="\/usr\/share\/doc\/gstreamer-devel-@VERSION@\/html\/gstreamer-libs"/g' html.devhelp > gstreamer-libs.devhelp
	perl -i -p -e 's/name="html"/name="gstreamer-libs"/' gstreamer-libs.devhelp
	rm html.devhelp
