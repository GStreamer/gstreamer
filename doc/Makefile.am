ASSEMBLIES = \
	$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll

ASSEMBLIES_BUILD = $(foreach asm,$(ASSEMBLIES),$(addprefix -assembly:,$(asm)))

if ENABLE_MONODOC
SOURCESDIR=$(prefix)/lib/monodoc/sources
TARGETS=gstreamer-sharp-docs.zip gstreamer-sharp-docs.tree gstreamer-sharp-docs.source
else
SOURCESDIR=$(datadir)
TARGETS=
endif

monodocdir=$(SOURCESDIR)
monodoc_DATA=$(TARGETS)

gstreamer-sharp-docs.tree: gstreamer-sharp-docs.zip

gstreamer-sharp-docs.zip: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(MDASSEMBLER) --ecma $(srcdir)/en -o gstreamer-sharp-docs

html: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(MONODOCS2HTML) -source:$(srcdir)/en -dest:html

update: $(ASSEMBLIES)
	LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:$(top_builddir)/gstreamer-sharp/glue/.libs \
	$(MONODOCER) $(ASSEMBLIES_BUILD) -path:en -updateto:en -delete

CLEANFILES = gstreamer-sharp-docs.zip gstreamer-sharp-docs.tree
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = gstreamer-sharp-docs.source

dist-hook:
	mkdir -p $(distdir)/en
	cp $(srcdir)/en/*.xml $(distdir)/en/
	mkdir -p $(distdir)/en/Gst/
	cp $(srcdir)/en/Gst/*.xml $(distdir)/en/Gst/

distclean-local:
	rm -rf html
