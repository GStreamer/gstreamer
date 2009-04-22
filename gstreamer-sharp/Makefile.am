SUBDIRS = . glue

TARGET = $(ASSEMBLY)
noinst_DATA = $(TARGET)
APIS = $(API)
API = gstreamer-api.xml
RAW_API = gstreamer-api.raw
METADATA = Gstreamer.metadata
SYMBOLS=gstreamer-symbols.xml
ASSEMBLY_NAME = gstreamer-sharp
references = $(GLIBSHARP_LIBS)
KEYFILE = gstreamer-sharp.snk

ASSEMBLY = $(ASSEMBLY_NAME).dll
gapidir = $(datadir)/gapi
gapi_DATA = $(APIS)

glue_includes="gst/gst.h"

CLEANFILES = $(ASSEMBLY) $(ASSEMBLY).mdb generated-stamp generated/*.cs glue/generated.c $(API) $(KEYFILE)
DISTCLEANFILES = $(ASSEMBLY).config AssemblyInfo.cs
MAINTAINERCLEANFILES = Makefile.in

install-data-local:
	@if test -n '$(TARGET)'; then                       \
	  echo "$(GACUTIL) /i $(ASSEMBLY) /f $(GACUTIL_FLAGS)";    \
	  $(GACUTIL) /i $(ASSEMBLY) /f $(GACUTIL_FLAGS) || exit 1; \
	fi

uninstall-local:
	@if test -n '$(TARGET)'; then                       \
	  echo "$(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS)";  \
	  $(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS) || exit 1;   \
	fi

clean-local:
	rm -rf generated

sources = \
	DynamicSignal.cs \
	BindingHelper.cs \
	Application.cs \
	Version.cs \
	AssemblyInfo.cs \
	GError.cs \
	Value.cs \
	plugins-base/PlayBin.cs \
	plugins-base/DecodeBin.cs \
	plugins-base/TypeFindElement.cs

build_sources = $(addprefix $(srcdir)/, $(sources))

customs = 			\
	Bin.custom		\
	Buffer.custom           \
	Bus.custom		\
	Caps.custom		\
	Clock.custom		\
	Debug.custom		\
	Element.custom		\
	Message.custom		\
	Pad.custom		\
	Parse.custom		\
	Object.custom		\
	PadTemplate.custom      \
	MiniObject.custom       \
	Registry.custom         \
	Query.custom 	        \
	Structure.custom        \
	Tag.custom		\
	TagList.custom

build_customs = $(addprefix $(srcdir)/, $(customs))

EXTRA_DIST =			\
	$(RAW_API)		\
	$(SYMBOLS)		\
	$(METADATA)		\
	$(customs)		\
	$(sources) \
	$(ASSEMBLY).config.in

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA) $(srcdir)/$(SYMBOLS)
	cp $(srcdir)/$(RAW_API) $(API)
	chmod u+w $(API)
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$(API) --metadata=$(srcdir)/$(METADATA) \
		--symbols=$(srcdir)/$(SYMBOLS)

generated-stamp: $(API) $(GAPI_CODEGEN) $(build_customs)
	$(GAPI_CODEGEN) --generate $(API) \
		--outdir=generated --customdir=$(srcdir) --assembly-name=$(ASSEMBLY_NAME) \
		--gluelib-name=gstreamersharpglue-0.10 --glue-filename=glue/generated.c \
		--glue-includes=$(glue_includes) \
	&& touch generated-stamp

$(KEYFILE): $(top_srcdir)/gstreamer-sharp.snk
	cp $(top_srcdir)/gstreamer-sharp.snk .

$(ASSEMBLY): $(build_sources) generated-stamp $(KEYFILE)
	$(CSC) /nowarn:0612 /debug /unsafe /out:$(ASSEMBLY) /target:library $(references) $(build_sources) $(GENERATED_SOURCES)

