RAW_API = $(ASSEMBLY_NAME)-api.raw
API = $(ASSEMBLY_NAME)-api.xml
METADATA = $(ASSEMBLY_NAME).metadata
DLL = $(top_srcdir)/out/$(ASSEMBLY_NAME).dll
DLLMAP = $(top_srcdir)/out/$(ASSEMBLY_NAME).dll.config
GLUEDIR = $(srcdir)/glue

sources = custom/*.cs

build_sources = AssemblyInfo.cs $(sources)

CLEANFILES = $(DLL) generated-stamp generated/*/*.cs $(API)

DISTCLEANFILES = AssemblyInfo.cs $(DLLMAP)

noinst_DATA = $(DLL)

EXTRA_DIST = \
	$(RAW_API) \
	$(sources) \
	$(METADATA) \
	AssemblyInfo.cs.in \
	$(ASSEMBLY_NAME).snk \
	gstreamer-sharp-api.xml

all: generated-stamp $(ASSEMBLY)

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA)
	cp $(srcdir)/$(RAW_API) $(API)
	chmod u+w $(API)
	$(GAPI_FIXUP) --api=$(srcdir)/$(API) --metadata=$(srcdir)/$(METADATA)

generated-stamp: $(API)
	rm -rf generated/* && \
	$(GAPI_CODEGEN) --generate $(srcdir)/$(API) $(GLIB_SHARP_CFLAGS) \
		--outdir=generated \
		--glue-filename=$(GLUEDIR)/generated.c --gluelib-name=libgstreamersharpglue-1.0.0.dll \
		--glue-includes=gst/gst.h,`cd $(GST_INCLUDEDIR);find gst -type f -name "*.h" -not -path "gst/gl/*" | tr "\n" ","` \
	 	--assembly-name=$(ASSEMBLY_NAME) && touch generated-stamp

$(DLL): $(build_sources) generated-stamp
	$(CSC) -nowarn:169 -nowarn:108 -nowarn:114 -unsafe -target:library $(GLIB_SHARP_LIBS) \
		$(build_sources)  generated/*/*.cs -out:$(DLL)

install-data-local:
	echo "$(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS)";  \
        $(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS) || exit 1; \
	cp $(API) $(gapidir);

uninstall-local:
	echo "$(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS)"; \
        $(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS) || exit 1;
	rm -f $(gapidir)/$(API)
clean-local:
	-rm -r generated/*
