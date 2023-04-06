# Basic tutorial 9: Media information gathering


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

Sometimes you might want to quickly find out what kind of media a file
(or URI) contains, or if you will be able to play the media at all. You
can build a pipeline, set it to run, and watch the bus messages, but
GStreamer has a utility that does just that for you. This tutorial
shows:

  - How to recover information regarding a URI

  - How to find out if a URI is playable

## Introduction

`GstDiscoverer` is a utility object found in the `pbutils` library
(Plug-in Base utilities) that accepts a URI or list of URIs, and returns
information about them. It can work in synchronous or asynchronous
modes.

In synchronous mode, there is only a single function to call,
`gst_discoverer_discover_uri()`, which blocks until the information is
ready. Due to this blocking, it is usually less interesting for
GUI-based applications and the asynchronous mode is used, as described
in this tutorial.

The recovered information includes codec descriptions, stream topology
(number of streams and sub-streams) and available metadata (like the
audio language).

As an example, this is the result
of discovering https://gstreamer.freedesktop.org/data/media/sintel\_trailer-480p.webm

    Duration: 0:00:52.250000000
    Tags:
      video codec: On2 VP8
      language code: en
      container format: Matroska
      application name: ffmpeg2theora-0.24
      encoder: Xiph.Org libVorbis I 20090709
      encoder version: 0
      audio codec: Vorbis
      nominal bitrate: 80000
      bitrate: 80000
    Seekable: yes
    Stream information:
      container: WebM
        audio: Vorbis
          Tags:
            language code: en
            container format: Matroska
            audio codec: Vorbis
            application name: ffmpeg2theora-0.24
            encoder: Xiph.Org libVorbis I 20090709
            encoder version: 0
            nominal bitrate: 80000
            bitrate: 80000
        video: VP8
          Tags:
            video codec: VP8 video
            container format: Matroska

The following code tries to discover the URI provided through the
command line, and outputs the retrieved information (If no URI is
provided it uses a default one).

This is a simplified version of what the `gst-discoverer-1.0` tool does
([](tutorials/basic/gstreamer-tools.md)), which is
an application that only displays data, but does not perform any
playback.

## The GStreamer Discoverer

Copy this code into a text file named `basic-tutorial-9.c` (or find it
in your GStreamer installation).

**basic-tutorial-9.c**

