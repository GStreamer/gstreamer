MAINTAINERCLEANFILES = Makefile.in

BASE_PATH="http://gstreamer.freedesktop.org/src"

update-source:
	wget -O - $(BASE_PATH)/gstreamer/gstreamer-$(GSTREAMER_REQUIRED_VERSION).tar.gz | tar xz
	wget -O - $(BASE_PATH)/gst-plugins-base/gst-plugins-base-$(GSTREAMER_REQUIRED_VERSION).tar.gz | tar xz

api:
	$(GAPI_PARSER) gstreamer-sharp-source.xml


