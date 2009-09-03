ELEMENTS = decodebin playbin xvimagesink decodebin2 playbin2

NAMESPACE = Gst.BasePlugins
INSPECT_FILES = $(patsubst %,$(srcdir)/inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = $(srcdir)/playbin.custom
METADATA_FILES = $(srcdir)/decodebin.metadata $(srcdir)/playbin.metadata $(srcdir)/decodebin2.metadata $(srcdir)/playbin2.metadata
CS_FILES = $(patsubst %,$(builddir)/generated/%.cs,$(ELEMENTS))
XML_FILES = $(patsubst %,$(builddir)/%.xml,$(ELEMENTS))

EXTRA_DIST = $(METADATA_FILES) $(CUSTOM_FILES) $(INSPECT_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)

plugins-update: $(patsubst inspect/%.raw, inspect-%, $(INSPECT_FILES))
inspect-%:
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect-%,%,$@) > inspect/$(patsubst inspect-%,%,$@).raw

all: $(XML_FILES) $(CS_FILES)
	touch generated

generated/%.cs: $(top_builddir)/elementgen/element-gen.exe $(srcdir)/../gstreamer-api.xml $(CUSTOM_FILES) $(builddir)/%.xml
	@mkdir -p generated
	$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=$(NAMESPACE) \
		--api=$(srcdir)/../gstreamer-api.xml --input=$(patsubst generated/%.cs,$(builddir)/%.xml,$@) --customfile=$(patsubst generated/%.cs,$(srcdir)/%.custom,$@) > $@

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(srcdir)/inspect/%.raw $(srcdir)/%.metadata
	cp $(patsubst %.xml,$(srcdir)/inspect/%.raw,$@) $@
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$@ --metadata=$(patsubst %.xml,$(srcdir)/%.metadata,$@)

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(srcdir)/inspect/%.raw
	cp $(patsubst %.xml,$(srcdir)/inspect/%.raw,$@) $@