``` c
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstDiscoverer *discoverer;
  GMainLoop *loop;
} CustomData;

/* Print a tag in a human-readable format (name: value) */
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data) {
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);

  gst_tag_list_copy_value (&val, tags, tag);

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);

  g_value_unset (&val);
}

/* Print information regarding a stream */
static void print_stream_info (GstDiscovererStreamInfo *info, gint depth) {
  gchar *desc = NULL;
  GstCaps *caps;
  const GstTagList *tags;

  caps = gst_discoverer_stream_info_get_caps (info);

  if (caps) {
    if (gst_caps_is_fixed (caps))
      desc = gst_pb_utils_get_codec_description (caps);
    else
      desc = gst_caps_to_string (caps);
    gst_caps_unref (caps);
  }

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_discoverer_stream_info_get_stream_type_nick (info), (desc ? desc : ""));

  if (desc) {
    g_free (desc);
    desc = NULL;
  }

  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    g_print ("%*sTags:\n", 2 * (depth + 1), " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (depth + 2));
  }
}

/* Print information regarding a stream and its substreams, if any */
static void print_topology (GstDiscovererStreamInfo *info, gint depth) {
  GstDiscovererStreamInfo *next;

  if (!info)
    return;

  print_stream_info (info, depth);

  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);
    gst_discoverer_stream_info_unref (next);
  } else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;

    streams = gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);
    }
    gst_discoverer_stream_info_list_free (streams);
  }
}

/* This function is called every time the discoverer has information regarding
 * one of the URIs we provided.*/
static void on_discovered_cb (GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, CustomData *data) {
  GstDiscovererResult result;
  const gchar *uri;
  const GstTagList *tags;
  GstDiscovererStreamInfo *sinfo;

  uri = gst_discoverer_info_get_uri (info);
  result = gst_discoverer_info_get_result (info);
  switch (result) {
    case GST_DISCOVERER_URI_INVALID:
      g_print ("Invalid URI '%s'\n", uri);
      break;
    case GST_DISCOVERER_ERROR:
      g_print ("Discoverer error: %s\n", err->message);
      break;
    case GST_DISCOVERER_TIMEOUT:
      g_print ("Timeout\n");
      break;
    case GST_DISCOVERER_BUSY:
      g_print ("Busy\n");
      break;
    case GST_DISCOVERER_MISSING_PLUGINS:{
      const GstStructure *s;
      gchar *str;

      s = gst_discoverer_info_get_misc (info);
      str = gst_structure_to_string (s);

      g_print ("Missing plugins: %s\n", str);
      g_free (str);
      break;
    }
    case GST_DISCOVERER_OK:
      g_print ("Discovered '%s'\n", uri);
      break;
  }

  if (result != GST_DISCOVERER_OK) {
    g_printerr ("This URI cannot be played\n");
    return;
  }

  /* If we got no error, show the retrieved information */

  g_print ("\nDuration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (gst_discoverer_info_get_duration (info)));

  tags = gst_discoverer_info_get_tags (info);
  if (tags) {
    g_print ("Tags:\n");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (1));
  }

  g_print ("Seekable: %s\n", (gst_discoverer_info_get_seekable (info) ? "yes" : "no"));

  g_print ("\n");

  sinfo = gst_discoverer_info_get_stream_info (info);
  if (!sinfo)
    return;

  g_print ("Stream information:\n");

  print_topology (sinfo, 1);

  gst_discoverer_stream_info_unref (sinfo);

  g_print ("\n");
}

/* This function is called when the discoverer has finished examining
 * all the URIs we provided.*/
static void on_finished_cb (GstDiscoverer *discoverer, CustomData *data) {
  g_print ("Finished discovering\n");

  g_main_loop_quit (data->loop);
}

int main (int argc, char **argv) {
  CustomData data;
  GError *err = NULL;
  gchar *uri = "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm";

  /* if a URI was provided, use it instead of the default one */
  if (argc > 1) {
    uri = argv[1];
  }

  /* Initialize custom data structure */
  memset (&data, 0, sizeof (data));

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  g_print ("Discovering '%s'\n", uri);

  /* Instantiate the Discoverer */
  data.discoverer = gst_discoverer_new (5 * GST_SECOND, &err);
  if (!data.discoverer) {
    g_print ("Error creating discoverer instance: %s\n", err->message);
    g_clear_error (&err);
    return -1;
  }

  /* Connect to the interesting signals */
  g_signal_connect (data.discoverer, "discovered", G_CALLBACK (on_discovered_cb), &data);
  g_signal_connect (data.discoverer, "finished", G_CALLBACK (on_finished_cb), &data);

  /* Start the discoverer process (nothing to do yet) */
  gst_discoverer_start (data.discoverer);

  /* Add a request to process asynchronously the URI passed through the command line */
  if (!gst_discoverer_discover_uri_async (data.discoverer, uri)) {
    g_print ("Failed to start discovering URI '%s'\n", uri);
    g_object_unref (data.discoverer);
    return -1;
  }

  /* Create a GLib Main Loop and set it to run, so we can wait for the signals */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Stop the discoverer process */
  gst_discoverer_stop (data.discoverer);

  /* Free resources */
  g_object_unref (data.discoverer);
  g_main_loop_unref (data.loop);

  return 0;
}
```


> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
>
> ``gcc basic-tutorial-9.c -o basic-tutorial-9 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-pbutils-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial opens the URI passed as the first parameter in the command line (or a default URI if none is provided) and outputs information about it on the screen. If the media is located on the Internet, the application might take a bit to react depending on your connection speed.
>
> Required libraries: `gstreamer-pbutils-1.0` `gstreamer-1.0`


## Walkthrough

These are the main steps to use the `GstDiscoverer`:

``` c
/* Instantiate the Discoverer */
data.discoverer = gst_discoverer_new (5 * GST_SECOND, &err);
if (!data.discoverer) {
  g_print ("Error creating discoverer instance: %s\n", err->message);
  g_clear_error (&err);
  return -1;
}
```

`gst_discoverer_new()` creates a new Discoverer object. The first
parameter is the timeout per file, in nanoseconds (use the
`GST_SECOND` macro for simplicity).

``` c
/* Connect to the interesting signals */
g_signal_connect (data.discoverer, "discovered", G_CALLBACK (on_discovered_cb), &data);
g_signal_connect (data.discoverer, "finished", G_CALLBACK (on_finished_cb), &data);
```

Connect to the interesting signals, as usual. We discuss them in the
snippet for their callbacks.

``` c
/* Start the discoverer process (nothing to do yet) */
gst_discoverer_start (data.discoverer);
```

`gst_discoverer_start()` launches the discovering process, but we have
not provided any URI to discover yet. This is done
next:

``` c
/* Add a request to process asynchronously the URI passed through the command line */
if (!gst_discoverer_discover_uri_async (data.discoverer, uri)) {
  g_print ("Failed to start discovering URI '%s'\n", uri);
  g_object_unref (data.discoverer);
  return -1;
}
```

`gst_discoverer_discover_uri_async()` enqueues the provided URI for
discovery. Multiple URIs can be enqueued with this function. As the
discovery process for each of them finishes, the registered callback
functions will be fired
up.

``` c
/* Create a GLib Main Loop and set it to run, so we can wait for the signals */
data.loop = g_main_loop_new (NULL, FALSE);
g_main_loop_run (data.loop);
```

