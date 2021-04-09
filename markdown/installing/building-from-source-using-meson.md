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

## What are meson and gst-build?

The [Meson build system][meson] is a portable build system which is fast and
meant to be more user friendly than alternatives. It generates build
instructions which can then be executed by [`ninja`][ninja]. The GStreamer
project uses it for all subprojects.

Since GStreamer has many components and has many dependencies, the
[`gst-build`][gst-build] module contains multiple python3 scripts to simplify
downloading and configuring everything using Meson. It uses a feature from meson
which allows defining subprojects and you can therefore configure and build the
GStreamer modules and certain dependencies in one go.


## Setting up gst-build

First clone `gst-build`:

``` shell
git clone https://gitlab.freedesktop.org/gstreamer/gst-build.git
cd gst-build
```

Or if you have developer access to the repositories:
``` shell
git clone git@gitlab.freedesktop.org:gstreamer/gst-build.git
cd gst-build
```

### Layout of gst-build

gst-build contains a few notable scripts and directories:
1. `meson.build` is the top-level build definition which will recursively
   configure all dependencies. It also defines some helper commands allowing you
   to have an uninstalled development environment or easily update git
   repositories for the GStreamer modules.
 2. `subprojects/` is the directory containing GStreamer modules and
   a selection of dependencies.



## Basic meson and ninja usage

Configuring a module (or several in one go when in gst-build) is done by
executing:

``` shell
meson <build_directory>
```

The `build_directory` is where all the build instructions and output will be
located (This is also called *"out of directory"* building). If the directory is
not created it will be done so at this point. Note that calling `meson` without
any *command* argument is implicitely calling the `meson setup` command (i.e. to
do the initial configuration of a project).

There is only one restriction regarding the location of the `build_directory`:
it can't be the same as the source directory (i.e. where you cloned your module
or gst-build). It can be outside of that directory or below/within that
directory though.

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

This will build everything from that module (and subprojects if within
gst-build).

Note: You do not need to re-run `meson` when you modify source files, you just
need to re-run `ninja`. If you build/configuration files changed, `ninja` will
figure out on its own that `meson` needs to be re-run and will do that
automatically.


## Entering the "uninstalled" environment

GStreamer is made of several tools, plugins and components. In order to make it
easier for development and testing, there is a target (provided by `gst-build`)
which will setup environment variables accordingly so that you can use all the
build results directly.

``` shell
ninja -C <path/to/build_directory> devenv
```

You will notice the prompt changed accordingly. You can then run any GStreamer
tool you just built directly (like `gst-inspect-1.0`, `gst-launch-1.0`, ...).


## Working with multiple branches or remotes

It is not uncommon to track multiple git remote repositories (such as the
official upstream repositories and your personal clone on gitlab).

You can do so by adding your personal git remotes in the subproject directory:

``` shell
cd subprojects/gstreamer/
git remote add personal git@gitlab.freedesktop.org:awesomehacker/gstreamer.git
git fetch
```

## Configuration of gst-build

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

More details are available in the [gst-build
documentation](https://gitlab.freedesktop.org/gstreamer/gst-build/blob/master/README.md).

  [meson]: https://mesonbuild.com/
  [ninja]: https://ninja-build.org/
  [gst-build]: https://gitlab.freedesktop.org/gstreamer/gst-build/
  [gst-validate]: https://gstreamer.freedesktop.org/documentation/gst-devtools/
