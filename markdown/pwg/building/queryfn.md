---
title: The query function
...

# The query function

Through the query function, your element will receive queries that it
has to reply to. These are queries like position, duration but also
about the supported formats and scheduling modes your element supports.
Queries can travel both upstream and downstream, so you can receive them
on sink pads as well as source pads.

Below follows a very simple query function that we install on the source
pad of our element.

``` c

static gboolean gst_my_filter_src_query (GstPad    *pad,
                                         GstObject *parent,
                                         GstQuery  *query);

[..]

static void
gst_my_filter_init (GstMyFilter * filter)
{
[..]
  /* configure event function on the pad before adding
   * the pad to the element */
  gst_pad_set_query_function (filter->srcpad,
      gst_my_filter_src_query);
[..]
}

static gboolean
gst_my_filter_src_query (GstPad    *pad,
                 GstObject *parent,
                 GstQuery  *query)
{
  gboolean ret;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      /* we should report the current position */
      [...]
      break;
    case GST_QUERY_DURATION:
      /* we should report the duration here */
      [...]
      break;
    case GST_QUERY_CAPS:
      /* we should report the supported caps here */
      [...]
      break;
    default:
      /* just call the default handler */
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}


```

It is a good idea to call the default query handler
`gst_pad_query_default ()` for unknown queries. Depending on the query
type, the default handler will forward the query or simply unref it.
