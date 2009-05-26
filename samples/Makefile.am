if USE_MONO_COMPILER
MCS_TARGETS = gtk-video-player.exe
endif
TARGETS = playbin-player.exe decodebin-transcoder.exe helloworld.exe typefind.exe metadata.exe $(MCS_TARGETS)
DEBUGS = $(addsuffix .mdb, $(TARGETS))

all: $(TARGETS) link

assemblies=$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll
references=$(addprefix -r:, $(assemblies))

playbin-player.exe: $(srcdir)/PlayBinPlayer.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/PlayBinPlayer.cs

decodebin-transcoder.exe: $(srcdir)/DecodeBinTranscoder.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/DecodeBinTranscoder.cs 

helloworld.exe: $(srcdir)/HelloWorld.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/HelloWorld.cs

typefind.exe: $(srcdir)/TypeFind.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/TypeFind.cs

queueexample.exe: $(srcdir)/QueueExample.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/QueueExample.cs

metadata.exe: $(srcdir)/MetaData.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/MetaData.cs

mp3launchparse.exe: $(srcdir)/MP3LaunchParse.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/MP3LaunchParse.cs

gtk-video-player.exe: $(srcdir)/GtkVideoPlayer.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) -pkg:gtk-sharp-2.0 $(srcdir)/GtkVideoPlayer.cs

link:
	ln -sf $(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll gstreamer-sharp.dll
	ln -sf $(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll.config gstreamer-sharp.dll.config

noinst_SCRIPTS = $(TARGETS)

CLEANFILES = $(TARGETS) $(DEBUGS) gstreamer-sharp.dll*
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	PlayBinPlayer.cs \
	DecodeBinTranscoder.cs \
	HelloWorld.cs \
	TypeFind.cs
