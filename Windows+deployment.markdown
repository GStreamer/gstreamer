#  GStreamer SDK documentation : Windows deployment 

This page last changed on Nov 28, 2012 by xartigas.

This page explains how to deploy GStreamer along your application. There
are different mechanisms, which have been reviewed in [Deploying your
application](Deploying%2Byour%2Bapplication.html). The details for some
of the mechanisms are given here, and more options might be added to
this documentation in the future.

# Shared GStreamer

This is the easiest way to deploy GStreamer, although most of the time
it installs unnecessary files which grow the size of the installer and
the target drive free space requirements. Since the SDK might be shared
among all applications that use it, though, the extra space requirements
are somewhat blurred.

Simply pack the GStreamer SDK  **runtime**  installer ([the same one you
installed in your development machine](Installing%2Bon%2BWindows.html))
inside your installer (or download it from your installer) and execute
it silently using `msiexec`. `msiexec` is the tool that wraps most of
the Windows Installer functionality and offers a number of options to
suit your needs. You can review these options by
executing `msiexec` without parameters. For example:

``` theme: Default; brush: plain; gutter: false
msiexec /i gstreamer-sdk-2012.9-x86.msi
```

This will bring up the installation dialog as if the user had
double-clicked on the `msi` file. Usually, you will want to let the user
choose where they want to install the SDK. An environment variable will
let your application locate it later on.

# Private deployment of GStreamer

You can use the same method as the shared SDK, but instruct its
installer to deploy to your application’s folder (or a
subfolder). Again, use the `msiexec` parameters that suit you best. For
example:

``` theme: Default; brush: plain; gutter: false
msiexec /passive INSTALLDIR=C:\Desired\Folder /i gstreamer-sdk-2012.9-x86.msi
```

This will install the SDK to `C:\Desired\Folder`  showing a progress
dialog, but not requiring user intervention.

# Deploy only necessary files, by manually picking them

