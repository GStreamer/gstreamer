ELEMENTS = decodebin playbin xvimagesink decodebin2 playbin2

NAMESPACE = Gst.BasePlugins
INSPECT_FILES = $(patsubst %,inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = playbin.custom
METADATA_FILES = decodebin.metadata playbin.metadata decodebin2.metadata playbin2.metadata
CS_FILES = $(patsubst %,%.cs,$(ELEMENTS))
XML_FILES = $(patsubst %,%.xml,$(ELEMENTS))

EXTRA_DIST = $(CUSTOM_FILES) $(INSPECT_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)

plugins-update: $(top_builddir)/elementgen/gsts-xmlinspect $(INSPECT_FILES)
inspect/%.raw: $(top_builddir)/elementgen/gsts-xmlinspect
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect/%.raw,%,$@) > $@

all: $(XML_FILES) $(CS_FILES)

%.cs: $(top_builddir)/elementgen/element-gen.exe $(CUSTOM_FILES) $(XML_FILES)
	$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=$(NAMESPACE) \
		--api=$(srcdir)/../gstreamer-api.xml --input=$(patsubst %.cs,%,$@) > $@

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(INSPECT_FILES) $(METADATA_FILES)
	cp $(patsubst %.xml,inspect/%.raw,$@) $@
	if [ -e $(patsubst %.xml,%.metadata,$@) ]; then \
		$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$@ \
			--metadata=$(patsubst %.xml,%.metadata,$@) ; \
	fi

