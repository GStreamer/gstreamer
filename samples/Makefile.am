TARGETS = playback.exe video-overlay.exe basic-tutorial-1.exe basic-tutorial-2.exe

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

basic-tutorial-1.exe: $(srcdir)/BasicTutorial1.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-1.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial1.cs

basic-tutorial-2.exe: $(srcdir)/BasicTutorial2.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-2.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial2.cs

EXTRA_DIST = 				\
	Playback.cs \
    VideoOverlay.cs \
    BasicTutorial1.cs \
    BasicTutorial2.cs
