ELEMENTS = capsfilter fakesrc fakesink fdsrc fdsink filesrc filesink identity queue queue2 tee typefind multiqueue

NAMESPACE = Gst.CorePlugins
INSPECT_FILES = $(patsubst %,$(srcdir)/inspect/%.raw,$(ELEMENTS))
CUSTOM_FILES = 
METADATA_FILES = $(srcdir)/typefind.metadata \
		$(srcdir)/fakesrc.metadata \
		$(srcdir)/fakesink.metadata \
		$(srcdir)/identity.metadata
CS_FILES = $(patsubst %,$(builddir)/generated/%.cs,$(ELEMENTS))
OTHER_CS_FILES = $(srcdir)/memindex.cs \
	$(srcdir)/fileindex.cs
XML_FILES = $(patsubst %,$(builddir)/%.xml,$(ELEMENTS))

EXTRA_DIST = $(METADATA_FILES) $(CUSTOM_FILES) $(INSPECT_FILES) $(OTHER_CS_FILES)
CLEANFILES = $(XML_FILES) $(CS_FILES)
MAINTAINERCLEANFILES = Makefile.in

plugins-update: $(patsubst $(srcdir)/inspect/%.raw, inspect-%, $(INSPECT_FILES))
inspect-%:
	$(top_builddir)/elementgen/gsts-xmlinspect $(patsubst inspect-%,%,$@) > inspect/$(patsubst inspect-%,%,$@).raw.tmp && mv inspect/$(patsubst inspect-%,%,$@).raw.tmp inspect/$(patsubst inspect-%,%,$@).raw || (rm inspect/$(patsubst inspect-%,%,$@).raw.tmp && true)

all: $(XML_FILES) $(CS_FILES)
	touch generated

generated/%.cs: $(top_builddir)/elementgen/element-gen.exe $(top_builddir)/gstreamer-sharp/gstreamer-api.xml $(CUSTOM_FILES) $(builddir)/%.xml
	@mkdir -p generated
	$(MONO) $(top_builddir)/elementgen/element-gen.exe --namespace=$(NAMESPACE) \
		--api=$(top_builddir)/gstreamer-sharp/gstreamer-api.xml --input=$(patsubst generated/%.cs,$(builddir)/%.xml,$@) --customfile=$(patsubst generated/%.cs,$(srcdir)/%.custom,$@) --interfacesdir=$(top_srcdir)/elementgen/interfaces > $@

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(srcdir)/inspect/%.raw $(srcdir)/%.metadata
	cp $(patsubst %.xml,$(srcdir)/inspect/%.raw,$@) $(builddir)/$@
	chmod +w $(builddir)/$@
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$(builddir)/$@ --metadata=$(patsubst %.xml,$(srcdir)/%.metadata,$@)

%.xml: $(top_builddir)/parser/gst-gapi-fixup.exe $(srcdir)/inspect/%.raw 
	cp $(patsubst %.xml,$(srcdir)/inspect/%.raw,$@) $(builddir)/$@
