if ENABLE_MONODOC
TARGETS = $(ASSEMBLY_NAME)-docs.source $(ASSEMBLY_NAME)-docs.zip $(ASSEMBLY_NAME)-docs.tree
docsdir = $(prefix)/lib/monodoc/sources
docs_DATA = $(TARGETS)
else
TARGETS = 
docsdir = $(datadir)
docs_DATA =
endif

ASSEMBLIES = $(top_srcdir)/out/$(ASSEMBLY_NAME).dll

UPDATE_ASSEMBLIES = $(addprefix -assembly:, $(ASSEMBLIES))

all: $(TARGETS)

update:
	$(MONODOCER) $(UPDATE_ASSEMBLIES) -path:en

html: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(MONODOCS2HTML) -source:$(srcdir)/en -dest:html

$(srcdir)/en/*/*.xml $(srcdir)/en/*.xml:
	$(MONODOCER) $(UPDATE_ASSEMBLIES) -path:en

$(ASSEMBLY_NAME)-docs.source:
	@echo "Generating $@"
	@echo "<?xml version=\"1.0\"?>" > $@
	@echo "<monodoc>" >> $@
	@echo "  <source provider=\"ecma\" basefile=\"$(ASSEMBLY_NAME)-docs\" path=\"classlib-$(ASSEMBLY_SHORTNAME)\"/>" >> $@
	@echo "</monodoc>" >> $@

$(ASSEMBLY_NAME)-docs.zip $(ASSEMBLY_NAME)-docs.tree: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(MDASSEMBLER) --ecma $(srcdir)/en -o $(ASSEMBLY_NAME)-docs

CLEANFILES = $(TARGETS)

NAMESPACES=$(ASSEMBLY_NAMESPACE) Gst.App Gst.Audio Gst.Base Gst.Controller Gst.FFT Gst.Net Gst.PbUtils Gst.Riff Gst.Rtp Gst.Rtsp Gst.Sdp Gst.Tags Gst.Video GtkSharp.GstreamerSharp

dist-hook:
	mkdir -p $(distdir)/en
	cp $(srcdir)/en/*.xml $(distdir)/en/
	for i in $(NAMESPACES); do                          \
		mkdir -p $(distdir)/en/$$i;                     \
		cp $(srcdir)/en/$$i/*.xml $(distdir)/en/$$i;    \
	done
