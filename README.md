# gst-build

GStreamer [meson](http://mesonbuild.com/) based repositories aggregrator

You can build GStreamer and all its modules at once using
meson and its [subproject](https://github.com/mesonbuild/meson/wiki/Subprojects) feature.

## Getting started

### Install meson and ninja

You should get meson through your package manager or using:

  $ pip3 install --user meson

You should get `ninja` using your package manager or downloading it from
[here](https://github.com/ninja-build/ninja/releases).

### Build GStreamer and its modules

You can get all GStreamer built running:

```
mkdir build/ && meson build && ninja -C build/
```

NOTE: on fedora (and maybe other distributions) replace `ninja` with `ninja-build`

# Development environment

## Uninstalled environment

gst-build also contains a special `uninstalled` target that lets you enter an
uninstalled development environment where you will be able to work on GStreamer
easily. You can get into that environment running:

```
ninja -C build/ uninstalled
```

If your operating system handles symlinks, built modules source code will be
available at the root of `gst-build/` for example GStreamer core will be in
`gstreamer/`. Otherwise they will be present in `subprojects/`. You can simply
hack in there and to rebuild you just need to rerun `ninja -C build/`.

## Update git subprojects

We added a special `update` target to update subprojects (it uses `git pull
--rebase` meaning you should always make sure the branches you work on are
following the right upstream branch, you can set it with `git branch
--set-upstream-to origin/master` if you are working on `gst-build` master
branch).

Update all GStreamer modules and rebuild:

```
ninja -C build/ update
```

Update all GStreamer modules without rebuilding:

```
ninja -C build/ git-update
```

## Run tests

You can easily run the test of all the components:

```
mesontest -C build
```

To list all available tests:

```
mesontest -C build --list
```

To run all the tests of a specific component:

```
mesontest -C build --suite gst-plugins-base
```

Or to run a specific test:

```
mesontest -C build/ --suite gstreamer gst/gstbuffer
```

## Add information about GStreamer development environment in your prompt line

### Bash prompt

We automatically handle `bash` and set `$PS1` accordingly

### Zsh prompt

In your `.zshrc`, you should add something like:

```
export PROMPT="$GST_ENV-$PROMPT"
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
