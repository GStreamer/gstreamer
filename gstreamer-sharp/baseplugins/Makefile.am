ELEMENTS = decodebin playbin xvimagesink decodebin2 playbin2

NAMESPACE = Gst.BasePlugins
INSPECT_FILES = $(patsubst %,inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = playbin.custom
METADATA_FILES = decodebin.metadata playbin.metadata decodebin2.metadata playbin2.metadata
CS_FILES = $(patsubst %,generated/%.cs,$(ELEMENTS))
XML_FILES = $(patsubst %,%.xml,$(ELEMENTS))

EXTRA_DIST = $(METADATA_FILES) $(CUSTOM_FILES) $(INSPECT_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)

plugins-update: $(patsubst inspect/%.raw, inspect-%, $(INSPECT_FILES))
inspect-%:
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect-%,%,$@) > inspect/$(patsubst inspect-%,%,$@).raw

all: $(XML_FILES) $(CS_FILES)
	touch generated

generated/%.cs: $(top_builddir)/elementgen/element-gen.exe $(srcdir)/../gstreamer-api.xml $(CUSTOM_FILES) %.xml
	@mkdir -p generated
	$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=$(NAMESPACE) \
		--api=$(srcdir)/../gstreamer-api.xml --input=$(patsubst generated/%.cs,%,$@) > $@

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe inspect/%.raw %.metadata
	cp $(patsubst %.xml,inspect/%.raw,$@) $@
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$@ --metadata=$(patsubst %.xml,%.metadata,$@)

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe inspect/%.raw
	cp $(patsubst %.xml,inspect/%.raw,$@) $@

