# gst-build

GStreamer [meson](http://mesonbuild.com/) based repositories aggregrator

You can build GStreamer and all its modules at once using
meson and its [subproject](https://github.com/mesonbuild/meson/wiki/Subprojects) feature.

## Getting started

### Install meson and ninja

Meson 0.48 or newer is required.

You should get meson through your package manager or using:

  $ pip3 install --user meson

This will install meson into ~/.local/bin which may or may not be included
automatically in your PATH by default.

If you are building on Windows, do not use the Meson MSI installer since it is
experimental and will likely not work.

You can also run meson directly from a meson git checkout if you like.

You should get `ninja` using your package manager or download the [official
release](https://github.com/ninja-build/ninja/releases) and put it in your PATH.

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

NOTE: In the uninstalled environment, a fully usable prefix is also configured
in `gst-build/prefix` where you can install any extra dependency/project.

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

## Custom subprojects

We also added a meson option, 'custom_subprojects', that allows the user
to provide a comma-separated list of subprojects that should be built
alongside the default ones.

To use it:

```
cd subprojects
git clone my_subproject
cd ../build
rm -rf * && meson .. -Dcustom_subprojects=my_subproject
ninja
```


## Run tests

You can easily run the test of all the components:

```
meson test -C build
```

To list all available tests:

```
meson test -C build --list
```

To run all the tests of a specific component:

```
meson test -C build --suite gst-plugins-base
```

Or to run a specific test file:

```
meson test -C build/ --suite gstreamer gst_gstbuffer
```

Run a specific test from a specific test file:

```
GST_CHECKS=test_subbuffer meson test -C build/ --suite gstreamer gst_gstbuffer
```

## Checkout another branch using worktrees

If you need to have several versions of GStreamer coexisting (eg. `master` and `1.14`),
you can use the `checkout-branch-worktree` script provided by `gst-build`. It allows you
to create a new `gst-build` environment with new checkout of all the GStreamer modules as
[git worktrees](https://git-scm.com/docs/git-worktree).

For example to get a fresh checkout of `gst-1.14` from a `gst-build` in master already
built in a `build` directory you can simply run:

```
./checkout-branch-worktree ../gst-1.14 1.14 -C build/
```

## Add information about GStreamer development environment in your prompt line

### Bash prompt

We automatically handle `bash` and set `$PS1` accordingly.

If the automatic `$PS1` override is not desired (maybe you have a fancy custom prompt), set the `$GST_BUILD_DISABLE_PS1_OVERRIDE` environment variable to `TRUE` and use `$GST_ENV` when setting the custom prompt, for example with a snippet like the following:

```bash
...
if [[ -n "${GST_ENV-}" ]];
then
  PS1+="[ ${GST_ENV} ]"
fi
...

```

### Zsh prompt

In your `.zshrc`, you should add something like:

```
export PROMPT="$GST_ENV-$PROMPT"
```

### Fish prompt

In your `~/.config/fish/functions/fish_prompt.fish`, you should add something like this at the end of the fish_prompt function body:

```
if set -q GST_ENV
  echo -n -s (set_color -b blue white) "(" (basename "$GST_ENV") ")" (set_color normal) " "
end
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
