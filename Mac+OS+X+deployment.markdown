#  GStreamer SDK documentation : Mac OS X deployment 

This page last changed on Nov 28, 2012 by xartigas.

This page explains how to deploy GStreamer along your application. There
are different mechanisms, which have been reviewed in [Deploying your
application](Deploying%2Byour%2Bapplication.html). The details for some
of the mechanisms are given here, and more options might be added to
this documentation in the future.

The recommended tool to create installer packages for Mac OS X
is [PackageMaker](http://developer.apple.com/library/mac/#documentation/DeveloperTools/Conceptual/PackageMakerUserGuide/Introduction/Introduction.html),
provided with the [XCode developer
tools](https://developer.apple.com/technologies/tools/).

# Shared GStreamer

This is the easiest way to deploy GStreamer, although most of the time
it installs unnecessary files which grow the size of the installer and
the target drive free space requirements. Since the SDK might be shared
among all applications that use it, though, the extra space requirements
are somewhat blurred.![](attachments/2097292/2424841.png)
![](attachments/thumbnails/2097292/2424842)

With PackageMaker, simply add the GStreamer SDK  **runtime ** disk image
 ([the same one you used to install the runtime in your development
machine](Installing%2Bon%2BMac%2BOS%2BX.html)) inside your installer
package and create a post-install script that mounts the disk image and
installs the SDK package. You can use the following example, where you
should replace `$INSTALL_PATH` with the path where your installer copied
the SDK's disk image files (the `/tmp` directory is good place to
install it as it will be removed at the end of the installation):

``` theme: Default; brush: bash; gutter: false
hdiutil attach $INSTALL_PATH/gstreamer-sdk-2012.7-x86.dmg
cd /Volumes/gstreamer-sdk-2012.7-x86/
installer -pkg gstreamer-sdk-2012.7-x86.pkg -target "/"
hdiutil detach /Volumes/gstreamer-sdk-2012.7-x86/
rm $INSTALL_PATH/gstreamer-sdk-2012.7-x86.dmg
```

# Private deployment of GStreamer

You can decide to distribute a private copy of the SDK with your
application, although it's not the recommended method. In this case,
simply copy the framework to the application's Frameworks folder as
defined in the [bundle programming
guide](https://developer.apple.com/library/mac/documentation/CoreFoundation/Conceptual/CFBundles/BundleTypes/BundleTypes.html#//apple_ref/doc/uid/10000123i-CH101-SW19):

``` theme: Default; brush: bash; gutter: false
cp -r /Library/Frameworks/GStreamer.framework ~/MyApp.app/Contents/Frameworks
```

Note that you can have several versions of the SDK, and targeting
different architectures, installed in the system. Make sure you only
copy the version you need and that you update accordingly the link
`GStreamer.framework/Version/Current`:

``` theme: Default; brush: bash; gutter: false
$ ls -l Frameworks/GStreamer.framework/Version/Current
lrwxr-xr-x 1 fluendo staff 21 Jun 5 18:46 Frameworks/GStreamer.framework/Versions/Current -> ../Versions/0.10/x86
```

Since the SDK will be relocated, you will need to follow the
instructions on how to relocate the SDK at the end of this page.

# Deploy only necessary files, by manually picking them

On the other side of the spectrum, if you want to reduce the space
requirements (and installer size) to the maximum, you can manually
choose which GStreamer libraries to deploy. Unfortunately, you are on
your own on this road, besides using the object file displaying tool:
[otool](https://developer.apple.com/library/mac/#documentation/darwin/reference/manpages/man1/otool.1.html).
Being a similar technique to deploying a private copy of the SDK, keep
in mind that you should relocate the SDK too, as explained at the end of
this page.

Bear also in mind that GStreamer is modular in nature. Plug-ins are
loaded depending on the media that is being played, so, if you do not
know in advance what files you are going to play, you do not know which
plugins and shared libraries you need to deploy.

# Deploy only necessary packages, using the provided ones

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

Get the disk image file with all the packages:

<table>
<colgroup>
<col width="100%" />
</colgroup>
<thead>
<tr class="header">
<th>Universal</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><a href="http://cdn.gstreamer.com/osx/universal/gstreamer-sdk-2013.6-universal-packages.dmg" class="external-link">GStreamer SDK 2013.6 (Congo) for Mac OS X (Deployment Packages)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/osx/universal/gstreamer-sdk-2013.6-universal-packages.dmg" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/osx/universal/gstreamer-sdk-2013.6-universal-packages.dmg.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/osx/universal/gstreamer-sdk-2013.6-universal-packages.dmg.sha1" class="external-link">sha1</a></p></td>
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

# Relocation of the SDK in OS X

In some situations we might need to relocate the SDK, moving it to a
different place in the file system, like for instance when we are
shipping a private copy of the SDK with our application.

### Location of dependent dynamic libraries.

On Darwin operating systems, the dynamic linker doesn't locate dependent
dynamic libraries using their leaf name, but instead it uses full paths,
which makes it harder to relocate them as explained in the DYNAMIC
LIBRARY LOADING section of
[dyld](https://developer.apple.com/library/mac/#documentation/Darwin/Reference/ManPages/man1/dyld.1.html)'s
man page:

> Unlike many other operating systems, Darwin does not locate dependent
> dynamic libraries via their leaf file name. Instead the full path to
> each dylib is used (e.g. /usr/lib/libSystem.B.dylib). But there are
> times when a full path is not appropriate; for instance, may want your
> binaries to be installable in anywhere on the disk.

We can get the list of paths used by an object file to locate its
dependent dynamic libraries
using [otool](https://developer.apple.com/library/mac/#documentation/darwin/reference/manpages/man1/otool.1.html):

``` theme: Default; brush: bash; gutter: false
$ otool -L /Library/Frameworks/GStreamer.framework/Commands/gst-launch-0.10 
/Library/Frameworks/GStreamer.framework/Commands/gst-launch-0.10:
 /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (compatibility version 150.0.0, current version 550.43.0)
 /Library/Frameworks/GStreamer.framework/Versions/0.10/x86/lib/libgstreamer-0.10.0.dylib (compatibility version 31.0.0, current version 31.0.0)
 /Library/Frameworks/GStreamer.framework/Versions/0.10/x86/lib/libxml2.2.dylib (compatibility version 10.0.0, current version 10.8.0)
...
```

As you might have already noticed, if we move the SDK to a different
folder, it will stop working because the runtime linker won't be able to
find `gstreamer-0.10` in the previous location
`/Library/Frameworks/GStreamer.framework/Versions/0.10/x86/lib/libgstreamer-0.10.0.dylib`.

This full path is extracted from the dynamic library  ***install name***
, a path that is used by the linker to determine its location. The
install name of a library can be retrieved with
[otool](https://developer.apple.com/library/mac/#documentation/darwin/reference/manpages/man1/otool.1.html) too:

``` theme: Default; brush: bash; gutter: false
$ otool -D /Library/Frameworks/GStreamer.framework/Libraries/libgstreamer-0.10.dylib 
/Library/Frameworks/GStreamer.framework/Libraries/libgstreamer-0.10.dylib:
/Library/Frameworks/GStreamer.framework/Versions/0.10/x86/lib/libgstreamer-0.10.0.dylib
```

Any object file that links to the dynamic library `gstreamer-0.10` will
use the
path `/Library/Frameworks/GStreamer.framework/Versions/0.10/x86/lib/libgstreamer-0.10.0.dylib` to
locate it, as we saw previously with `gst-launch-0.10`.

Since working exclusively with full paths wouldn't let us install our
binaries anywhere in the path, the linker provides a mechanism of string
substitution, adding three variables that can be used as a path prefix.
At runtime the linker will replace them with the generated path for the
prefix. These variables are `@executable_path`,
`@loader_path` and `@rpath`, described in depth in the DYNAMIC LIBRARY
LOADING section
of [dyld](https://developer.apple.com/library/mac/#documentation/Darwin/Reference/ManPages/man1/dyld.1.html)'s
man page.

For our purpose we will use the `@executable_path` variable, which is
replaced with a fixed path, the path to the directory containing the
main executable: `/Applications/MyApp.app/Contents/MacOS`.
The `@loader_path` variable can't be used in our scope, because it will
be replaced with the path to the directory containing the mach-o binary
that loaded the dynamic library, which can vary.

Therefore, in order to relocate the SDK we will need to replace all
paths
containing `/Library/Frameworks/GStreamer.framework/` with `@executable_path/../Frameworks/GStreamer.framework/`, which
can be done using
the [install\_name\_tool](http://developer.apple.com/library/mac/#documentation/Darwin/Reference/ManPages/man1/install_name_tool.1.html)
utility

### Relocation of the binaries

As mentioned in the previous section, we can use
the `install_name_tool` in combination with `otool` to list all paths
for dependant dynamic libraries and modify them to use the new location.
However the SDK has a huge list of binaries and doing it manually would
be a painful task. That's why a simple relocation script is provided
which you can find in cerbero's repository
(`cerbero/tools/osxrelocator.py`). This scripts takes 3 parameters:

1.  `directory`: the directory to parse looking for binaries
2.  `old_prefix`: the old prefix we want to change (eg:
    `/Library/Frameworks/GStreamer.framework`)
3.  `new_prefix`: the new prefix we want to use
    (eg: `@executable_path/../Frameworks/GStreamer.framework/`)

When looking for binaries to fix, we will run the script in the
following
directories:

``` theme: Default; brush: bash; gutter: false
$ osxrelocator.py MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/lib /Library/Frameworks/GStreamer.framework/ @executable_path/../Frameworks/GStreamer.framework/ -r
$ osxrelocator.py MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/libexec /Library/Frameworks/GStreamer.framework/ @executable_path/../Frameworks/GStreamer.framework/ -r
$ osxrelocator.py MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/bin /Library/Frameworks/GStreamer.framework/ @executable_path/../Frameworks/GStreamer.framework/ -r
$ osxrelocator.py MyApp.app/Contents/MacOS /Library/Frameworks/GStreamer.framework/ @executable_path/../Frameworks/GStreamer.framework/ -r
```

### Adjusting environment variables with the new paths

The application also needs to set the following environment variables to
help other libraries finding resources in the new
    path:

  - `GST_PLUGIN_SYSTEM_PATH=/Applications/MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/lib/gstreamer-0.10`
  - `GST_PLUGIN_SCANNER=/Applications/MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/libexec/gstreamer-0.10/gst-plugin-scanner`
  - `GTK_PATH=/Applications/MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/`
  - `GIO_EXTRA_MODULES=/Applications/MyApp.app/Contents/Frameworks/GStreamer.framework/Versions/Current/lib/gio/modules`

You can use the following functions:

  - C: [putenv("VAR=/foo/bar")](http://linux.die.net/man/3/putenv)

  - Python: [os.environ\['VAR'\] =
    '/foo/var'](http://docs.python.org/library/os.html)

## Attachments:

![](images/icons/bullet_blue.gif)
[PackageMaker1.png](attachments/2097292/2424841.png) (image/png)  
![](images/icons/bullet_blue.gif)
[PackageMaker2.png](attachments/2097292/2424842.png) (image/png)  

Document generated by Confluence on Oct 08, 2015 10:27

