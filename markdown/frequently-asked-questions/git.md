# Building GStreamer from git

## Is there a way to test or develop against GStreamer from git without interfering with my system GStreamer installed from packages?

Yes! You have two options: you can either run GStreamer in an
uninstalled setup (see [How do I develop against an uninstalled
GStreamer copy?](#developing-uninstalled-gstreamer)), or you can use
GNOME's jhbuild.

## How do I check out GStreamer from git?

GStreamer and its various official modules are hosted on Freedesktop.org. For
starters, you will likely be interested in the core `gstreamer` module and the
basic base functionality provided by the `gstreamer-plugins-base` and
`gstreamer-plugins-good` modules. Additionally, and in case you want more
comprehensive media format support, you might want to check out the
`gst-plugins-ugly`, `gst-plugins-bad` and `gst-ffmpeg` modules.

You can use the following command to download the latest source code for the
base modules:

```
for module in gstreamer gst-plugins-base gst-plugins-good; do
  git clone git://anongit.freedesktop.org/git/gstreamer/$module ;
done
```

This will create three directories in your current directory: `gstreamer`,
`gst-plugins-base`, and `gst-plugins-good`. If you want to get another module,
use the above `git clone` command line and replace `$module` with the name of
the module. Then you will need to go into each directory and run `./autogen.sh`,
this will, among other things, checkout the `common` module underneath each
module checkout.

You can get a list of active modules and their description from the
[modules page](http://gstreamer.freedesktop.org/modules/).

## How do I get developer access to GStreamer git?

If you want to gain developer access to the GStreamer source-code repositories,
you need to either send a request to the development lists, or directly ask one
of the maintainers. We usually only consider requests by developers who have
been active for some time and have shown to be competent GStreamer contributors.
If you are not already a registered developer with a user account on
Freedesktop.org, you will have to provide them with:

1.  your desired username

2.  your full name

3.  your e-mail address

4.  a copy of your public `sshv2` identity. If you do not have this yet,
    you can generate one by running `ssh-keygen -t rsa -f
    ~/.ssh/id_rsa.pub-fdo`. The resulting public key will be left in
    `~/.ssh/id_rsa.pub-fdo`

5.  your GPG fingerprint. This will allow you to add and remove `ssh` keys to
    your account.

Once you have all these items, review
<http://freedesktop.org/wiki/AccountRequests> for instructions on what to do
with them.

## I ran autogen.sh, but it failed with aclocal errors. What's wrong?

```
+ running aclocal -I m4 -I common/m4 ...
aclocal: configure.ac: 8: macro `AM_DISABLE_STATIC' not found in library
aclocal: configure.ac: 17: macro `AM_PROG_LIBTOOL' not found in library
aclocal failed
```

What's wrong?

`aclocal` is unable to find two macros installed by `libtool` in a
file called `libtool.m4`. Normally, this would indicate that you don't have
`libtool`, but that would mean `autogen.sh` should have failed on not finding
`libtool`.

It is more likely that you installed `automake` (which provides `aclocal`)
and `libtool` in different prefixes. You can check this by examining in
what prefix `aclocal` and `libtool` are installed.

You can do three things to fix this :

1.  install `automake` in the same prefix as `libtool`

2.  force use of the `automake` installed in the same prefix as `libtool` by
    using the `--with-automake` option

3.  figure out what prefix `libtool` has been installed to and point
    `aclocal` to the right location by running

```
    export ACLOCAL_FLAGS="-I $(prefix)/share/aclocal"
```
    where you need to replace `$(prefix)` with the one where `libtool` was
    installed to.

## Why is "-Wall -Werror" being used?

`-Wall` is being used because it finds a lot of possible problems
with code. Not all of them are necessarily a problem, but it's better to
have the compiler report some false positives and find a work-around
than spending days chasing a bug the compiler was already hinting to.

`-Werror` is turned off for actual releases. It's turned on by default
for git and prereleases so that people actually notice and fix problems
found by `-Wall`. We want people to actively hit and report or fix them.

If for any reason you want to bypass these flags and you are certain
it's the right thing to do, you can run:

```
make ERROR_CFLAGS=""
```

to clear these error checking `CFLAGS`.
