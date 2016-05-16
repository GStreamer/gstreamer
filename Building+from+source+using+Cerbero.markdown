#  GStreamer SDK documentation : Building from source using Cerbero 

This page last changed on Jul 15, 2013 by ylatuya.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>This section is intended for advanced users.</p></td>
</tr>
</tbody>
</table>

#### Build requirements

The GStreamer SDK build system provides bootstrapping facilities for all
platforms, but it still needs a minimum base to bootstrap:

  - python \>= 2.6 and python's `argparse` module, which is already
    included in python2.7.
  - git

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><strong>Windows users</strong><br />

<p>Cerbero can be used on Windows using the Msys/MinGW shell (a Unix-like shell for Windows). There is a bit of setup that you need to do before Cerbero can take control.</p>
<p>You need to install the following programs:</p>
<ul>
<li><a href="http://www.python.org/getit/releases/2.7/" class="external-link">Python 2.7</a></li>
<li><a href="http://code.google.com/p/msysgit/downloads/list?q=full+installer+official+git" class="external-link">Git</a> (Select the install option &quot;Checkout as-is, Commit as-is&quot; and install it in a path without spaces, eg: c:\Git)</li>
<li><a href="https://sourceforge.net/projects/mingw/files/Installer/mingw-get-inst/" class="external-link">Msys/MinGW</a> (Install it with all the options enabled)</li>
<li><a href="http://www.cmake.org/cmake/resources/software.html" class="external-link">CMake</a> (Select the option &quot;Add CMake in system path for the current user&quot;)</li>
<li><p><a href="http://yasm.tortall.net/Download.html" class="external-link">Yasm</a> (Download the win32 or win64 version for your platform, name it <code>yasm.exe</code>, and place it in your MinGW <code>bin</code> directory, typically, <code>C:\MinGW\bin</code>)</p></li>
<li><a href="http://wix.codeplex.com/releases/view/60102" class="external-link">WiX 3.5</a> </li>
<li><a href="http://www.microsoft.com/en-us/download/details.aspx?id=8279" class="external-link">Microsoft SDK 7.1</a> (Install the SDK samples and the Visual C++ Compilers, required to build the DirectShow base classes. Might need installing the .NET 4 Framework first if the SDK installer doesn't find it)</li>
<li><a href="http://msdn.microsoft.com/en-us/windows/hardware/hh852365" class="external-link">Windows Driver Kit 7.1.0</a></li>
</ul>
<p>Your user ID can't have spaces (eg: John Smith). Paths with spaces are not correctly handled in the build system and msys uses the user ID for the home folder.</p>
<p>Cerbero must be run in the MinGW shell, which is accessible from the main menu once MinGW is installed.</p>
<p>The last step is making <code>python</code> and <code>git</code> available from the shell, for which you will need to create a <code>.profile</code> file. Issue this command from within the MinGW shell:</p>
<div class="code panel" style="border-width: 1px;">
<div class="codeContent panelContent">
<pre class="theme: Default; brush: plain; gutter: false" style="font-size:12px;"><code>echo &quot;export PATH=\&quot;\$PATH:/c/Python27:/c/Git/bin\&quot;&quot; &gt;&gt; ~/.profile</code></pre>
</div>
</div>
<p>Using the appropriate paths to where you installed <code>python</code> and <code>git</code>.</p>
<p>(Note that inside the shell, / is mapped to c:\Mingw\msys\1.0\ )</p></td>
</tr>
</tbody>
</table>

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><strong>OS X users</strong><br />

<p>To use cerbero on OS X you need to install the &quot;Command Line Tools&quot; from XCode. They are available from the &quot;Preferences&quot; dialog under &quot;Downloads&quot;.</p></td>
</tr>
</tbody>
</table>

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><strong>iOS developers</strong><br />

<p>If you want to build the GStreamer-SDK for iOS, you also need the iOS SDK. The minimum required iOS SDK version is 6.0 and is included in <a href="https://developer.apple.com/devcenter/ios/index.action#downloads" class="external-link">Xcode</a> since version 4.</p></td>
</tr>
</tbody>
</table>

Download the sources

To build the GStreamer SDK, you first need to download **Cerbero**.
Cerbero is a multi-platform build system for Open Source projects that
builds and creates native packages for different platforms,
architectures and distributions.

Get a copy of Cerbero by cloning the git repository:

``` theme: Default; brush: plain; gutter: false
git clone git://anongit.freedesktop.org/gstreamer-sdk/cerbero
```

Cerbero can be run uninstalled and for convenience you can create an
alias in your `.bashrc` file*. *If you prefer to skip this step,
remember that you need to replace the calls to `cerbero` with
`./cerbero-uninstalled` in the next steps.

``` theme: Default; brush: plain; gutter: false
echo "alias cerbero='~/git/cerbero/cerbero-uninstalled'" >> ~/.bashrc
```

#### Setup environment

After Cerbero and the base requirements are in place, you need to setup
the build environment.

Cerbero reads the configuration file `$HOME/.cerbero/cerbero.cbc` to
determine the build options. This file is a python code which allows
overriding/defining some options.

If the file does not exist, Cerbero will try to determine the distro you
are running and will use default build options such as the default build
directory. The default options should work fine on the supported
distributions.

An example configuration file with detailed comments can be found
[here](http://www.freedesktop.org/software/gstreamer-sdk/cerbero.cbc.template)

To fire up the bootstrapping process, go to the directory where you
cloned/unpacked Cerbero and type:

``` theme: Default; brush: plain; gutter: false
cerbero bootstrap 
```

Enter the superuser/root password when prompted.

The bootstrap process will then install all packages required to build
the GStreamer SDK.

#### Build the SDK

To generate the SDK, use the following command:

``` theme: Default; brush: plain; gutter: false
cerbero package gstreamer-sdk
```

This should build all required SDK components and create packages for
your distribution at the Cerbero source directory.

A list of supported packages to build can be retrieved using:

``` theme: Default; brush: plain; gutter: false
cerbero list-packages
```

Packages are composed of 0 (in case of a meta package) or more
components that can be built separately if desired. The components are
defined as individual recipes and can be listed with:

``` theme: Default; brush: plain; gutter: false
cerbero list
```

To build an individual recipe and its dependencies, do the following:

``` theme: Default; brush: plain; gutter: false
cerbero build <recipe_name>
```

Or to build or force a rebuild of a recipe without building its
dependencies use:

``` theme: Default; brush: plain; gutter: false
cerbero buildone <recipe_name>
```

To wipe everything and start from scratch:

``` theme: Default; brush: plain; gutter: false
cerbero wipe
```

Once built, the output of the recipes will be installed at the prefix
defined in the Cerbero configuration file `$HOME/.cerbero/cerbero.cbc`
or at `$HOME/cerbero/dist` if no prefix is defined.

#### Build a single project with the SDK

Rebuilding the whole SDK is relatively fast on Linux and OS X, but it
can be very slow on Windows, so if you only need to rebuild a single
project (eg: gst-plugins-good to patch qtdemux) there is a much faster
way of doing it. You will need  to follow the steps detailed in this
page, but skipping the step "**Build the SDK**", and installing the
SDK's development files as explained in [Installing the
SDK](Installing%2Bthe%2BSDK.html).

By default, Cerbero uses as prefix a folder in the user directory with
the following schema ~/cerbero/dist/$platform\_$arch, but for the SDK we
must change this prefix to use its installation directory. This can be
done with a custom configuration file named *custom.cbc*:

``` theme: Default; brush: plain; gutter: false
# For Windows x86
prefix='/c/gstreamer-sdk/0.10/x86/'
 
# For Windows x86_64
#prefix='/c/gstreamer-sdk/0.10/x86_64'
 
# For Linux
#prefix='/opt/gstreamer-sdk'
 
# For OS X
#prefix='/Library/Frameworks/GStreamer.framework/Versions/0.10'
```

The prefix path might not be writable by your current user. Make sure
you fix it before, for instance with:

``` theme: Default; brush: plain; gutter: false
$ sudo chown -R <username> /Library/Frameworks/GStreamer.framework/
```

Cerbero has a shell command that starts a new shell with all the
environment set up to target the SDK. You can start a new shell using
the installation prefix defined in *custom.cbc *with the following
command:

``` theme: Default; brush: plain; gutter: false
$ cerbero -c custom.cbc shell
```

Once you are in Cerbero's shell you can compile new
projects targeting the SDK using the regular build
process:

``` theme: Default; brush: plain; gutter: false
$ git clone -b sdk-0.10.31 git://anongit.freedesktop.org/gstreamer-sdk/gst-plugins-good; cd gst-plugins-good
$ sh autogen.sh --disable-gtk-doc --prefix=<prefix>
$ make -C gst/isomp4
```

 

#### Cross-compilation of the SDK

Cerbero can be used to cross-compile the SDK to other platforms like
Android or Windows. You only need to use a configuration file that sets
the target platform, but we also provide a set of of pre-defined
configuration files for the supported platforms (you will find them in
the `config` folder with the `.cbc` extension

##### Android

You can cross-compile the SDK for Android from a Linux host using the
configuration file `config/cross-android.cbc`. Replace all the previous
commands with:

``` theme: Default; brush: plain; gutter: false
cerbero -c config/cross-android.cbc <command>
```

##### Windows

The SDK can also be cross-compiled to Windows from Linux, but you should
only use it for testing purpose. The DirectShow plugins cannot be
cross-compiled yet and WiX can't be used with Wine yet, so packages can
only be created from Windows.

Replace all the above commands for Windows 32bits with:

``` theme: Default; brush: plain; gutter: false
cerbero -c config/cross-win32.cbc <command>
```

Or with using the following for Windows 64bits:

``` theme: Default; brush: plain; gutter: false
cerbero -c config/cross-win64.cbc <command>
```

##### iOS

To cross compile for iOS from OS X, use the configuration file
`config/cross-ios-universal.cbc`. Replace all previous commands with:

``` theme: Default; brush: cpp; gutter: false
cerbero -c config/cross-ios-universal.cbc <command>
```

Document generated by Confluence on Oct 08, 2015 10:27

