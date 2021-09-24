---
title: Request and Sometimes pads
...

# Request and Sometimes pads

Until now, we've only dealt with pads that are always available.
However, there's also pads that are only being created in some cases, or
only if the application requests the pad. The first is called a
*sometimes*; the second is called a *request* pad. The availability of a
pad (always, sometimes or request) can be seen in a pad's template. This
chapter will discuss when each of the two is useful, how they are
created and when they should be disposed.

## Sometimes pads

A “sometimes” pad is a pad that is created under certain conditions, but
not in all cases. This mostly depends on stream content: demuxers will
generally parse the stream header, decide what elementary (video, audio,
subtitle, etc.) streams are embedded inside the system stream, and will
then create a sometimes pad for each of those elementary streams. At its
own choice, it can also create more than one instance of each of those
per element instance. The only limitation is that each newly created pad
should have a unique name. Sometimes pads are disposed when the stream
data is disposed, too (i.e. when going from PAUSED to the READY state).
You should *not* dispose the pad on EOS, because someone might
re-activate the pipeline and seek back to before the end-of-stream
point. The stream should still stay valid after EOS, at least until the
stream data is disposed. In any case, the element is always the owner of
such a pad.

The example code below will parse a text file, where the first line is a
number (n). The next lines all start with a number (0 to n-1), which is
the number of the source pad over which the data should be sent.

```
3
0: foo
1: bar
0: boo
2: bye

```

The code to parse this file and create the dynamic “sometimes” pads,
looks like this:

``` c

typedef struct _GstMyFilter {
[..]
  gboolean firstrun;
  GList *srcpadlist;
} GstMyFilter;

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src_%u",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_STATIC_CAPS ("ANY")
);

static void
gst_my_filter_class_init (GstMyFilterClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
[..]
  gst_element_class_add_pad_template (element_class,
    gst_static_pad_template_get (&src_factory));
[..]
}

static void
gst_my_filter_init (GstMyFilter *filter)
{
[..]
  filter->firstrun = TRUE;
  filter->srcpadlist = NULL;
}

/*
 * Get one line of data - without newline.
 */

static GstBuffer *
gst_my_filter_getline (GstMyFilter *filter)
{
  guint8 *data;
  gint n, num;

  /* max. line length is 512 characters - for safety */
  for (n = 0; n < 512; n++) {
    num = gst_bytestream_peek_bytes (filter->bs, &data, n + 1);
    if (num != n + 1)
      return NULL;

    /* newline? */
    if (data[n] == '\n') {
      GstBuffer *buf = gst_buffer_new_allocate (NULL, n + 1, NULL);

      gst_bytestream_peek_bytes (filter->bs, &data, n);
      gst_buffer_fill (buf, 0, data, n);
      gst_buffer_memset (buf, n, '\0', 1);
      gst_bytestream_flush_fast (filter->bs, n + 1);

      return buf;
    }
  }
}

static void
gst_my_filter_loopfunc (GstElement *element)
{
  GstMyFilter *filter = GST_MY_FILTER (element);
  GstBuffer *buf;
  GstPad *pad;
  GstMapInfo map;
  gint num, n;

  /* parse header */
  if (filter->firstrun) {
    gchar *padname;
    guint8 id;

    if (!(buf = gst_my_filter_getline (filter))) {
      gst_element_error (element, STREAM, READ, (NULL),
             ("Stream contains no header"));
      return;
    }
    gst_buffer_extract (buf, 0, &id, 1);
    num = atoi (id);
    gst_buffer_unref (buf);

    /* for each of the streams, create a pad */
    for (n = 0; n < num; n++) {
      padname = g_strdup_printf ("src_%u", n);
      pad = gst_pad_new_from_static_template (src_factory, padname);
      g_free (padname);

      /* here, you would set _event () and _query () functions */

      /* need to activate the pad before adding */
      gst_pad_set_active (pad, TRUE);

      gst_element_add_pad (element, pad);
      filter->srcpadlist = g_list_append (filter->srcpadlist, pad);
    }
  }

  /* and now, simply parse each line and push over */
  if (!(buf = gst_my_filter_getline (filter))) {
    GstEvent *event = gst_event_new (GST_EVENT_EOS);
    GList *padlist;

    for (padlist = srcpadlist;
         padlist != NULL; padlist = g_list_next (padlist)) {
      pad = GST_PAD (padlist->data);
      gst_pad_push_event (pad, gst_event_ref (event));
    }
    gst_event_unref (event);
    /* pause the task here */
    return;
  }

  /* parse stream number and go beyond the ':' in the data */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  num = atoi (map.data[0]);
  if (num >= 0 && num < g_list_length (filter->srcpadlist)) {
    pad = GST_PAD (g_list_nth_data (filter->srcpadlist, num);

    /* magic buffer parsing foo */
    for (n = 0; map.data[n] != ':' &&
                map.data[n] != '\0'; n++) ;
    if (map.data[n] != '\0') {
      GstBuffer *sub;

      /* create region copy that starts right past the space. The reason
       * that we don't just forward the data pointer is because the
       * pointer is no longer the start of an allocated block of memory,
       * but just a pointer to a position somewhere in the middle of it.
       * That cannot be freed upon disposal, so we'd either crash or have
       * a memleak. Creating a region copy is a simple way to solve that. */
      sub = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL,
          n + 1, map.size - n - 1);
      gst_pad_push (pad, sub);
    }
  }
  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);
}


```

