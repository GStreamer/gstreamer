ELEMENTS = capsfilter fakesrc fakesink fdsrc fdsink filesrc filesink identity queue tee typefind multiqueue

NAMESPACE = Gst.CorePlugins
INSPECT_FILES = $(patsubst %,inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = 
METADATA_FILES = typefind.metadata \
		fakesrc.metadata \
		fakesink.metadata \
		identity.metadata
CS_FILES = $(patsubst %,%.cs,$(ELEMENTS))
OTHER_CS_FILES = memindex.cs \
	fileindex.cs
XML_FILES = $(patsubst %,%.xml,$(ELEMENTS))

EXTRA_DIST = $(CUSTOM_FILES) $(INSPECT_FILES) $(OTHER_CS_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)

plugins-update: $(patsubst inspect/%.raw, inspect-%, $(INSPECT_FILES))
inspect-%:
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect-%,%,$@) > inspect/$(patsubst inspect-%,%,$@).raw

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

