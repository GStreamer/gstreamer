if ENABLE_MONODOC
TARGETS = gstreamer-sharp-docs.zip gstreamer-sharp-docs.tree gstreamer-sharp-docs.source

ASSEMBLIES = \
	$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll

ASSEMBLIES_BUILD = $(foreach asm,$(ASSEMBLIES),$(addprefix -assembly:,$(asm)))

install-data-local:
	mkdir -p $(DESTDIR)/$(MONODOC_SOURCESDIR)
	install -m 644 $(srcdir)/gstreamer-sharp-docs.source \
		$(DESTDIR)/$(MONODOC_SOURCESDIR)
	install -m 644 gstreamer-sharp-docs.tree \
		$(DESTDIR)/$(MONODOC_SOURCESDIR)
	install -m 644 gstreamer-sharp-docs.zip \
		$(DESTDIR)/$(MONODOC_SOURCESDIR)

uninstall-local:
	rm -f $(DESTDIR)/$(MONODOC_SOURCESDIR)/gstreamer-sharp-docs.source
	rm -f $(DESTDIR)/$(MONODOC_SOURCESDIR)/gstreamer-sharp-docs.tree
	rm -f $(DESTDIR)/$(MONODOC_SOURCESDIR)/gstreamer-sharp-docs.zip
else
TARGETS = 
endif

noinst_DATA = $(TARGETS)

gstreamer-sharp-docs.zip gstreamer-sharp-docs.tree: $(srcdir)/en/*/*.xml $(srcdir)/en/*.xml
	$(MDASSEMBLER) --ecma $(srcdir)/en -o gstreamer-sharp-docs

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
