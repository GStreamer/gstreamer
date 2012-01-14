GStreamer build for Visual Studio 10

Dependencies
============

GLib
Flex/Bison for Windows ( http://sourceforge.net/projects/winflexbison/ )

Sample Directory layout
=======================

<root>
	build					 (contains source for each project)
		glib
		gstreamer
		gst-plugins-base
		gst-plugins-good
		gst-plugins-ugly
		gst-plugins-bad
		gst-ffmpeg
	vs10					(contains built products)
		Win32
			bin
				flex
			include
			lib

GLib and its dependencies need to be installed in <root>\vs10\Win32 folder,
which is default behavior when you put the source to <root>\build\glib and
build it.

The gstreamer projects follow similar pattern. After build the result files
will be placed in <root>\vs10\Win32\bin, include, and lib folders.

The entire flex distribution (including data folder) needs to be placed in the
<root>\vs10\Win32\bin folder. If you put the data folder elsewhere you will need
to modify BISON_PKGDATADIR variable in Common.props (see the next seciton)

Modifying Build Settings
========================

This build makes use of Visual Studio Property sheets. All projects include the
Common.props property sheet preset in gstreamer\win32\vs10

IMPORTANT:
You will need to modify at least one property - GstAbsPluginPath. This property
should contains escaped path to default GStreamer plugin folder, i.e.
C:\\gstreamer\\vs10\\Win32\\lib\\gstreamer-1.0 when your <root> is C:\\gstreamer

All projects include common property files from the gstreamer project so it must
be present when building gst-plugins-base,good,ugly,bad and gst-ffmpeg.

Creating Project for New Library or Plugin
==========================================

1. Create empty project in the Solution. Note that project name is improtant.

   For plugin the project name should be in form of "gstmypluigin" and the resulting
   plugin dll will have same name as the project (i.e. "gstmyplugin.dll")

   For library the project name should be same as the library folder. I.e. for
   gst-libs\gst\pbutils library in gst-plugins-base the project name is "pbutils"

2. Add gstreamer\win32\vs10\Common.props and either gstreamer\win32\vs10\Library.props
   or gstreamer\Win32\vs10\Plugin.props depending on whether the project is for library
   or plugin.

3. Set the project type to Shared Library in project preferences

4. If the project contains any generated gst-enums or needs to generate marshallers add
   the appropriate entries to solution "generate" project. See the generate project in
   gst-plugins-base solution for example

5. Add source files to project

6. If necessary modify the project settings. Usually the only modification necessary
   should be adding dependency libraries in linker input settings or specfying project
   dependencies if the new project depends on another project within the solution

7. If the project is a library it must have a def file in win32\common with name
   libgst<projectname>.def . I.e. pbutils project needs to have libgstpbutils.def
   file in gst-plugins-base\win32\common
