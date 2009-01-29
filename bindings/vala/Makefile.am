
VAPI_FILES = gst-rtsp-server-0.10.deps
DEPS_FILES = gst-rtsp-server-0.10.vapi

gst-rtsp-server-0.10.deps:
	cp packages/gst-rtsp-server-0.10.deps $@
    
vapidir = $(VAPIDIR)
vapi_DATA = $(VAPI_FILES) $(DEPS_FILES)

EXTRA_DIST = packages $(DEPS_FILES)

