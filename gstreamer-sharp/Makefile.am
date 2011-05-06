SUBDIRS = . glue coreplugins baseplugins goodplugins badplugins

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
gapidir = $(datadir)/gapi-2.0
gapi_DATA = $(APIS)

glue_includes="gst/gst.h gst/interfaces/colorbalance.h gst/interfaces/colorbalancechannel.h gst/interfaces/tuner.h gst/interfaces/tunerchannel.h gst/interfaces/tunernorm.h gst/cdda/gstcddabasesrc.h"

CLEANFILES = $(ASSEMBLY) $(ASSEMBLY).mdb $(ASSEMBLY_NAME).pdb generated-stamp generated/*.cs glue/generated.c $(API) $(KEYFILE)
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
	Application.cs \
	Version.cs \
	AssemblyInfo.cs \
	GError.cs \
	Value.cs \
	PropertyInfo.cs \
	EnumInfo.cs \
	MiniObject.cs \
	Marshaller.cs \
	GstSharp.PadQueryTypeFunctionNative.cs \
	PadQueryTypeFunction.cs \
	TypeFindDelegates.cs \
	PresetDefault.cs \
	MissingPluginMessage.cs \
	MixerMessage.cs \
	NavigationMessage.cs \
	NavigationEvent.cs \
	NavigationQuery.cs \
	VideoEvent.cs \
	coreplugins/*.cs \
	glib-sharp/*.cs

overrides = \
	$(srcdir)/override/URIHandlerAdapter.cs \
	$(srcdir)/override/ColorBalanceAdapter.cs \
	$(srcdir)/override/Mixer.cs \
	$(srcdir)/override/MixerAdapter.cs \
	$(srcdir)/override/PropertyProbe.cs \
	$(srcdir)/override/PropertyProbeAdapter.cs \
	$(srcdir)/override/ProbeNeededHandler.cs \
	$(srcdir)/override/NavigationAdapter.cs

build_sources = $(addprefix $(srcdir)/, $(sources))
if USE_MONO_COMPILER
csc_build_sources = $(build_sources)
else
csc_build_sources = $(subst /,\\,$(build_sources)) 
endif

customs = 			\
	AppSink.custom		\
	AppSrc.custom		\
	Bin.custom		\
	Pipeline.custom		\
	Buffer.custom           \
	Bus.custom		\
	Caps.custom		\
	ColorBalanceChannel.custom		\
	Clock.custom		\
	ClockEntry.custom	\
	Debug.custom		\
	Element.custom		\
	ElementFactory.custom	\
	Message.custom		\
	Event.custom		\
	Pad.custom		\
	Parse.custom		\
	Object.custom		\
	PadTemplate.custom      \
	Plugin.custom     	\
	Task.custom     	\
	IndexEntry.custom       \
	Index.custom       \
	IndexFactory.custom       \
	Iterator.custom       \
	Registry.custom         \
	Query.custom 	        \
	Structure.custom        \
	Tag.custom		\
	TagList.custom		\
	TypeFind.custom		\
	TypeFindFactory.custom	\
	MixerTrack.custom	\
	TunerNorm.custom	\
	TunerChannel.custom	\
	Adapter.custom		\
	Controller.custom	\
	ControlSource.custom	\
	VideoUtil.custom	\
	BitReader.custom        \
	ByteReader.custom       \
	ByteWriter.custom

plugin_csfiles = $(builddir)/coreplugins/generated/*.cs \
	$(builddir)/baseplugins/generated/*.cs \
	$(builddir)/goodplugins/generated/*.cs \
	$(builddir)/badplugins/generated/*.cs

build_customs = $(addprefix $(srcdir)/, $(customs))

EXTRA_DIST =			\
	$(RAW_API)		\
	$(SYMBOLS)		\
	$(METADATA)		\
	$(customs)		\
	$(build_sources)	\
	$(overrides)		\
	$(ASSEMBLY).config.in

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA) $(srcdir)/$(SYMBOLS)
	cp $(srcdir)/$(RAW_API) $(builddir)/$(API)
	chmod u+w $(API)
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$(builddir)/$(API) \
		--metadata=$(srcdir)/$(METADATA) --symbols=$(srcdir)/$(SYMBOLS)

generated-stamp: $(API) $(build_customs) $(overrides)
	 $(MONO) $(top_builddir)/generator/gst-gapi_codegen.exe --generate $(builddir)/$(API) \
		--outdir=$(builddir)/generated --customdir=$(srcdir) --assembly-name=$(ASSEMBLY_NAME) \
		--gluelib-name=gstreamersharpglue-0.10.dll --glue-filename=$(builddir)/glue/generated.c \
		--glue-includes=$(glue_includes) \
	&& cp $(overrides) $(builddir)/generated \
	&& sed 's;public class ObjectManager;internal class ObjectManager;g' $(builddir)/generated/ObjectManager.cs > $(builddir)/generated/ObjectManager.cs.tmp \
	&& mv $(builddir)/generated/ObjectManager.cs.tmp $(builddir)/generated/ObjectManager.cs \
	&& touch generated-stamp

coreplugins/generated/*.cs: $(API) $(builddir)/coreplugins/generated
coreplugins/generated: $(API) $(srcdir)/coreplugins/*.metadata $(srcdir)/coreplugins/inspect/*.raw
	$(MAKE) -C coreplugins

baseplugins/generated/*.cs: $(API) $(builddir)/baseplugins/generated
baseplugins/generated: $(API) $(srcdir)/baseplugins/*.metadata $(srcdir)/baseplugins/inspect/*.raw
	$(MAKE) -C baseplugins

goodplugins/generated/*.cs: $(API) $(builddir)/goodplugins/generated
goodplugins/generated: $(API) $(srcdir)/goodplugins/inspect/*.raw
	$(MAKE) -C goodplugins

badplugins/generated/*.cs: $(API) $(builddir)/badplugins/generated
badplugins/generated: $(API) $(srcdir)/badplugins/inspect/*.raw
	$(MAKE) -C badplugins

$(KEYFILE): $(top_srcdir)/gstreamer-sharp.snk
	cp $(top_srcdir)/gstreamer-sharp.snk .

$(ASSEMBLY): $(build_sources) generated-stamp $(KEYFILE) $(plugin_csfiles)
	$(CSC) -nowarn:0612 -debug -unsafe -out:$(ASSEMBLY) -target:library $(references) $(csc_build_sources) $(GENERATED_SOURCES) $(plugin_csfiles) -define:GTK_SHARP_2_6 -define:GTK_SHARP_2_8 -define:GTK_SHARP_2_10 -define:GTK_SHARP_2_12 -define:GTK_SHARP_2_14

plugins-update:
	$(MAKE) -C coreplugins plugins-update
	$(MAKE) -C baseplugins plugins-update
	$(MAKE) -C goodplugins plugins-update
	$(MAKE) -C badplugins plugins-update

