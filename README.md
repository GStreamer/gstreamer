# GStreamer

This is GStreamer, a framework for streaming media.

## Where to start

We have a website at

  https://gstreamer.freedesktop.org

Our documentation, including tutorials, API reference and FAQ can be found at

  https://gstreamer.freedesktop.org/documentation/

You can subscribe to our mailing lists:

  https://lists.freedesktop.org/mailman/listinfo/gstreamer-announce

  https://lists.freedesktop.org/mailman/listinfo/gstreamer-devel

We track bugs, feature requests and merge requests (patches) in GitLab at

  https://gitlab.freedesktop.org/gstreamer/

You can join us on IRC - #gstreamer on irc.oftc.net

This repository contains all official modules supported by the GStreamer
community which can be found in the `subprojects/` directory.

## Getting started

### Install git and python 3.8+

If you're on Linux, you probably already have these. On macOS, new versions of
Xcode ship Python 3 already. If you're on an older Xcode, you can use the
[official Python installer](https://www.python.org/downloads/mac-osx/).

You can find [instructions for Windows below](#windows-prerequisites-setup).

### Install meson and ninja

Meson 0.62 or newer is required.

On Linux and macOS you can get meson through your package manager or using:

  $ pip3 install --user meson

This will install meson into `~/.local/bin` which may or may not be included
automatically in your PATH by default.

You should get `ninja` using your package manager or download the [official
release](https://github.com/ninja-build/ninja/releases) and put the `ninja`
binary in your PATH.

You can find [instructions for Windows below](#windows-prerequisites-setup).


If you used the official Python installer on macOS instead of the Python
3 shipped with Xcode, you might need to execute "Install Certificates.command"
from the Python folder in the user Applications folder:

```
$ /Applications/Python\ 3.*/Install\ Certificates.command
```

Otherwise you will get this error when downloading meson wraps:

```
urllib.error.URLError: urlopen error [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed
```

### Build GStreamer and its modules

You can get all GStreamer built running:

```
meson setup builddir
meson compile -C builddir
```

This will automatically create the `builddir` directory and build everything
inside it.

NOTE: On Windows, meson will automatically detect and use the latest Visual
Studio if GCC, clang, etc are not available in `PATH`. Use the `--vsenv`
argument to force the use of Visual Studio.

NOTE: Meson will not update subprojects automatically once a subproject has
been fetched. Remember to update subprojects if wrap files are updated.

```
meson subprojects update
```

### External dependencies

All mandatory dependencies of GStreamer are included as [meson subprojects](https://mesonbuild.com/Subprojects.html):
libintl, zlib, libffi, glib. Some optional dependencies are also included as
subprojects, such as ffmpeg, x264, json-glib, graphene, openh264, orc, etc.

Mandatory dependencies will be automatically built if meson cannot find them on
your system using pkg-config. The same is true for optional dependencies that
are included as subprojects. You can find a full list by looking at the
`subprojects` directory.

Plugins that need optional dependencies that aren't included can only be built
if they are provided by the system. Instructions on how to build some common
ones such as Qt5/QML are listed below. If you do not know how to provide an
optional dependency needed by a plugin, you should use [Cerbero](https://gitlab.freedesktop.org/gstreamer/cerbero/#description)
which handles this for you automatically.

Plugins will be automatically enabled if possible, but you can ensure that
a particular plugin (especially if it has external dependencies) is built by
enabling the gstreamer repository that ships it and the plugin inside it. For
example, to enable the Qt5 plugin in the gst-plugins-good repository, you need
to run meson as follows:

```
meson -Dgood=enabled -Dgst-plugins-good:qt5=enabled builddir
```

This will cause Meson to error out if the plugin could not be enabled. You can
also flip the default and disable all plugins except those explicitly enabled
like so:

```
meson -Dauto_features=disabled -Dgstreamer:tools=enabled -Dbad=enabled -Dgst-plugins-bad:openh264=enabled
```

This will disable all optional features and then enable the `openh264` plugin
and the tools that ship with the core gstreamer repository: `gst-inspect-1.0`,
`gst-launch-1.0`, etc. As usual, you can change these values on a builddir that
has already been setup with `meson configure -Doption=value`.

### Building the Qt5 QML plugin

If `qmake` is not in `PATH` and pkgconfig files are not available, you can
point the `QMAKE` env var to the Qt5 installation of your choosing before
running `meson` as shown above.

The plugin will be automatically enabled if possible, but you can ensure that
it is built by passing `-Dgood=enabled -Dgst-plugins-good:qt5=enabled` to `meson`.

### Building the Intel MSDK plugin

On Linux, you need to have development files for `libmfx` installed. On
Windows, if you have the [Intel Media SDK](https://software.intel.com/en-us/media-sdk),
it will set the `INTELMEDIASDKROOT` environment variable, which will be used by
the build files to find `libmfx`.

The plugin will be automatically enabled if possible, but you can ensure it by
passing `-Dbad=enabled -Dgst-plugins-bad:msdk=enabled` to `meson`.

### Building plugins with (A)GPL-licensed dependencies

Some plugins have GPL- or AGPL-licensed dependencies and will only be built
if you have explicitly opted in to allow (A)GPL-licensed dependencies by
passing `-Dgpl=enabled` to Meson.

List of plugins with (A)GPL-licensed dependencies (non-exhaustive) in gst-plugins-bad:
 - dts (DTS audio decoder plugin)
 - faad (Free AAC audio decoder plugin)
 - iqa (Image quality assessment plugin based on dssim-c)
 - mpeg2enc (MPEG-2 video encoder plugin)
 - mplex (audio/video multiplexer plugin)
 - ofa (Open Fingerprint Architecture library plugin)
 - resindvd (Resin DVD playback plugin)
 - x265 (HEVC/H.265 video encoder plugin)

List of plugins with (A)GPL-licensed dependencies (non-exhaustive) in gst-plugins-ugly:
 - a52dec (Dolby Digital (AC-3) audio decoder plugin)
 - cdio (CD audio source plugin based on libcdio)
 - dvdread (DVD video source plugin based on libdvdread)
 - mpeg2dec (MPEG-2 video decoder plugin based on libmpeg2)
 - sidplay (Commodore 64 audio decoder plugin based on libsidplay)
 - x264 (H.264 video encoder plugin based on libx264)

### Static build

Since *1.18.0* when doing a static build using `--default-library=static`, a
shared library `gstreamer-full-1.0` will be produced and includes all enabled
GStreamer plugins and libraries. A list of libraries that needs to be exposed in
`gstreamer-full-1.0` ABI can be set using `gst-full-libraries` option. glib-2.0,
gobject-2.0 and gstreamer-1.0 are always included.

```
meson --default-library=static -Dgst-full-libraries=app,video builddir
```

GStreamer *1.18* requires applications using gstreamer-full-1.0 to initialize
static plugins by calling `gst_init_static_plugins()` after `gst_init()`. That
function is defined in `gst/gstinitstaticplugins.h` header file.

Since *1.20.0* `gst_init_static_plugins()` is called automatically by
`gst_init()` and applications must not call it manually any more. The header
file has been removed from public API.

One can use the `gst-full-version-script` option to pass a
[version script](https://www.gnu.org/software/gnulib/manual/html_node/LD-Version-Scripts.html)
to the linker. This can be used to control the exact symbols that are exported by
the gstreamer-full library, allowing the linker to garbage collect unused code
and so reduce the total library size. A default script `gstreamer-full-default.map`
declares only glib/gstreamer symbols as public.

One can use the `gst-full-plugins` option to pass a list of plugins to be registered
in the gstreamer-full library. The default value is '*' which means that all the plugins selected
during the build process will be registered statically. An empty value will prevent any plugins to
be registered.

One can select a specific set of features with `gst-full-elements`, `gst-full-typefind-functions`, `gst-full-device-providers` or `gst-full-dynamic-types` to select specific feature from a plugin.
When a feature has been listed in one of those options, the other features from its plugin will no longer be automatically included, even if the plugin is listed in `gst-full-plugins`.

The user must insure that all selected plugins and features (element, typefind, etc.) have been
enabled during the build configuration.

To register features, the syntax is the following:
plugins are separated by ';' and features from a plugin starts after ':' and are ',' separated.

As an example:
 * `-Dgst-full-plugins=coreelements;playback;typefindfunctions;alsa;pbtypes`: enable only `coreelements`, `playback`, `typefindfunctions`, `alsa`, `pbtypes` plugins.
 * `-Dgst-full-elements=coreelements:filesrc,fakesink,identity;alsa:alsasrc`: enable only `filesrc`, `identity` and `fakesink` elements from `coreelements` and `alsasrc` element from `alsa` plugin.
 * `-Dgst-full-typefind-functions=typefindfunctions:wav,flv`: enable only typefind func `wav` and `flv` from `typefindfunctions`
 * `-Dgst-full-device-providers=alsa:alsadeviceprovider`: enable `alsadeviceprovider` from `alsa`.
 * `-Dgst-full-dynamic-types=pbtypes:video_multiview_flagset`:  enable `video_multiview_flagset` from `pbtypes

All features from the `playback` plugin will be enabled and the other plugins will be restricted to the specific features requested.

All the selected features will be registered into a dedicated `NULL` plugin name.

This will cause the features/plugins that are not registered to not be included in the final gstreamer-full library.

This is an experimental feature, backward incompatible changes could still be
made in the future.

### Building documentation

Documentation is not built by default because it is slow to generate. To build
the documentation, first ensure that `hotdoc` is installed and `doc` option is
enabled. For API documentation, gobject introspection must also be enabled.
The special target `gst-doc` can then be used to (re)generate the documentation.

```sh
$ pip install hotdoc
$ meson setup -Ddoc=enabled -Dintrospection=enabled builddir
$ meson compile -C builddir gst-doc
```

NOTE: To visualize the documentation, `devhelp` can be run inside the development
environment (see below).

# Development environment

## Development environment target

GStreamer ships a script that drops you into a development environment where
all the plugins, libraries, and tools you just built are available:

```
./gst-env.py
```

Or with a custom builddir (i.e., not `build`, `_build` or `builddir`):

```
./gst-env.py --builddir <BUILDDIR>
```

You can also use `ninja devenv` inside your build directory to achieve the same
effect. However, this may not work on Windows if meson has auto-detected the
visual studio environment.

Alternatively, if you'd rather not start a shell in your workflow, you
can mutate the current environment into a suitable state like so:

```
./gst-env.py --only-environment
```

This will print output suitable for an sh-compatible `eval` function,
just like `ssh-agent -s`.

An external script can be run in development environment with:

```
./gst-env.py external_script.sh
```

NOTE: In the development environment, a fully usable prefix is also configured
in `gstreamer/prefix` where you can install any extra dependency/project.

For more extensive documentation about the development environment go to [the
documentation](https://gstreamer.freedesktop.org/documentation/installing/building-from-source-using-meson.html).

## Custom subprojects

We also added a meson option, `custom_subprojects`, that allows the user
to provide a comma-separated list of meson subprojects that should be built
alongside the default ones.

To use it:

```sh
# Clone into the subprojects directory
$ git -C subprojects clone my_subproject
# Wipe dependency detection state, in case you have an existing build dir
$ meson setup --wipe builddir -Dcustom_subprojects=my_subproject
$ meson compile -C builddir
```

## Run tests

You can easily run the test of all the components:

```
meson test -C build
```

To list all available tests:

```
meson test -C builddir --list
```

To run all the tests of a specific component:

```
meson test -C builddir --suite gst-plugins-base
```

Or to run a specific test file:

```
meson test -C builddir --suite gstreamer gst_gstbuffer
```

Run a specific test from a specific test file:

```
GST_CHECKS=test_subbuffer meson test -C builddir --suite gstreamer gst_gstbuffer
```

## Optional Installation

You can also install everything that is built into a predetermined prefix like
so:

```
meson setup --prefix=/path/to/install/prefix builddir
meson compile -C builddir
meson install -C builddir
```

Note that the installed files have `RPATH` stripped, so you will need to set
`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, or `PATH` as appropriate for your
platform for things to work.


## Add information about GStreamer development environment in your prompt line

### Bash prompt

We automatically handle `bash` and set `$PS1` accordingly.

If the automatic `$PS1` override is not desired (maybe you have a fancy custom
prompt), set the `$GST_BUILD_DISABLE_PS1_OVERRIDE` environment variable to
`TRUE` and use `$GST_ENV` when setting the custom prompt, for example with a
snippet like the following:

```bash
...
if [[ -n "${GST_ENV-}" ]];
then
  PS1+="[ ${GST_ENV} ]"
fi
...
```

### Using powerline

In your powerline theme configuration file (by default in
`{POWERLINE INSTALLATION DIR}/config_files/themes/shell/default.json`)
you should add a new environment segment as follow:

```
{
  "function": "powerline.segments.common.env.environment",
  "args": { "variable": "GST_ENV" },
  "priority": 50
},
```

## Windows Prerequisites Setup

On Windows, some of the components may require special care.

### Git for Windows

Use the [Git for Windows](https://gitforwindows.org/) installer. It will
install a `bash` prompt with basic shell utils and up-to-date git binaries.

During installation, when prompted about `PATH`, you should select the
following option:

![Select "Git from the command line and also from 3rd-party software"](/data/images/git-installer-PATH.png)

### Python 3.8+ on Windows

Use the [official Python installer](https://www.python.org/downloads/windows/).
You must ensure that Python is installed into `PATH`:

![Enable Add Python to PATH, then click Customize Installation](/data/images/py-installer-page1.png)

You may also want to customize the installation and install it into
a system-wide location such as `C:\PythonXY`, but this is not required.

### Ninja on Windows

If you are using Visual Studio 2019 or newer, Ninja is already provided.

In other cases, the easiest way to install Ninja on Windows is with `pip3`,
which will download the compiled binary and place it into the `Scripts`
directory inside your Python installation:

```
pip3 install ninja
```

You can also download the [official release](https://github.com/ninja-build/ninja/releases)
and place it into `PATH`, or use MSYS2.

### Meson on Windows

**IMPORTANT**: Do not use the Meson MSI installer since it is experimental and known to not
work with `GStreamer`.

You can use `pip3` to install Meson, same as Ninja above:

```
pip3 install meson
```

Note that Meson is written entirely in Python, so you can also run it as-is
from the [git repository](https://github.com/mesonbuild/meson/) if you want to
use the latest master branch for some reason.

### Running Meson on Windows

Since version 0.59.0, Meson automatically activates the Visual Studio
environment on Windows if no other compilers (gcc, clang, etc) are found. To
force the use of Visual Studio in such cases, you can use:

```
meson setup --vsenv builddir
```

### Setup a mingw/wine based development environment on linux

#### Install wine and mingw

##### On fedora x64

``` sh
sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-pkg-config mingw64-winpthreads wine
```

FIXME: Figure out what needs to be installed on other distros

#### Get meson from git

This simplifies the process and allows us to use the cross files
defined in meson itself.

``` sh
git clone https://github.com/mesonbuild/meson.git
```

#### Build and install

```
BUILDDIR=$PWD/winebuild/
export WINEPREFIX=$BUILDDIR/wine-prefix/ && mkdir -p $WINEPREFIX
# Setting the prefix is mandatory as it is used to setup symlinks within the development environment
meson/meson.py $BUILDDIR --cross-file meson/cross/linux-mingw-w64-64bit.txt -Dgst-plugins-bad:vulkan=disabled -Dorc:gtk_doc=disabled --prefix=$BUILDDIR/wininstall/ -Djson-glib:gtk_doc=disabled
meson/meson.py install -C $BUILDDIR/
```

> __NOTE__: You should use `meson install -C $BUILDDIR`  each time you make a change
> instead of the usual `meson compile -C $BUILDDIR` as this is not in the
> development environment.

Alternatively, you can also use `mingw64-meson` on Fedora, which is a wrapper
script that sets things up to use Fedora's cross files and settings. However,
the wrapper script can be buggy in some cases.

#### cross-mingw development environment

You can get into the development environment as usual with the gst-env.py
script:

```
./gst-env.py
```

See [above](#development-environment) for more details.

After setting up [binfmt] to use wine for windows binaries,
you can run GStreamer tools under wine by running:

```
gst-launch-1.0.exe videotestsrc ! glimagesink
```

[binfmt]: http://man7.org/linux/man-pages/man5/binfmt.d.5.html
