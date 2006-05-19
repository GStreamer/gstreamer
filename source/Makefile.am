EXTRA_DIST =				\
	gstreamer-parsable.diff

MAINTAINERCLEANFILES = Makefile.in

get-source-code:
	wget http://gstreamer.freedesktop.org/src/gstreamer/gstreamer-$(GSTREAMER_REQUIRED_VERSION).tar.gz --output-document=- | tar -xz
	patch -p0 < gstreamer-parsable.diff

api:
	$(GAPI_PARSER) gstreamer-sharp-source.xml


