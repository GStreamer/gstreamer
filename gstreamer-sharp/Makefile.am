SUBDIRS = . glue coreplugins baseplugins

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

glue_includes="gst/gst.h gst/interfaces/colorbalance.h gst/interfaces/colorbalancechannel.h gst/interfaces/tuner.h gst/interfaces/tunerchannel.h gst/interfaces/tunernorm.h"

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
	BindingHelper.cs \
	Application.cs \
	Version.cs \
	AssemblyInfo.cs \
	GError.cs \
	Value.cs \
	PropertyInfo.cs \
	EnumInfo.cs \
	Iterator.cs \
	MiniObject.cs \
	GstSharp.PadQueryTypeFunctionNative.cs \
	PadQueryTypeFunction.cs \
	TypeFindDelegates.cs \
	PresetDefault.cs \
	MixerMessage.cs \
	NavigationMessage.cs \
	NavigationEvent.cs \
	NavigationQuery.cs \
	coreplugins/*.cs \
	baseplugins/*.cs

overrides = \
	override/URIHandlerAdapter.cs \
	override/ColorBalanceAdapter.cs \
	override/ColorBalance.cs  \
	override/Mixer.cs \
	override/MixerAdapter.cs \
	override/PropertyProbe.cs \
	override/PropertyProbeAdapter.cs \
	override/ProbeNeededHandler.cs \
	override/NavigationAdapter.cs

build_sources = $(addprefix $(srcdir)/, $(sources))
if USE_MONO_COMPILER
csc_build_sources = $(build_sources)
else
csc_build_sources = $(subst /,\\,$(build_sources)) 
endif

customs = 			\
	Bin.custom		\
	Buffer.custom           \
	Bus.custom		\
	Caps.custom		\
	ColorBalanceChannel.custom		\
	Clock.custom		\
	ClockEntry.custom	\
	Debug.custom		\
	Element.custom		\
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
	Registry.custom         \
	Query.custom 	        \
	Structure.custom        \
	Tag.custom		\
	TagList.custom		\
	TypeFind.custom		\
	TypeFindFactory.custom	\
	MixerTrack.custom	\
	TunerNorm.custom	\
	TunerChannel.custom

build_customs = $(addprefix $(srcdir)/, $(customs))

EXTRA_DIST =			\
	$(RAW_API)		\
	$(SYMBOLS)		\
	$(METADATA)		\
	$(customs)		\
	$(sources)		\
	$(overrides)		\
	$(ASSEMBLY).config.in

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA) $(srcdir)/$(SYMBOLS)
	cp $(srcdir)/$(RAW_API) $(API)
	chmod u+w $(API)
	$(MONO) $(top_builddir)/parser/gst-gapi-fixup.exe --api=$(API) --metadata=$(srcdir)/$(METADATA) \
		--symbols=$(srcdir)/$(SYMBOLS)

generated-stamp: $(API) $(build_customs) $(overrides)
	 $(MONO) $(top_builddir)/generator/gst-gapi_codegen.exe --generate $(API) \
		--outdir=generated --customdir=$(srcdir) --assembly-name=$(ASSEMBLY_NAME) \
		--gluelib-name=gstreamersharpglue-0.10.dll --glue-filename=glue/generated.c \
		--glue-includes=$(glue_includes) \
	&& cp $(overrides) $(builddir)/generated \
	&& sed 's;public class ObjectManager;internal class ObjectManager;g' generated/ObjectManager.cs > generated/ObjectManager.cs.tmp \
	&& mv generated/ObjectManager.cs.tmp generated/ObjectManager.cs \
	&& touch generated-stamp

coreplugins/*.cs: $(API) coreplugins/*.metadata coreplugins/inspect/*.raw
	$(MAKE) -C coreplugins

baseplugins/*.cs: $(API) baseplugins/*.metadata baseplugins/inspect/*.raw
	$(MAKE) -C baseplugins

$(KEYFILE): $(top_srcdir)/gstreamer-sharp.snk
	cp $(top_srcdir)/gstreamer-sharp.snk .

$(ASSEMBLY): $(build_sources) generated-stamp $(KEYFILE)
	$(CSC) -nowarn:0612 -debug -unsafe -out:$(ASSEMBLY) -target:library $(references) $(csc_build_sources) $(GENERATED_SOURCES)

plugins-update:
	$(MAKE) -C coreplugins plugins-update
	$(MAKE) -C baseplugins plugins-update

