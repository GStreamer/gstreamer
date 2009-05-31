ELEMENTS = decodebin playbin xvimagesink decodebin2 playbin2

NAMESPACE = Gst.BasePlugins
INSPECT_FILES = $(patsubst %,inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = playbin.custom
METADATA_FILES = decodebin.metadata playbin.metadata decodebin2.metadata playbin2.metadata
CS_FILES = $(patsubst %,generated/%.cs,$(ELEMENTS))
XML_FILES = $(patsubst %,%.xml,$(ELEMENTS))

EXTRA_DIST = $(CUSTOM_FILES) $(INSPECT_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)

plugins-update: $(patsubst inspect/%.raw, inspect-%, $(INSPECT_FILES))
inspect-%:
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect-%,%,$@) > inspect/$(patsubst inspect-%,%,$@).raw

all: $(XML_FILES) $(CS_FILES)

generated/%.cs: $(top_builddir)/elementgen/element-gen.exe $(CUSTOM_FILES) $(XML_FILES)
	mkdir -p generated
	$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=$(NAMESPACE) \
		--api=$(srcdir)/../gstreamer-api.xml --input=$(patsubst generated/%.cs,%,$@) > $@

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(INSPECT_FILES) $(METADATA_FILES)
	cp $(patsubst %.xml,inspect/%.raw,$@) $@
	if [ -e $(patsubst %.xml,%.metadata,$@) ]; then \
		$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$@ \
			--metadata=$(patsubst %.xml,%.metadata,$@) ; \
	fi

