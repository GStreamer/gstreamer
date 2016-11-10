# Building GStreamer from git

## Is there a way to test or develop against GStreamer from git without interfering with my system GStreamer installed from packages?

Yes! You have two options: you can either run GStreamer in an
uninstalled setup (see [How do I develop against an uninstalled
GStreamer copy ?](#developing-uninstalled-gstreamer)), or you can use
GNOME's jhbuild.

## How do I check out GStreamer from git ?

GStreamer is hosted on Freedesktop.org. GStreamer consists of
various parts. In the beginning, you will be interested in the
"gstreamer" module, containing the core, and "gst-plugins-base" and
"gst-plugins-good", containing the basic set of plugins. Finally, you
may also be interested in "gst-plugins-ugly", "gst-plugins-bad" and
"gst-ffmpeg" for more comprehensive media format support.

To check out the latest git version of the core and the basic modules,
use

``` 
 for module in gstreamer gst-plugins-base gst-plugins-good; do
   git clone git://anongit.freedesktop.org/git/gstreamer/$module ;
 done
```

This will create three directories in your current directory:
"gstreamer", "gst-plugins-base", and "gst-plugins-good". If you want to
get another module, use the above git clone command line and replace
$module with the name of the module. Once you have checked out these
modules, you will need to change into each directory and run
./autogen.sh, which will among other things checkout the common module
underneath each module checkout.

The [modules page](http://gstreamer.freedesktop.org/modules/) has a list
of active ones together with a short description.

## How do I get developer access to GStreamer git ?

If you want to gain developer access to GStreamer git, you should
ask for it on the development lists, or ask one of the maintainers
directly. We will usually only consider requests by developers who have
been active and competent GStreamer contributors for some time already.
If you are not already a registered developer with a user account on
Freedesktop.org, you will then have to provide them with:

1.  your desired unix username

2.  your full name

3.  your e-mail address

4.  a copy of your public sshv2 identity. If you do not have this yet,
    you can generate it by running "ssh-keygen -t rsa -f
    ~/.ssh/id\_rsa.pub-fdo". The resulting public key will be in
    `~/.ssh/id_rsa.pub-fdo`

5.  your GPG fingerprint. This would allow you to add and remove ssh
    keys to your account.

Once you have all these items, see
<http://freedesktop.org/wiki/AccountRequests> for what to do with them.

## I ran autogen.sh, but it fails with aclocal errors. What's wrong ?

    + running aclocal -I m4 -I common/m4 ...
    aclocal: configure.ac: 8: macro `AM_DISABLE_STATIC' not found in library
    aclocal: configure.ac: 17: macro `AM_PROG_LIBTOOL' not found in library
    aclocal failed

What's wrong ?

aclocal is unable to find two macros installed by libtool in a
file called libtool.m4. Normally this would indicate that you don't have
libtool, but that would mean autogen.sh would have failed on not finding
libtool.

It is more likely that you installed automake (which provides aclocal)
in a different prefix than libtool. You can check this by examining in
what prefix both aclocal and libtool are installed.

You can do three things to fix this :

1.  install automake in the same prefix as libtool

2.  force use of the automake installed in the same prefix as libtool by
    using the --with-automake option

3.  figure out what prefix libtool has been installed to and point
    aclocal to the right location by running
    
        export ACLOCAL_FLAGS="-I $(prefix)/share/aclocal"
    
    where you replace prefix with the prefix where libtool was
    installed.

## Why is "-Wall -Werror" being used ?

"-Wall" is being used because it finds a lot of possible problems
with code. Not all of them are necessarily a problem, but it's better to
have the compiler report some false positives and find a work-around
than to spend time chasing a bug for days that the compiler was giving
you hints about.

"-Werror" is turned off for actual releases. It's turned on by default
for git and prereleases so that people actually notice and fix problems
found by "-Wall". We want people to actively hit and report or fix them.

If for any reason you want to bypass these flags and you are certain
it's the right thing to do, you can run

    make ERROR_CFLAGS=""

to clear the CFLAGS for error checking.
