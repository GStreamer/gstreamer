RAW_API = $(ASSEMBLY_NAME)-api.raw
API = $(ASSEMBLY_NAME)-api.xml
METADATA = $(ASSEMBLY_NAME).metadata
DLL = ../out/$(ASSEMBLY_NAME).dll
DLLMAP = ../out/$(ASSEMBLY_NAME).dll.config
GLUEDIR = glue
KEYFILE = $(ASSEMBLY_NAME).snk

sources = \
	Adapter.cs \
	Application.cs \
	AppSink.cs \
	AppSrc.cs \
	AudioFilter.cs \
	Bin.cs \
	Buffer.cs \
	Bus.cs \
	Caps.cs \
	DeviceProvider.cs \
	DynamicSignal.cs \
	Element.cs \
	FFTF32.cs \
	Iterator.cs \
	MapInfo.cs \
	Message.cs \
	MiniObject.cs \
	NavigationAdapter.cs \
	Object.cs \
	Pad.cs \
	Pipeline.cs \
	TagList.cs \
	Value.cs \
	Version.cs \
	VideoGLUploadMeta.cs

build_sources = AssemblyInfo.cs $(addprefix $(srcdir)/custom/, $(sources))

CLEANFILES = $(DLL) generated-stamp generated/*/*.cs $(API) $(KEYFILE)

DISTCLEANFILES = AssemblyInfo.cs $(DLLMAP) $(DLL)

MAINTAINERCLEANFILES = \
	Makefile.in

noinst_DATA = $(DLL)
gapi_DATA = $(API)

EXTRA_DIST = \
	$(RAW_API) \
	$(addprefix $(srcdir)/custom/, $(sources)) \
	$(METADATA) \
	AssemblyInfo.cs.in \
	gstreamer-sharp-api.xml

all: generated-stamp $(ASSEMBLY)

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA)
	cp $(srcdir)/$(RAW_API) $(API)
	chmod u+w $(API)
	$(GAPI_FIXUP) --api=$(srcdir)/$(API) --metadata=$(srcdir)/$(METADATA)

generated-stamp: $(API)
	rm -rf generated/* && \
	$(GAPI_CODEGEN) --generate $(srcdir)/$(API) $(GLIB_SHARP_CFLAGS) $(GIO_SHARP_CFLAGS) \
		--outdir=generated \
		--glue-filename=$(GLUEDIR)/generated.c --gluelib-name=libgstreamersharpglue-1.0.0.dll \
		--glue-includes=gst/gst.h,`cd $(GST_INCLUDEDIR);find gst -type f -name "*.h" -not -path "gst/gl/*" | tr "\n" ","` \
	 	--assembly-name=$(ASSEMBLY_NAME) && touch generated-stamp

$(KEYFILE): $(top_srcdir)/gstreamer-sharp.snk
	cp $(top_srcdir)/gstreamer-sharp.snk .

$(DLL): $(build_sources) generated-stamp $(KEYFILE)
	test -d ../out || mkdir ../out
	$(CSC) -nowarn:169 -nowarn:108 -nowarn:114 -unsafe -target:library $(GLIB_SHARP_LIBS) $(GIO_SHARP_LIBS) \
		$(build_sources)  generated/*/*.cs -out:$(DLL)

install-data-local:
	echo "$(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS)";  \
        $(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS) || exit 1;

uninstall-local:
	echo "$(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS)"; \
        $(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS) || exit 1;
clean-local:
	-rm -r generated/*
