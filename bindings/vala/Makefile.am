
DEPS_FILES = gst-rtsp-server-0.10.deps
VAPI_FILES = gst-rtsp-server-0.10.vapi

gst-rtsp-server-0.10.deps:
	cp $(srcdir)/packages/gst-rtsp-server-0.10.deps $@
    
vapidir = $(VAPIDIR)
vapi_DATA = $(VAPI_FILES) $(DEPS_FILES)

EXTRA_DIST = packages $(VAPI_FILES)

