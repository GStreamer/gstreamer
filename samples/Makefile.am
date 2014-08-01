TARGETS = playback.exe video-overlay.exe

DEBUGS = $(addsuffix .mdb, $(TARGETS))
assemblies = 					\
	$(top_builddir)/out/gstreamer-sharp.dll

references=$(addprefix -r:, $(assemblies))

noinst_SCRIPTS = $(TARGETS)
CLEANFILES = $(TARGETS) $(DEBUGS)

playback.exe: $(srcdir)/Playback.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/Playback.cs

video-overlay.exe: $(srcdir)/VideoOverlay.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:video-overlay.exe $(references) $(GTK_SHARP_LIBS) $(srcdir)/VideoOverlay.cs


EXTRA_DIST = 				\
	Playback.cs \
    VideoOverlay.cs
