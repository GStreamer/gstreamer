---
title: Integration
...

# Integration

GStreamer tries to integrate closely with operating systems (such as
Linux and UNIX-like operating systems, OS X or Windows) and desktop
environments (such as GNOME or KDE). In this chapter, we'll mention some
specific techniques to integrate your application with your operating
system or desktop environment of choice.

## Linux and UNIX-like operating systems

GStreamer provides a basic set of elements that are useful when
integrating with Linux or a UNIX-like operating system.

  - For audio input and output, GStreamer provides input and output
    elements for several audio subsystems. Amongst others, GStreamer
    includes elements for ALSA (alsasrc, alsasink), OSS (osssrc,
    osssink) Pulesaudio (pulsesrc, pulsesink) and Sun audio
    (sunaudiosrc, sunaudiomixer, sunaudiosink).

  - For video input, GStreamer contains source elements for Video4linux2
    (v4l2src, v4l2element, v4l2sink).

  - For video output, GStreamer provides elements for output to
    X-windows (ximagesink), Xv-windows (xvimagesink; for
    hardware-accelerated video), direct-framebuffer (dfbimagesink) and
    openGL image contexts (glsink).

## GNOME desktop

GStreamer has been the media backend of the
[GNOME](http://www.gnome.org/) desktop since GNOME-2.2 onwards.
Nowadays, a whole bunch of GNOME applications make use of GStreamer for
media-processing, including (but not limited to)
[Rhythmbox](http://www.rhythmbox.org/),
[Videos](https://wiki.gnome.org/Apps/Videos) and [Sound
Juicer](https://wiki.gnome.org/Apps/SoundJuicer).

Most of these GNOME applications make use of some specific techniques to
integrate as closely as possible with the GNOME desktop:

  - GNOME applications usually call `gtk_init ()` to parse command-line
    options and initialize GTK. GStreamer applications would normally
    call `gst_init ()` to do the same for GStreamer. This would mean
    that only one of the two can parse command-line options. To work
    around this issue, GStreamer can provide a GLib `GOptionGroup` which
    can be passed to `gnome_program_init ()`. The following example
    requires GTK 2.6 or newer (previous GTK versions do not support
    command line parsing via GOption yet)

    ``` c
    #include <gtk/gtk.h>
    #include <gst/gst.h>

    static gchar **cmd_filenames = NULL;

    static GOptionEntries cmd_options[] = {
      /* here you can add command line options for your application. Check
       * the GOption section in the GLib API reference for a more elaborate
       * example of how to add your own command line options here */

      /* at the end we have a special option that collects all remaining
       * command line arguments (like filenames) for us. If you don't
       * need this, you can safely remove it */
      { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &cmd_filenames,
        "Special option that collects any remaining arguments for us" },

      /* mark the end of the options array with a NULL option */
      { NULL, }
    };

    /* this should usually be defined in your config.h */
    #define VERSION "0.0.1"

    gint
    main (gint argc, gchar **argv)
    {
      GOptionContext *context;
      GOptionGroup *gstreamer_group, *gtk_group;
      GError *err = NULL;

      context = g_option_context_new ("gtk-demo-app");

      /* get command line options from GStreamer and add them to the group */
      gstreamer_group = gst_init_get_option_group ();
      g_option_context_add_group (context, gstreamer_group);
      gtk_group = gtk_get_option_group (TRUE);
      g_option_context_add_group (context, gtk_group);

      /* add our own options. If you are using gettext for translation of your
       * strings, use GETTEXT_PACKAGE here instead of NULL */
      g_option_context_add_main_entries (context, cmd_options, NULL);

      /* now parse the commandline options, note that this already
       * calls gtk_init() and gst_init() */
      if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
        g_print ("Error initializing: %s\n", err->message);
        g_clear_error (&err);
        g_option_context_free (ctx);
        exit (1);
      }
      g_option_context_free (ctx);

      /* any filenames we got passed on the command line? parse them! */
      if (cmd_filenames != NULL) {
        guint i, num;

        num = g_strv_length (cmd_filenames);
        for (i = 0; i < num; ++i) {
          /* do something with the filename ... */
          g_print ("Adding to play queue: %s\n", cmd_filenames[i]);
        }

        g_strfreev (cmd_filenames);
        cmd_filenames = NULL;
      }

    [..]

    }
    ```

  - GNOME uses Pulseaudio for audio, use the pulsesrc and pulsesink
    elements to have access to all the features.

  - GStreamer provides data input/output elements for use with the GIO
    VFS system. These elements are called “giosrc” and “giosink”. The
    deprecated GNOME-VFS system is supported too but shouldn't be used
    for any new applications.

## KDE desktop

GStreamer has been proposed for inclusion in KDE-4.0. Currently,
GStreamer is included as an optional component, and it's used by several
KDE applications, including [AmaroK](http://amarok.kde.org/),
[KMPlayer](http://www.xs4all.nl/~jjvrieze/kmplayer.html) and
[Kaffeine](http://kaffeine.sourceforge.net/).

Although not yet as complete as the GNOME integration bits, there are
already some KDE integration specifics available. This list will
probably grow as GStreamer starts to be used in KDE-4.0:

  - AmaroK contains a kiosrc element, which is a source element that
    integrates with the KDE VFS subsystem KIO.

## OS X

GStreamer provides native video and audio output elements for OS X. It
builds using the standard development tools for OS X.

## Windows

> **Warning**
>
> Note: this section is out of date. GStreamer-1.0 has much better
> support for win32 than previous versions though and should usually
> compile and work out-of-the-box both using MSYS/MinGW or Microsoft
> compilers. The [GStreamer web site](http://gstreamer.freedesktop.org)
> and the [mailing list
> archives](http://news.gmane.org/gmane.comp.video.gstreamer.devel) are
> a good place to check the latest win32-related news.

GStreamer builds using Microsoft Visual C .NET 2003 and using Cygwin.

### Building GStreamer under Win32

There are different makefiles that can be used to build GStreamer with
the usual Microsoft compiling tools.

The Makefile is meant to be used with the GNU make program and the free
version of the Microsoft compiler
(<http://msdn.microsoft.com/visualc/vctoolkit2003/>). You also have to
modify your system environment variables to use it from the
command-line. You will also need a working Platform SDK for Windows that
is available for free from Microsoft.

The projects/makefiles will generate automatically some source files
needed to compile GStreamer. That requires that you have installed on
your system some GNU tools and that they are available in your system
PATH.

The GStreamer project depends on other libraries, namely :

  - GLib

  - libxml2

  - libintl

  - libiconv

Work is being done to provide pre-compiled GStreamer-1.0 libraries as a
packages for win32. Check the [GStreamer web
site](http://gstreamer.freedesktop.org) and check our [mailing
list](http://news.gmane.org/gmane.comp.video.gstreamer.devel) for the
latest developments in this respect.

> **Note**
>
> GNU tools needed that you can find on
> <http://gnuwin32.sourceforge.net/>
>
>   - GNU flex (tested with 2.5.4)
>
>   - GNU bison (tested with 1.35)
>
> and <http://www.mingw.org/>
>
>   - GNU make (tested with 3.80)
>
> the generated files from the -auto makefiles will be available soon
> separately on the net for convenience (people who don't want to
> install GNU tools).

### Installation on the system

FIXME: This section needs be updated for GStreamer-1.0.
