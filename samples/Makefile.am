TARGETS =

if HAVE_GLIB_SHARP
TARGETS += playbin-player.exe decodebin-transcoder.exe helloworld.exe typefind.exe metadata.exe
if HAVE_GTK_SHARP
TARGETS += gtk-video-player.exe
endif
if HAVE_MONO_CAIRO
TARGETS += appsrc.exe transform-sample.exe
endif
endif
DEBUGS = $(addsuffix .mdb, $(TARGETS))

all: $(TARGETS) link

assemblies=$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll
references=$(addprefix -r:, $(assemblies)) $(GLIB_SHARP_LIBS)

playbin-player.exe: $(srcdir)/PlayBinPlayer.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/PlayBinPlayer.cs

decodebin-transcoder.exe: $(srcdir)/DecodeBinTranscoder.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/DecodeBinTranscoder.cs 

helloworld.exe: $(srcdir)/HelloWorld.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/HelloWorld.cs

typefind.exe: $(srcdir)/TypeFind.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/TypeFind.cs

queueexample.exe: $(srcdir)/QueueExample.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/QueueExample.cs

metadata.exe: $(srcdir)/MetaData.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/MetaData.cs

mp3launchparse.exe: $(srcdir)/MP3LaunchParse.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/MP3LaunchParse.cs

appsrc.exe: $(srcdir)/AppSrc.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(MONO_CAIRO_LIBS) $(srcdir)/AppSrc.cs

transform-sample.exe: $(srcdir)/TransformSample.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(MONO_CAIRO_LIBS) $(srcdir)/TransformSample.cs

gtk-video-player.exe: $(srcdir)/GtkVideoPlayer.cs $(assemblies)
	$(CSC) -debug -out:$@ $(GLIBSHARP_LIBS) $(references) $(GTK_SHARP_LIBS) $(srcdir)/GtkVideoPlayer.cs

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
	TypeFind.cs \
	MetaData.cs \
	AppSrc.cs \
	GtkVideoPlayer.cs \
	TransformSample.cs