On the other side of the spectrum, if you want to reduce the space
requirements (and installer size) to the maximum, you can manually
choose which GStreamer libraries to deploy. Unfortunately, you are on
your own on this road, besides using the [Dependency
Walker](http://www.dependencywalker.com/) tool to discover inter-DLL
dependencies.

Bear in mind that GStreamer is modular in nature. Plug-ins are loaded
depending on the media that is being played, so, if you do not know in
advance what files you are going to play, you do not know which DLLs you
need to deploy.

# Deploy only necessary packages, using provided Merge Modules

If you are building your installer using one of the Professional
editions of [Visual
Studio](http://www.microsoft.com/visualstudio/en-us/products/2010-editions/professional/overview)
or [WiX](http://wix.sf.net) you can take advantage of pre-packaged
[Merge
Modules](http://msdn.microsoft.com/en-us/library/windows/desktop/aa369820\(v=vs.85\).aspx).
The GStreamer SDK is divided in packages, which roughly take care of
different tasks. There is the core package, the playback package, the
networking package, etc. Each package contains the necessary libraries
and files to accomplish its task.

The Merge Modules are pieces that can be put together to build a larger
Windows Installer. In this case, you just need to create a deployment
project for your application with Visual Studio and then add the Merge
Modules for the GStreamer packages your application needs.

This will produce a smaller installer than deploying the complete
GStreamer SDK, without the added burden of having to manually pick each
library. You just need to know which packages your application requires.

![](images/icons/grey_arrow_down.gif)Available packages (Click to
expand)

<table>
<colgroup>
<col width="25%" />
<col width="25%" />
<col width="25%" />
<col width="25%" />
</colgroup>
<thead>
<tr class="header">
<th>Package name</th>
<th>Dependencies</th>
<th>Licenses</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>base-system</td>
<td> </td>
<td>JPEG, FreeType, BSD-like, LGPL,<br />
LGPL-2+, LGPL-2.1, LibPNG and MIT</td>
<td>Base system dependencies</td>
</tr>
<tr class="even">
<td>gobject-python</td>
<td>base-system</td>
<td>LGPL</td>
<td>GLib/GObject python bindings</td>
</tr>
<tr class="odd">
<td>gstreamer-capture</td>
<td>base-system, gstreamer-core</td>
<td>LGPL and LGPL-2+</td>
<td>GStreamer plugins for capture</td>
</tr>
<tr class="even">
<td>gstreamer-clutter</td>
<td>base-system, gtk+-2.0, gstreamer-core</td>
<td>LGPL</td>
<td>GStreamer Clutter support</td>
</tr>
<tr class="odd">
<td>gstreamer-codecs</td>
<td>base-system, gstreamer-core</td>
<td>BSD, Jasper-2.0, BSD-like, LGPL,<br />
LGPL-2, LGPL-2+, LGPL-2.1 and LGPL-2.1+</td>
<td>GStreamer codecs</td>
</tr>
<tr class="even">
<td>gstreamer-codecs-gpl</td>
<td>base-system, gstreamer-core</td>
<td>BSD-like, LGPL, LGPL-2+ and LGPL-2.1+</td>
<td>GStreamer codecs under the GPL license and/or with patents issues</td>
</tr>
<tr class="odd">
<td>gstreamer-core</td>
<td>base-system</td>
<td>LGPL and LGPL-2+</td>
<td>GStreamer core</td>
</tr>
<tr class="even">
<td>gstreamer-dvd</td>
<td>base-system, gstreamer-core</td>
<td>GPL-2+, LGPL and LGPL-2+</td>
<td>GStreamer DVD support</td>
</tr>
<tr class="odd">
<td>gstreamer-effects</td>
<td>base-system, gstreamer-core</td>
<td>LGPL and LGPL-2+</td>
<td>GStreamer effects and instrumentation plugins</td>
</tr>
<tr class="even">
<td>gstreamer-networking</td>
<td>base-system, gstreamer-core</td>
<td>GPL-3, LGPL, LGPL-2+, LGPL-2.1+<br />
and LGPL-3+</td>
<td>GStreamer plugins for network protocols</td>
</tr>
<tr class="odd">
<td>gstreamer-playback</td>
<td>base-system, gstreamer-core</td>
<td>LGPL and LGPL-2+</td>
<td>GStreamer plugins for playback</td>
</tr>
<tr class="even">
<td>gstreamer-python</td>
<td>base-system, gobject-python,<br />
gstreamer-core</td>
<td>LGPL and LGPL-2.1+</td>
<td>GStreamer python bindings</td>
</tr>
<tr class="odd">
<td>gstreamer-system</td>
<td>base-system, gstreamer-core</td>
<td>LGPL, LGPL-2+ and LGPL-2.1+</td>
<td>GStreamer system plugins</td>
</tr>
<tr class="even">
<td>gstreamer-tutorials</td>
<td> </td>
<td>LGPL</td>
<td>Tutorials for GStreamer</td>
</tr>
<tr class="odd">
<td>gstreamer-visualizers</td>
<td>base-system, gstreamer-core</td>
<td>LGPL and LGPL-2+</td>
<td>GStreamer visualization plugins</td>
</tr>
<tr class="even">
<td>gtk+-2.0</td>
<td>base-system</td>
<td>LGPL</td>
<td>Gtk toolkit</td>
</tr>
<tr class="odd">
<td>gtk+-2.0-python</td>
<td>base-system, gtk+-2.0</td>
<td>LGPL and LGPL-2.1+</td>
<td>Gtk python bindings</td>
</tr>
<tr class="even">
<td>snappy</td>
<td><p>base-system, gstreamer-clutter,<br />
gtk+-2.0, gstreamer-playback,<br />
gstreamer-core, gstreamer-codecs</p></td>
<td>LGPL</td>
<td>Snappy media player</td>
</tr>
</tbody>
</table>

If you include a merge module in your deployment project, remember to
include also its dependencies. Otherwise, the project will build
correctly and install flawlessly, but, when executing your application,
it will miss files.

Get the ZIP file with all Merge Modules for your architecture:

<table>
<colgroup>
<col width="100%" />
</colgroup>
<thead>
<tr class="header">
<th>32 bits</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><a href="http://cdn.gstreamer.com/windows/x86/gstreamer-sdk-x86-2013.6-merge-modules.zip" class="external-link">GStreamer SDK 2013.6 (Congo) for Windows 32 bits (Merge Modules)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/windows/x86/gstreamer-sdk-x86-2013.6-merge-modules.zip" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/windows/x86/gstreamer-sdk-x86-2013.6-merge-modules.zip.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/windows/x86/gstreamer-sdk-x86-2013.6-merge-modules.zip.sha1" class="external-link">sha1</a></p></td>
</tr>
<tr class="even">
<td><span style="color: rgb(0,0,0);">64 bits</span></td>
</tr>
<tr class="odd">
<td><a href="http://cdn.gstreamer.com/windows/x86-64/gstreamer-sdk-x86_64-2013.6-merge-modules.zip" class="external-link">GStreamer SDK 2013.6 (Congo) for Windows 64 bits (Merge Modules)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/windows/x86-64/gstreamer-sdk-x86_64-2013.6-merge-modules.zip" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/windows/x86-64/gstreamer-sdk-x86_64-2013.6-merge-modules.zip.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/windows/x86-64/gstreamer-sdk-x86_64-2013.6-merge-modules.zip.sha1" class="external-link">sha1</a></td>
</tr>
</tbody>
</table>

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>Due to the size of these files, usage of a <a href="http://en.wikipedia.org/wiki/Download_manager" class="external-link">Download Manager</a> is <strong>highly recommended</strong>. Take a look at <a href="http://en.wikipedia.org/wiki/Comparison_of_download_managers" class="external-link">this list</a> if you do not have one installed. If, after downloading, the installer reports itself as corrupt, chances are that the connection ended before the file was complete. A Download Manager will typically re-start the process and fetch the missing parts.</p></td>
</tr>
</tbody>
</table>

Document generated by Confluence on Oct 08, 2015 10:27

