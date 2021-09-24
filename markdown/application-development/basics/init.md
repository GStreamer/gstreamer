---
title: Initializing GStreamer
...

# Initializing GStreamer

When writing a GStreamer application, you can simply include `gst/gst.h`
to get access to the library functions. Besides that, you will also need
to initialize the GStreamer library.

## Simple initialization

Before the GStreamer libraries can be used, `gst_init` has to be called
from the main application. This call will perform the necessary
initialization of the library as well as parse the GStreamer-specific
command line options.

A typical program \[1\] would have code to initialize GStreamer that
looks like this:


``` c
#include <stdio.h>
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  const gchar *nano_str;
  guint major, minor, micro, nano;

  gst_init (&argc, &argv);

  gst_version (&major, &minor, &micro, &nano);

  if (nano == 1)
    nano_str = "(CVS)";
  else if (nano == 2)
    nano_str = "(Prerelease)";
  else
    nano_str = "";

  printf ("This program is linked against GStreamer %d.%d.%d %s\n",
          major, minor, micro, nano_str);

  return 0;
}
```

Use the `GST_VERSION_MAJOR`, `GST_VERSION_MINOR` and `GST_VERSION_MICRO`
macros to get the GStreamer version you are building against, or use the
function `gst_version` to get the version your application is linked
against. GStreamer currently uses a scheme where versions with the same
major and minor versions are API- and ABI-compatible.

It is also possible to call the `gst_init` function with two `NULL`
arguments, in which case no command line options will be parsed by
GStreamer.

## The GOption interface

You can also use a `GOption` table to initialize your own parameters as
shown in the next example:


``` c
#include <gst/gst.h>

int
main (int   argc,
      char *argv[])
{
  gboolean silent = FALSE;
  gchar *savefile = NULL;
  GOptionContext *ctx;
  GError *err = NULL;
  GOptionEntry entries[] = {
    { "silent", 's', 0, G_OPTION_ARG_NONE, &silent,
      "do not output status information", NULL },
    { "output", 'o', 0, G_OPTION_ARG_STRING, &savefile,
      "save xml representation of pipeline to FILE and exit", "FILE" },
    { NULL }
  };

  ctx = g_option_context_new ("- Your application");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Failed to initialize: %s\n", err->message);
    g_clear_error (&err);
    g_option_context_free (ctx);
    return 1;
  }
  g_option_context_free (ctx);

  printf ("Run me with --help to see the Application options appended.\n");

  return 0;
}
```

As shown in this fragment, you can use a
[GOption](http://developer.gnome.org/glib/stable/glib-Commandline-option-parser.html)
table to define your application-specific command line options, and pass
this table to the GLib initialization function along with the option
group returned from the function `gst_init_get_option_group`. Your
application options will be parsed in addition to the standard GStreamer
options.

1.  The code for this example is automatically extracted from the
    documentation and built under `tests/examples/manual` in the
    GStreamer tarball.
