---
short-description: Setting up a development environment the modern way
authors:
  - name: Edward Hervey
    email: edward@centricular.com
    years: [2020]
...


# Building from source using Meson

**If you just want to use GStreamer, please visit [the download page](https://gstreamer.freedesktop.org/download/).
We provide pre-built binaries for Windows, macOS, Android, and iOS**.

This is the recommended setup for developers who want to work on the GStreamer
code itself and/or modify it, or application developers who wish to quickly try a
feature which isn't yet in a released version of GStreamer.

Note: This only applies for doing GStreamer development on Linux, Windows and
macOS. If you:

1. Want to do GStreamer development for Android, iOS, or UWP, or
1. Have to build GStreamer packages for distribution or deployment, or
1. Need plugins with external dependencies without Meson ports

Please refer to [Building using Cerbero](installing/building-from-source-using-cerbero.md),
which can be used to build a specific GStreamer release or to build unreleased
GStreamer code.

## What are Meson, gst-build and the GStreamer monorepo?

The [Meson build system][meson] is a portable build system which is fast and
meant to be more user friendly than alternatives. It generates build
instructions which can then be executed by [`ninja`][ninja]. The GStreamer
project uses it for all subprojects.

In September 2021 all of the main GStreamer modules were merged into a
single code repository, the GStreamer [mono repo][monorepo-faq] which lives
in the main [GStreamer git repository][gstreamer], and this is where all
GStreamer development happens nowadays for GStreamer version 1.19/1.20 and later.

Before the mono repository merge the different GStreamer modules lived in
separate git repositories and there was a separate meta-builder project
called [`gst-build`][gst-build] to download and build all the subprojects.
This is what you should use if you want to build or develop against older
stable branches such as GStreamer 1.16 or 1.18.

If you want to build or develop against upcoming development or stable branches
you should use the `main` branch of the GStreamer module containing the mono
repository.

In the following sections we will only talk about the GStreamer mono repo,
but `gst-build` works pretty much the same way, the only difference being
that it would download the various GStreamer submodules as well.

[monorepo-faq]: https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/mono-repository.html

## Setting up the build with Meson

In order to build the current GStreamer development version, which will become
the 1.20 stable branch in the near future, clone the GStreamer mono repository:
``` shell
git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git
cd gstreamer
```

Or if you have developer access to the repositories:
``` shell
git clone git@gitlab.freedesktop.org:gstreamer/gstreamer.git
cd gstreamer
```
If you want to build the stable 1.18 or 1.16 branches, clone `gst-build`:

``` shell
git clone https://gitlab.freedesktop.org/gstreamer/gst-build.git
cd gst-build
```

### Repository layout

The repository contains a few notable scripts and directories:
1. `meson.build` is the top-level build definition which will recursively
   configure all dependencies. It also defines some helper commands allowing you
   to have a development environment or easily update git
   repositories for the GStreamer modules.
2. `subprojects/` is the directory containing GStreamer modules and
   a selection of dependencies.


## Basic meson and ninja usage

Configuring a module (or several in one go when in gst-build) is done by
executing:

``` shell
meson setup <build_directory>
```

The `build_directory` is where all the build instructions and output will be
located (This is also called *"out of directory"* building). If the directory is
not created it will be done so at this point. Note that older versions of `meson`
could run without any *command* argument, this is now deprecated.

There is only one restriction regarding the location of the `build_directory`:
it can't be the same as the source directory (i.e. where you cloned your module).
It can be outside of that directory or below/within that directory though.

Once meson is done configuring, you can either:

1. enter the specified build directory and run ninja.

   ``` shell
   cd <build_directory>
   ninja
   ``` 
2. *or* instead of switching to the build directory every time you wish to
   execute `ninja` commands, you can just specify the build directory as an
   argument. The advantage of this option is that you can run it from anywhere
   (instead of changing to the ninja directory)
   
   ``` shell
   ninja -C </path/to/build_directory>
   ```

This will build everything from that module (and subprojects if building
gst-build or the mono repository).

Note: You do not need to re-run `meson` when you modify source files, you just
need to re-run `ninja`. If you build/configuration files changed, `ninja` will
figure out on its own that `meson` needs to be re-run and will do that
automatically.


## Entering the development environment

GStreamer is made of several tools, plugins and components. In order to make it
easier for development and testing, there is a target (provided by `gst-build`
or the mono repository, and in future directly by `meson` itself) which will
setup environment variables accordingly so that you can use all the
build results directly.

For anyone familiar with python and virtualenv, you will feel right at home.

The script is called **`gst-env.py`**, it is located in the root of the
GStreamer mono-repository.

Another way to enter the virtual environment is to execute **`ninja -C
<path/to/build_directory> devenv`**. This option has less options and is
therefore not compatible with some workflows.

*NOTE*: you cannot use `meson` or reconfigure with `ninja` within the virtual
environment, therefore build before entering the environment or build from
another terminal/terminal-tab.

### How does it work?

Start a new shell session with a specific set of environment variables, that
tell GStreamer where to find plugins or libraries.

The most important options are:

+ **Shell context related variables**
  * *PATH* - System path used to search for executable files, `gst-env` will
    append folders containing executables from the build directory.
  * *GST_PLUGIN_PATH* - List of paths to search for plugins (`.so`/`.dll`
    files), `gst-env` will add all plugins found within the
    `GstPluginsPath.json` file and from a few other locations.
  * *GST_PLUGIN_SYSTEM_PATH*  - When set this will make GStreamer check for
    plugins in system wide paths, this is kept blank on purpose by `gst-env` to
    avoid using plugins installed outside the environment.
  * *GST_REGISTRY* - Use a custom file as plugin cache / registry. `gst-env`
    utilizes the one found in the given build directory.
+ **Meson (build environment) related variables**
  * *GST_VERSION* - Sets the build version in meson.
  * *GST_ENV* - Makes sure that neither meson or ninja are run from within the
    `gst-env`. Can be used to identify if the environment is active.
+ **Validation (test runners) related variables**
  * *GST_VALIDATE_SCENARIOS_PATH* - List of paths to search for validation
    scenario files (list of actions to be executed by the pipeline). By default
    `gst-env` will use all scenarious found in the
    `prefix/share/gstreamer-1.0/validate/scenarios` directory within the parent
    directory of `gst-env.py`.
  * *GST_VALIDATE_PLUGIN_PATH* - List of paths to search for plugin files to
    add to the plugin registry. The default search path is in the given build
    directory under `subprojects/gst-devtools/validate/plugins`.

The general idea is to set up the meson build directory, build the project and
the switch to the development environment with `gst-env`. This creates a
development environment in your shell, that provides a separate set of plugins
and tools.
To check if you are in the development environment run: `echo
$GST_ENV`, which will be set by `gst_env` to `gst-$GST_VERSION`.

You will notice the prompt changed accordingly. You can then run any GStreamer
tool you just built directly (like `gst-inspect-1.0`, `gst-launch-1.0`, ...).

### Options `gst-env`

+ **builddir**
  - By default, the script will try to find the build directory within the
    `build` or `builddir` directory within the same folder where
    `gst-env.py` are located. This option allows to specify
    a different location. This might be useful when you have multiple different
    builds but you don't want to jump between folders.
+ **srcdir**
  - The parent folder of `gst-env.py` is used by default.
    This option is used to get the current branch of the repository, to fetch
    GstValidate plugins and for gdb.
+ **sysroot**
  - Useful if the project was cross-compiled on another machine and mounted via
    a network file system/ssh file system/etc. Adjusts the paths (e.g. the
    paths found in *GST_PLUGIN_PATH*) by removing the front part of the path
    which matches *sysroot*.

  <sub>For example if your rootfs is at /srv/rootfs, then the v4l2codecs plugin
  might be built at
  `/srv/rootfs/home/user/gstreamer/build/subprojects/gst-plugins-bad/sys/v4l2codecs`.
  By executing `gst-env.py --sysroot /srv/rootfs` the path will be stored
  within *GST_PLUGIN_PATH* as:
  `/home/user/gstreamer/build/subprojects/gst-plugins-bad/sys/v4l2codecs`.</sub>

+ **wine**
  - Extend the GST_VERSION environment variable with a specific wine command
+ **winepath**
  - Add additional elements to the WINEPATH variable for wine environments.
+ **only-environment**
  - Instead of opening a new shell environment, print the environment variables
    that would be used.

### Use cases

#### Setting up a development environment while keeping the distribution package

This case is very simple all you have to do is either:

- `./gst-env.py` from the project root
- `ninja -C build devenv` (build is the generated meson build directory)
- `meson devenv` from the meson build directory (e.g. `build`) within the
  project root

#### Using GStreamer as sub project to another project

This case is very similar to the previous, the only important deviation is that
the file system structure is important. **`gst-env`** will look for a
`GstPluginPaths.json` file either within the meson build directory (e.g.
`build`) or within `build/subprojects/gstreamer`.

#### Cross-compiling in combination with a network share

For cross compiling in general take a look at the [meson
documentation](https://mesonbuild.com/Cross-compilation.html) or at projects
like [gst-build-sdk](https://gitlab.collabora.com/collabora/gst-build-sdk).

The basic idea is to prepare a rootfs on the cross compile host, that is
similar to that of target machine, prepare a
[cross-file.txt](https://mesonbuild.com/Cross-compilation.html#defining-the-environment),
build the project and export it via a [NFS mount/NFS
rootfs](https://wiki.archlinux.org/title/NFS)/[SSHFS](https://wiki.archlinux.org/title/SSHFS)/[Syncthing](https://wiki.archlinux.org/title/Syncthing)
etc.

On the target machine you then have to remove the path to the rootfs on the
build machine from the GStreamer paths:

- `./gst-env.py --sysroot /path/to/rootfs-on-cross-compile-host`

## Working with multiple branches or remotes

It is not uncommon to track multiple git remote repositories (such as the
official upstream repositories and your personal clone on gitlab).

In the gstreamer mono repository, just add your personal git remotes as you
would do with any other git repository, e.g.:

``` shell
git remote add personal git@gitlab.freedesktop.org:awesomehacker/gstreamer.git
git fetch
```

In gst-build (for 1.16/1.18 branches), you can add your personal
git remotes in the relevant subproject directory (and that would have to be
done for each subproject of interest, since the old 1.16/1.18 branches live in
separate git repositories), e.g.:

``` shell
cd subprojects/gstreamer/
git remote add personal git@gitlab.freedesktop.org:awesomehacker/gstreamer.git
git fetch
```


## Configuration

You can list all the available options of a `meson` project by using the
configure command:

``` shell
meson configure
```

If you have an already configured build directory, you can provide that and you
will additionally get the configured values for that build:

``` shell
meson configure <build-directory>
```

That command will list for each option:
* The name of the option
* The default (or configured) value of the option
* The possible values
* The description of that option

> The values with `auto` mean that `meson` will figure out at configuration time
> the proper value (for example, if you have the available development packages
> to build a certain plugin).
>
> You will also see values with `<inherited from main project>`. This is mostly
> used for options which are generic options. For example the `doc` option is
> present at the top-level, and also on every submodules (ex:
> `gstreamer:doc`). Generally you only want to set the value of that option
> once, and all submodules will inherit from that.

You can then provide those options to `meson` when configuring the build with
`-D<option_name>=<option_value>`. For example, if one does not want to build the
rust plugins in `gst-build` (`rs` option), you would do:

``` shell
meson -Drs=disabled <build-directory>
```

You can also peek at the `meson_options.txt` files and `subproject/xyz/meson_options.txt`
files which is where the various project specific build options are listed.
These do not include all the standard Meson options however.

## Running tests

Running the unit tests is done by calling `meson test` from the build directory,
or `meson test -C <path/to/build_directory>`. If there are any failures you can
have a look at the file specified at the end or you can run `meson test
--print-errorlogs` which will show you the logs of the failing test after
execution.

You can also execute just a subset of tests by specifying the name name. For
example `meson test gst_gstpad`. The complete list of tests is available with
`meson test --list`.

If the `gst-devtools` submodule is built, you can also use
`gst-validate-launcher`[gst-validate] for running tests.

``` shell
gst-validate-launcher check.gst*
```

## Going further

More details are available in the [GStreamer mono repo README](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/README.md)
or (for the older 1.16/1.18 branches) in the [gst-build documentation](https://gitlab.freedesktop.org/gstreamer/gst-build/blob/master/README.md).

  [meson]: https://mesonbuild.com/
  [ninja]: https://ninja-build.org/
  [gstreamer]: https://gitlab.freedesktop.org/gstreamer/gstreamer/
  [gst-build]: https://gitlab.freedesktop.org/gstreamer/gst-build/
  [gst-validate]: https://gstreamer.freedesktop.org/documentation/gst-devtools/