The usual GLib main loop is instantiated and executed. We will get out
of it when `g_main_loop_quit()` is called from the
`on_finished_cb` callback.

``` c
/* Stop the discoverer process */
gst_discoverer_stop (data.discoverer);
```

Once we are done with the discoverer, we stop it with
`gst_discoverer_stop()` and unref it with `g_object_unref()`.

Let's review now the callbacks we have
registered:

``` c
/* This function is called every time the discoverer has information regarding
 * one of the URIs we provided.*/
static void on_discovered_cb (GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, CustomData *data) {
  GstDiscovererResult result;
  const gchar *uri;
  const GstTagList *tags;
  GstDiscovererStreamInfo *sinfo;

  uri = gst_discoverer_info_get_uri (info);
  result = gst_discoverer_info_get_result (info);
```

We got here because the Discoverer has finished working on one URI, and
provides us a `GstDiscovererInfo` structure with all the information.

The first step is to retrieve the particular URI this call refers to (in
case we had multiple discover process running, which is not the case in
this example) with `gst_discoverer_info_get_uri()` and the discovery
result with `gst_discoverer_info_get_result()`.

``` c
switch (result) {
  case GST_DISCOVERER_URI_INVALID:
    g_print ("Invalid URI '%s'\n", uri);
    break;
  case GST_DISCOVERER_ERROR:
    g_print ("Discoverer error: %s\n", err->message);
    break;
  case GST_DISCOVERER_TIMEOUT:
    g_print ("Timeout\n");
    break;
  case GST_DISCOVERER_BUSY:
    g_print ("Busy\n");
    break;
  case GST_DISCOVERER_MISSING_PLUGINS:{
    const GstStructure *s;
    gchar *str;

    s = gst_discoverer_info_get_misc (info);
    str = gst_structure_to_string (s);

    g_print ("Missing plugins: %s\n", str);
    g_free (str);
    break;
  }
  case GST_DISCOVERER_OK:
    g_print ("Discovered '%s'\n", uri);
    break;
}

if (result != GST_DISCOVERER_OK) {
  g_printerr ("This URI cannot be played\n");
  return;
}
```

As the code shows, any result other than `GST_DISCOVERER_OK` means that
there has been some kind of problem, and this URI cannot be played. The
reasons can vary, but the enum values are quite explicit
(`GST_DISCOVERER_BUSY` can only happen when in synchronous mode, which
is not used in this example).

If no error happened, information can be retrieved from the
`GstDiscovererInfo` structure with the different
`gst_discoverer_info_get_*` methods (like,
`gst_discoverer_info_get_duration()`, for example).

Bits of information which are made of lists, like tags and stream info,
needs some extra parsing:

``` c
tags = gst_discoverer_info_get_tags (info);
if (tags) {
  g_print ("Tags:\n");
  gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (1));
}
```

Tags are metadata (labels) attached to the media. They can be examined
with `gst_tag_list_foreach()`, which will call `print_tag_foreach` for
each tag found (the list could also be traversed manually, for example,
or a specific tag could be searched for with
`gst_tag_list_get_string()`). The code for `print_tag_foreach` is pretty
much self-explanatory.

``` c
sinfo = gst_discoverer_info_get_stream_info (info);
if (!sinfo)
  return;

g_print ("Stream information:\n");

print_topology (sinfo, 1);

gst_discoverer_stream_info_unref (sinfo);
```

`gst_discoverer_info_get_stream_info()` returns
a `GstDiscovererStreamInfo` structure that is parsed in
the `print_topology` function, and then discarded
with `gst_discoverer_stream_info_unref()`.

``` c
/* Print information regarding a stream and its substreams, if any */
static void print_topology (GstDiscovererStreamInfo *info, gint depth) {
  GstDiscovererStreamInfo *next;

  if (!info)
    return;

  print_stream_info (info, depth);

  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);
    gst_discoverer_stream_info_unref (next);
  } else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;

    streams = gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);
    }
    gst_discoverer_stream_info_list_free (streams);
  }
}
```

The `print_stream_info` function's code is also pretty much
self-explanatory: it prints the stream's capabilities and then the
associated caps, using `print_tag_foreach` too.

Then, `print_topology` looks for the next element to display. If
`gst_discoverer_stream_info_get_next()` returns a non-NULL stream info,
it refers to our descendant and that should be displayed. Otherwise, if
we are a container, recursively call `print_topology` on each of our
children obtained with `gst_discoverer_container_info_get_streams()`.
Otherwise, we are a final stream, and do not need to recurse (This part
of the Discoverer API is admittedly a bit obscure).

## Conclusion

This tutorial has shown:

  - How to recover information regarding a URI using the `GstDiscoverer`

  - How to find out if a URI is playable by looking at the return code
    obtained with `gst_discoverer_info_get_result()`.

It has been a pleasure having you here, and see you soon!
