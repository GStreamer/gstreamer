TARGETS = playback.exe video-overlay.exe basic-tutorial-1.exe basic-tutorial-2.exe basic-tutorial-3.exe basic-tutorial-4.exe basic-tutorial-5.exe basic-tutorial-6.exe basic-tutorial-7.exe basic-tutorial-8.exe basic-tutorial-9.exe basic-tutorial-12.exe basic-tutorial-13.exe playback-tutorial-1.exe playback-tutorial-2.exe playback-tutorial-3.exe playback-tutorial-4.exe playback-tutorial-5.exe playback-tutorial-6.exe

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

basic-tutorial-3.exe: $(srcdir)/BasicTutorial3.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-3.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial3.cs

basic-tutorial-4.exe: $(srcdir)/BasicTutorial4.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-4.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial4.cs

basic-tutorial-5.exe: $(srcdir)/BasicTutorial5.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-5.exe $(references) $(GTK_SHARP_LIBS) $(srcdir)/BasicTutorial5.cs

basic-tutorial-6.exe: $(srcdir)/BasicTutorial6.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-6.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial6.cs

basic-tutorial-7.exe: $(srcdir)/BasicTutorial7.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-7.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial7.cs

basic-tutorial-8.exe: $(srcdir)/BasicTutorial8.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-8.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial8.cs

basic-tutorial-9.exe: $(srcdir)/BasicTutorial9.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-9.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial9.cs

basic-tutorial-12.exe: $(srcdir)/BasicTutorial12.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-12.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial12.cs

basic-tutorial-13.exe: $(srcdir)/BasicTutorial13.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:basic-tutorial-13.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/BasicTutorial13.cs

playback-tutorial-1.exe: $(srcdir)/PlaybackTutorial1.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-1.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial1.cs

playback-tutorial-2.exe: $(srcdir)/PlaybackTutorial2.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-2.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial2.cs

playback-tutorial-3.exe: $(srcdir)/PlaybackTutorial3.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-3.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial3.cs

playback-tutorial-4.exe: $(srcdir)/PlaybackTutorial4.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-4.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial4.cs

playback-tutorial-5.exe: $(srcdir)/PlaybackTutorial5.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-5.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial5.cs

playback-tutorial-6.exe: $(srcdir)/PlaybackTutorial6.cs $(assemblies)
	$(CSC) $(CSFLAGS) -out:playback-tutorial-6.exe $(references) $(GLIB_SHARP_LIBS) $(srcdir)/PlaybackTutorial6.cs

EXTRA_DIST = 				\
	Playback.cs \
    VideoOverlay.cs \
    BasicTutorial1.cs \
    BasicTutorial2.cs \
    BasicTutorial3.cs \
    BasicTutorial4.cs \
    BasicTutorial5.cs \
    BasicTutorial6.cs \
    BasicTutorial7.cs \
    BasicTutorial8.cs \
    BasicTutorial9.cs \
    BasicTutorial12.cs \
    BasicTutorial13.cs \
    PlaybackTutorial1.cs \
    PlaybackTutorial2.cs \
    PlaybackTutorial3.cs \
    PlaybackTutorial4.cs \
    PlaybackTutorial5.cs \
    PlaybackTutorial6.cs