Note that we use a lot of checks everywhere to make sure that the
content in the file is valid. This has two purposes: first, the file
could be erroneous, in which case we prevent a crash. The second and
most important reason is that - in extreme cases - the file could be
used maliciously to cause undefined behaviour in the plugin, which might
lead to security issues. *Always* assume that the file could be used to
do bad things.

## Request pads

“Request” pads are similar to sometimes pads, except that request are
created on demand of something outside of the element rather than
something inside the element. This concept is often used in muxers,
where - for each elementary stream that is to be placed in the output
system stream - one sink pad will be requested. It can also be used in
elements with a variable number of input or outputs pads, such as the
`tee` (multi-output) or `input-selector` (multi-input) elements.

To implement request pads, you need to provide a padtemplate with a
GST\_PAD\_REQUEST presence and implement the `request_new_pad` virtual
method in `GstElement`. To clean up, you will need to implement the
`release_pad` virtual method.

``` c

static GstPad * gst_my_filter_request_new_pad   (GstElement     *element,
                         GstPadTemplate *templ,
                                                 const gchar    *name,
                                                 const GstCaps  *caps);

static void gst_my_filter_release_pad (GstElement *element,
                                       GstPad *pad);

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink_%u",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS ("ANY")
);

static void
gst_my_filter_class_init (GstMyFilterClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
[..]
  gst_element_class_add_pad_template (klass,
    gst_static_pad_template_get (&sink_factory));
[..]
  element_class->request_new_pad = gst_my_filter_request_new_pad;
  element_class->release_pad = gst_my_filter_release_pad;
}

static GstPad *
gst_my_filter_request_new_pad (GstElement     *element,
                   GstPadTemplate *templ,
                   const gchar    *name,
                               const GstCaps  *caps)
{
  GstPad *pad;
  GstMyFilterInputContext *context;

  context = g_new0 (GstMyFilterInputContext, 1);
  pad = gst_pad_new_from_template (templ, name);
  gst_pad_set_element_private (pad, context);

  /* normally, you would set _chain () and _event () functions here */

  gst_element_add_pad (element, pad);

  return pad;
}

static void
gst_my_filter_release_pad (GstElement *element,
                           GstPad *pad)
{
  GstMyFilterInputContext *context;

  context = gst_pad_get_element_private (pad);
  g_free (context);

  gst_element_remove_pad (element, pad);
}



```
