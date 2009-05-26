ELEMENTS = decodebin playbin xvimagesink

EXTRA_DIST = *.custom inspect/*.raw
CLEANFILES = $(srcdir)/*.xml $(srcdir)/*.cs element-bindings

plugins-update: $(top_builddir)/elementgen/gsts-xmlinspect
	for element in $(ELEMENTS); do \
		$(top_builddir)/elementgen/gsts-xmlinspect $$element > inspect/$$element.raw; \
	done

all: element-bindings

element-bindings: $(srcdir)/../gstreamer-api.xml $(srcdir)/inspect/*.raw
	for element in $(ELEMENTS); do \
		cp inspect/$$element.raw $$element.xml ; \
		if [ -e $$element.metadata ]; then \
			$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$$element.xml \
				--metadata=$$element.metadata ; \
		fi ; \
		$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=Gst.BasePlugins \
			--api=$(srcdir)/../gstreamer-api.xml --input=$$element > $$element.cs ; \
	done
	touch element-bindings

