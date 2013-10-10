RAW_API = $(ASSEMBLY_NAME)-api.raw
API = $(ASSEMBLY_NAME)-api.xml
METADATA = $(ASSEMBLY_NAME).metadata
DLL = $(top_srcdir)/out/$(ASSEMBLY_NAME).dll
DLLMAP = $(top_srcdir)/out/$(ASSEMBLY_NAME).dll.config
GLUEDIR = $(srcdir)/glue

sources = custom/*.cs

build_sources = AssemblyInfo.cs $(sources)

CLEANFILES = $(DLL) generated-stamp generated/*.cs $(API)

DISTCLEANFILES = AssemblyInfo.cs $(DLLMAP)

noinst_DATA = $(DLL)

EXTRA_DIST = \
	$(RAW_API) \
	$(sources) \
	$(METADATA) \
	AssemblyInfo.cs.in \
	$(ASSEMBLY_NAME).snk

all: generated-stamp $(ASSEMBLY)

$(API): $(srcdir)/$(RAW_API) $(srcdir)/$(METADATA)
	cp $(srcdir)/$(RAW_API) $(API)
	chmod u+w $(API)
	$(GAPI_FIXUP) --api=$(srcdir)/$(API) --metadata=$(srcdir)/$(METADATA)

generated-stamp: $(API)
	 $(GAPI_CODEGEN) --generate $(srcdir)/$(API) $(GLIB_SHARP_CFLAGS) \
		--outdir=generated \
		--glue-filename=$(GLUEDIR)/generated.c --gluelib-name=libgstreamersharpglue-1.0.6.so \
		--glue-includes=gst/gst.h,gst/base/gstadapter.h,gst/base/gstbaseparse.h,gst/base/gstbasesink.h,gst/base/gstbasesrc.h,gst/base/gstbasetransform.h,gst/base/gstbitreader.h,gst/base/gstbytereader.h,gst/base/gstbytewriter.h,gst/base/gstcollectpads.h,gst/base/gstpushsrc.h,gst/base/gsttypefindhelper.h,gst/video/colorbalance.h,gst/video/colorbalancechannel.h,gst/video/gstvideodecoder.h,gst/video/gstvideoencoder.h,gst/video/gstvideofilter.h,gst/video/gstvideometa.h,gst/video/gstvideopool.h,gst/video/gstvideosink.h,gst/video/gstvideoutils.h,gst/video/navigation.h,gst/video/video-blend.h,gst/video/video-color.h,gst/video/video-enumtypes.h,gst/video/video-event.h,gst/video/video-format.h,gst/video/video-frame.h,gst/video/video-info.h,gst/video/video-overlay-composition.h,gst/video/video.h,gst/video/videoorientation.h,gst/video/videooverlay.h,gst/audio/audio-channels.h,gst/audio/audio-enumtypes.h,gst/audio/audio-format.h,gst/audio/audio-info.h,gst/audio/audio.h,gst/audio/gstaudiobasesink.h,gst/audio/gstaudiobasesrc.h,gst/audio/gstaudiocdsrc.h,gst/audio/gstaudioclock.h,gst/audio/gstaudiodecoder.h,gst/audio/gstaudioencoder.h,gst/audio/gstaudiofilter.h,gst/audio/gstaudioiec61937.h,gst/audio/gstaudiometa.h,gst/audio/gstaudioringbuffer.h,gst/audio/gstaudiosink.h,gst/audio/gstaudiosrc.h,gst/audio/streamvolume.h,gst/pbutils/codec-utils.h,gst/pbutils/descriptions.h,gst/pbutils/encoding-profile.h,gst/pbutils/encoding-target.h,gst/pbutils/gstdiscoverer.h,gst/pbutils/gstpluginsbaseversion.h,gst/pbutils/install-plugins.h,gst/pbutils/missing-plugins.h,gst/pbutils/pbutils-enumtypes.h,gst/pbutils/pbutils.h \
	 	--assembly-name=$(ASSEMBLY_NAME) && touch generated-stamp

$(DLL): $(build_sources) generated-stamp
	$(CSC) -nowarn:169 -unsafe -target:library $(GLIB_SHARP_LIBS) \
		$(build_sources)  generated/*.cs -out:$(DLL)

install-data-local:
	echo "$(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS)";  \
        $(GACUTIL) /i $(DLL) /f $(GACUTIL_FLAGS) || exit 1;

uninstall-local:
	echo "$(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS)"; \
        $(GACUTIL) /u $(ASSEMBLY_NAME) $(GACUTIL_FLAGS) || exit 1;
