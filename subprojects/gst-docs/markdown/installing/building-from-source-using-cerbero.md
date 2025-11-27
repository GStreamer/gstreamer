---
short-description: Setting up a development environment the modern way
authors:
  - name: Nirbheek Chauhan
    email: nirbheek@centricular.com
    years: [2021]
...


# Building from source using Cerbero

**If you just want to use GStreamer, please visit [the download page](https://gstreamer.freedesktop.org/download/).
We provide pre-built binaries for Windows, macOS, Android, and iOS**.

Cerbero is a cross-platform build aggregator for Open Source projects that
builds and creates native packages for different platforms, architectures and
distributions. It supports both native compilation and cross compilation and
can run on macOS, Linux, and Windows.

You should use Cerbero to build GStreamer if you:

1. Want to do GStreamer development for Android, iOS, or UWP, or
1. Have to build GStreamer packages for distribution or deployment, or
1. Need plugins with external dependencies without Meson ports

However, if you are a developer who wants to work on the GStreamer code itself
on Linux, Windows, or macOS, it is much more convenient to use gst-build.
Please refer to [Building using Meson](installing/building-from-source-using-meson.md).

## Minimum Requirements

Cerbero provides bootstrapping facilities for all platforms, but it still needs a
minimum base to bootstrap on top of.

### Linux Setup

On Linux, you will only need a distribution with python >= 3.6. Cerbero will
use your package manager to install all other required packages during
[bootstrap](#bootstrap-to-setup-environment).

### macOS Setup

On macOS we use the Python 3 that ships with Xcode, so you only need to install
Xcode Command Line Tools.

Cerbero will build all other required packages during
[bootstrap](#bootstrap-to-setup-environment). You do **not** need anything from
Homebrew or MacPorts, and in fact having too many packages installed in either
of those can cause problems.

### Windows Setup

The initial setup on Windows is automated with an in-tree PowerShell script. It
will auto-detect and installs the necessary tools with [Chocolatey](https://chocolatey.org/):

* Visual Studio 2019 or newer Build Tools
* CMake
* MSYS2
* Git
* Python 3.11 or newer
* WiX 5.0.1 or higher

Start an admin PowerShell and run:

```powershell
# Enable running scripts
$ Set-ExecutionPolicy -ExecutionPolicy Unrestricted

# Run the bootstrap script
$ .\tools\bootstrap-windows.ps1
```

You may specify the Visual Studio version to install by passing the `-VSVersion`
parameter with either `2019` or `2022`. If not specified, you will be prompted to
choose one during the bootstrap process.

```powershell
# Run the bootstrap script specifying Visual Studio 2022
$ .\tools\bootstrap-windows.ps1 -VSVersion '2022'
```

**IMPORTANT:** Using cerbero on Windows with the [GCC/MinGW
toolchain](https://gitlab.freedesktop.org/gstreamer/cerbero/-/blob/master/docs/toolchains.md#windows)
requires a 64-bit operating system. The toolchain is only available for 64-bit
and it can produce 32-bit or 64-bit binaries.

#### Important Windows-specific Notes

* You should add the cerbero git directory to the list of excluded folders in your
  anti-virus, or you will get random build failures when Autotools does file
  operations such as renames and deletions. It will also slow your build by
  about 3-4x.

* If you are using Windows 10, it is also highly recommended to enable "Developer
  Mode" in Windows Settings as shown below.

  ![Enable Developer Mode in Windows Settings](images/cerbero/windows-settings-developer-mode.png)

## Download the sources

To build GStreamer using Cerbero, you first need to download **Cerbero**:

```sh
$ git clone https://gitlab.freedesktop.org/gstreamer/cerbero
```

This will build the latest unreleased GStreamer code.

Despite the presence of `setup.py` this tool does not need installation. It is
invoked via the `cerbero-uninstalled` script, which should be invoked as
`./cerbero-uninstalled`, or you can create an alias to it in your `.bashrc`
file.

You can build a specific release by checking out that tag, for example `git
checkout 1.18.4`. Building a release tag will cause Cerbero to use the release
tarballs instead of git repositories when fetching gstreamer recipes for
building.

You can also build the latest unreleased 'stable branch' code, for instance for
1.18 you'd do: `git checkout 1.18`, or `git clone -b 1.18 [...]`, which will
fetch the corresponding stable branches when building gstreamer recipes.

You can also use git worktrees, which may be more convenient when building
several different versions of gstreamer since the build artefacts always go
into the `build` directory inside the git repository.

# Running Cerbero

Despite the presence of `setup.py` this tool does not need installation. It is invoked via the
cerbero-uninstalled script, which should be invoked as `./cerbero-uninstalled`, or you can add
the cerbero directory in your path and invoke it as `cerbero-uninstalled`.

On Windows, Cerbero supports running from CMD.EXE, Powershell, or an MSYS2 UCRT64 shell.

## Bootstrap to setup environment

Before using cerbero for the first time, you will need to run the bootstrap
command. This command installs the missing parts of the build system using the
packages manager when available, and also downloads the necessary toolchains
when building for Windows/MinGW or Android.

Note that this will take a while (a couple hours or even more on Windows).

```sh
$ ./cerbero-uninstalled bootstrap
```

On Linux and macOS, this will use `sudo` to make changes to the system.

The bootstrap process will then install or build all packages required to build
GStreamer.

## Build GStreamer

To generate GStreamer binaries, use the following command:

```sh
$ ./cerbero-uninstalled package gstreamer-1.0
```

This will fetch and build all required GStreamer components and create packages
for your distribution, then place them in the Cerbero source directory.

A list of supported packages to build can be retrieved using:

```sh
$ ./cerbero-uninstalled list-packages
```

Packages are composed of 0 (in case of a meta package) or more
components that can be built separately if desired. The components are
defined as individual recipes and can be listed with:

```sh
$ ./cerbero-uninstalled list
```

To build an individual recipe and its dependencies, do the following:

```sh
$ ./cerbero-uninstalled build <recipe_name>
```

Or to build or force a rebuild of a recipe without building its
dependencies use:

```sh
$ ./cerbero-uninstalled buildone <recipe_name>
```

To wipe everything and start from scratch:

```sh
$ ./cerbero-uninstalled wipe
```

Once built, the binaries built by all the recipes will be installed inside
a auto-detected prefix inside the `build` directory in the Cerbero source tree.

## Cross Compilation

If you're using Cerbero to cross-compile to iOS, Android, Cross-MinGW, or UWP,
you must select the appropriate config file and pass it to all steps:
bootstrap, build, package, etc.

For example if you're on Linux and you want to build for Android Universal, you
must run:

```sh
# Bootstrap for Android Universal on Linux
$ ./cerbero-uninstalled -c config/cross-android-universal.cbc bootstrap

# Build everything and package for Android Universal
$ ./cerbero-uninstalled -c config/cross-android-universal.cbc package gstreamer-1.0
```

Here's a list of config files for each target machine:

#### Linux Targets

| Target            | Config file |
|-------------------|-------------|
| MinGW 32-bit      | `cross-win32.cbc` |
| MinGW 64-bit      | `cross-win64.cbc` |
| Android Universal | `cross-android-universal.cbc` |
| Android ARM64     | `cross-android-arm64.cbc` |
| Android ARMv7     | `cross-android-armv7.cbc` |
| Android x86       | `cross-android-x86.cbc` |
| Android x86_64    | `cross-android-x86-64.cbc` |

#### macOS Targets

| Target                 | Config file |
|------------------------|-------------|
| macOS System Framework | `osx-x86-64.cbc` |
| iOS Universal          | `cross-ios-universal.cbc` |
| iOS ARM64              | `cross-ios-arm64.cbc` |
| iOS ARMv7              | `cross-ios-armv7.cbc` |
| iOS x86                | `cross-ios-x86.cbc` |
| iOS x86_64             | `cross-ios-x86-64.cbc` |

#### Windows Targets

On Windows, config files are used to select the architecture and variants are
used to select the toolchain (MinGW, MSVC, UWP):

Target          | Config file               | Variant
:---------------|:--------------------------|:-------
MSVC x86        | `win32.cbc`               |
MSVC x86_64     | `win64.cbc`               |
MinGW x86       | `win32.cbc`               | mingw
MinGW x86_64    | `win64.cbc`               | mingw
UWP x86         | `win32.cbc`               | uwp
UWP x86_64      | `win64.cbc`               | uwp
UWP ARM64       | `cross-win-arm64.cbc`     | uwp
UWP Universal   | `cross-uwp-universal.cbc` | (implicitly uwp)

Example usage:

```sh
# Target MSVC 64-bit
$ ./cerbero-uninstalled -c config/win64.cbc package gstreamer-1.0

# Target MinGW 32-bit
$ ./cerbero-uninstalled -c config/win32.cbc -v mingw package gstreamer-1.0

# Target UWP, x86_64
$ ./cerbero-uninstalled -c config/win64.cbc -v uwp package gstreamer-1.0

# Target UWP, Cross ARM64
$ ./cerbero-uninstalled -c config/cross-win-arm64.cbc -v uwp package gstreamer-1.0

# Target UWP, All Supported Arches
$ ./cerbero-uninstalled -c config/cross-uwp-universal.cbc package gstreamer-1.0
```

## Tips for CI setup

Cerbero can split its bootstrap and package commands into stages which can be
useful for CI setups. For example, you might want to do any system setup (such
as installing packages) and also fetch all sources to cache them when building
your CI image:

```sh
$ ./cerbero-uninstalled fetch-bootstrap
$ ./cerbero-uninstalled fetch-package gstreamer-1.0
# This will use "sudo"
$ ./cerbero-uninstalled bootstrap --system=yes --toolchains=no --build-tools=no --offline
```

Then inside your CI job, you will not need root for the remaining steps:

```sh
$ ./cerbero-uninstalled bootstrap --system=no --toolchains=yes --build-tools=yes --offline
# When building a non-tagged commit, this will update the git repos for all gstreamer recipes
$ ./cerbero-uninstalled fetch-package gstreamer-1.0
$ ./cerbero-uninstalled package gstreamer-1.0 --offline
```

For more inspiration, see [GStreamer's GitLab CI setup](https://gitlab.freedesktop.org/gstreamer/cerbero/.gitlab-ci.yml).

# Artifact Types

Starting with the 1.28 release, Cerbero supports outputting various kinds of
artifact types:

* Tarballs
  - Default for Android and Linux, available for all targets
  - Prior to 1.28, this could be selected with the `-t/--tarball` option
* Windows Installer Packages
  - Default for, and only available for Windows and cross-Windows targets
* macOS Installer Packages
  - Default for, and only available for macOS and iOS targets
* Python 3 Wheels
  - Available for macOS and Windows
  - The minimum supported Python version for the wheel will be whatever Python
    version you build the wheel with. The oldest supported by Cerbero is 3.9.

The artifact to output can be controlled with the `--artifact` option. Valid
choices are: `tarball` `msi` `pkg` `wheel`.

## Enabling Optional Features with Variants

Cerbero controls optional and platform-specific features with `variants`. You
can see a full list of available variants by running:

```sh
$ ./cerbero-uninstalled --list-variants
```

Some variants are enabled by default while others are not. You can enable
a particular variant by doing one of the following:

* Either invoke `cerbero-uninstalled` with the `-v` argument, for example:

```sh
$ ./cerbero-uninstalled -v variantname [-c ...] package gstreamer-1.0
```

* Or, edit `~/.cerbero/cerbero.cbc` and add `variants = ['variantname']` at the
  bottom. Create the file if it doesn't exist.

Multiple variants can either be separated by a comma or with multiple `-v`
arguments, for example the following are equivalent:

```sh
$ ./cerbero-uninstalled -v variantname1,variantname2 [-c ...] package gstreamer-1.0
$ ./cerbero-uninstalled -v variantname1 -v variantname2 [-c ...] package gstreamer-1.0
```

To explicitly **disable** a variant, use `novariantname` instead,
e.g. `-v norust` to disable Rust support and building of the Rust plugins.

In the case of multiple enabling/disable of the same variant, then the last
condition on the command line will take effect.  e.g. if novariantname is last
then variantname is disabled.

### Enabling Qt5 or Qt6 Support

Starting with version 1.15.2, Cerbero has built-in support for building the Qt5
QML GStreamer plugin. You can toggle that on by
[enabling the `qt5` variant](#enabling-optional-features-with-variants).

You must also tell Cerbero where your Qt5 installation prefix is. You can do it
by setting the `QMAKE` environment variable to point to the `qmake` that you
want to use, f.ex. `/path/to/Qt5.12.0/5.12.0/ios/bin/qmake`

When building for Android Universal with Qt < 5.14, instead of `QMAKE`, you
**must** set the `QT5_PREFIX` environment variable pointed to the directory
inside your prefix which contains all the android targets, f.ex.
`/path/to/Qt5.12.0/5.12.0`.

Next, run `package`:

```sh
$ export QMAKE='/path/to/Qt5.12.0/5.12.0/<target>/bin/qmake'
$ ./cerbero-uninstalled -v qt5 [-c ...] package gstreamer-1.0
```

This will try to build the Qt5 QML plugin and error out if Qt5 could not be
found or if the plugin could not be built. The plugin will be automatically
added to the package outputted.

Similarly, if you set `QMAKE6` and enable the `qt6` variant, the same will be
done for Qt6.

**NOTE:** The package outputted will not contain a copy of the Qt libraries in
it. You must link to them while building your app yourself.

## Enabling or Disabling Rust / Cargo Support

Starting with version 1.22, Cerbero supports bootstrapping Rust toolchains, and
can build Rust gstreamer plugins.

This is enabled by default for the following configurations: native-linux,
cross-macos-universal, native-win64 (msvc), native-win32 (msvc).

More targets will be enabled in the future. If you want to force-enable the
feature, you can use the [`rust` variant](#enabling-optional-features-with-variants)
by invoking cerbero as follows:

```
$ ./cerbero-uninstalled -v rust <command>
```

To explicitly **disable** Rust support and building of the Rust plugins,
pass `-v norust`.

## Enabling Hardware Codec Support

Starting with version 1.15.2, Cerbero has built-in support for building and
packaging hardware codecs for Intel and Nvidia. If the appropriate variant is
enabled, the plugin will either be built or Cerbero will error out if that's
not possible.

### Intel Hardware Codecs

For Intel, the [variant to enable](#enabling-optional-features-with-variants)
is `intelmsdk` which will build the `msdk` plugin.

You must set the `INTELMEDIASDKROOT` env var to point to your [Intel Media
SDK](https://software.intel.com/en-us/media-sdk) prefix, or you must have the
SDK's pkgconfig prefix in `PKG_CONFIG_PATH`

On Windows, `INTELMEDIASDKROOT` automatically set by the installer. On Linux,
if you need to set this, you must set it to point to the directory that
contains the mediasdk `include` and `lib64` dirs.

For VA-API, the [variant to enable](#enabling-optional-features-with-variants)
is `vaapi` which will build the va plugins from gst-plugins-bad.

## Customising Cerbero

### How to build a custom GStreamer repository or branch

Create a `localconf.cbc` file and add the following:

```
# Set custom remote and branch for all gstreamer recipes
recipes_remotes = {'gstreamer-1.0': {'custom-remote': '<YOUR_GIT_REPO>'}}
recipes_commits = {'gstreamer-1.0': 'custom-remote/<YOUR_GIT_BRANCH>'}
```

You can then run Cerbero with e.g.:

```cmd
./cerbero-uninstalled -c localconf.cbc -c config/win64.cbc -v visualstudio package gstreamer-1.0
```

This works for all builds of course, not only the Windows one.

### How to force a specific Visual Studio version

Create a `localconf.cbc` file and add the following:

```
# Specify Visual Studio install path and version
vs_install_path = 'C:/Path/To/Install'

# This is the Visual Studio Compiler toolset version, vs16 is for Visual Studio 2019. vs15 is 2017, and so on
vs_install_version = 'vs16'
```
You can then run Cerbero with e.g.:

```cmd
./cerbero-uninstalled -c localconf.cbc -c config/win64.cbc -v visualstudio package gstreamer-1.0
```
