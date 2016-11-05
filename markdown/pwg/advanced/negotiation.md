---
title: Caps negotiation
...

# Caps negotiation

Caps negotiation is the act of finding a media format (GstCaps) between
elements that they can handle. This process in GStreamer can in most
cases find an optimal solution for the complete pipeline. In this
section we explain how this works.

## Caps negotiation basics

In GStreamer, negotiation of the media format always follows the
following simple rules:

  - A downstream element suggest a format on its sinkpad and places the
    suggestion in the result of the CAPS query performed on the sinkpad.
    See also [Implementing a CAPS query
    function](#implementing-a-caps-query-function).

  - An upstream element decides on a format. It sends the selected media
    format downstream on its source pad with a CAPS event. Downstream
    elements reconfigure themselves to handle the media type in the CAPS
    event on the sinkpad.

  - A downstream element can inform upstream that it would like to
    suggest a new format by sending a RECONFIGURE event upstream. The
    RECONFIGURE event simply instructs an upstream element to restart
    the negotiation phase. Because the element that sent out the
    RECONFIGURE event is now suggesting another format, the format in
    the pipeline might change.

In addition to the CAPS and RECONFIGURE event and the CAPS query, there
is an ACCEPT\_CAPS query to quickly check if a certain caps can be
accepted by an element.

All negotiation follows these simple rules. Let's take a look at some
typical uses cases and how negotiation happens.

## Caps negotiation use cases

In what follows we will look at some use cases for push-mode scheduling.
The pull-mode scheduling negotiation phase is discussed in [Pull-mode
Caps negotiation](#pull-mode-caps-negotiation) and is actually similar
as we will see.

Since the sink pads only suggest formats and the source pads need to
decide, the most complicated work is done in the source pads. We can
identify 3 caps negotiation use cases for the source pads:

  - Fixed negotiation. An element can output one format only. See [Fixed
    negotiation](#fixed-negotiation).

  - Transform negotiation. There is a (fixed) transform between the
    input and output format of the element, usually based on some
    element property. The caps that the element will produce depend on
    the upstream caps and the caps that the element can accept depend on
    the downstream caps. See [Transform
    negotiation](#transform-negotiation).

  - Dynamic negotiation. An element can output many formats. See
    [Dynamic negotiation](#dynamic-negotiation).

### Fixed negotiation

In this case, the source pad can only produce a fixed format. Usually
this format is encoded inside the media. No downstream element can ask
for a different format, the only way that the source pad will
renegotiate is when the element decides to change the caps itself.

Elements that could implement fixed caps (on their source pads) are, in
general, all elements that are not renegotiable. Examples include:

  - A typefinder, since the type found is part of the actual data stream
    and can thus not be re-negotiated. The typefinder will look at the
    stream of bytes, figure out the type, send a CAPS event with the
    caps and then push buffers of the type.

  - Pretty much all demuxers, since the contained elementary data
    streams are defined in the file headers, and thus not renegotiable.

  - Some decoders, where the format is embedded in the data stream and
    not part of the peercaps *and* where the decoder itself is not
    reconfigurable, too.

  - Some sources that produce a fixed format.

`gst_pad_use_fixed_caps()` is used on the source pad with fixed caps. As
long as the pad is not negotiated, the default CAPS query will return
the caps presented in the padtemplate. As soon as the pad is negotiated,
the CAPS query will return the negotiated caps (and nothing else). These
are the relevant code snippets for fixed caps source pads.

``` c

[..]
  pad = gst_pad_new_from_static_template (..);
  gst_pad_use_fixed_caps (pad);
[..]


```

The fixed caps can then be set on the pad by calling `gst_pad_set_caps
()`.

``` c

[..]
    caps = gst_caps_new_simple ("audio/x-raw",
        "format", G_TYPE_STRING, GST_AUDIO_NE(F32),
        "rate", G_TYPE_INT, <samplerate>,
        "channels", G_TYPE_INT, <num-channels>, NULL);
    if (!gst_pad_set_caps (pad, caps)) {
      GST_ELEMENT_ERROR (element, CORE, NEGOTIATION, (NULL),
          ("Some debug information here"));
      return GST_FLOW_ERROR;
    }
[..]


```

These types of elements also don't have a relation between the input
format and the output format, the input caps simply don't contain the
information needed to produce the output caps.

All other elements that need to be configured for the format should
implement full caps negotiation, which will be explained in the next few
sections.

### Transform negotiation

In this negotiation technique, there is a fixed transform between the
element input caps and the output caps. This transformation could be
parameterized by element properties but not by the content of the stream
(see [Fixed negotiation](#fixed-negotiation) for that use-case).

The caps that the element can accept depend on the (fixed
transformation) downstream caps. The caps that the element can produce
depend on the (fixed transformation of) the upstream caps.

This type of element can usually set caps on its source pad from the
`_event()` function on the sink pad when it received the CAPS event.
This means that the caps transform function transforms a fixed caps into
another fixed caps. Examples of elements include:

  - Videobox. It adds configurable border around a video frame depending
    on object properties.

  - Identity elements. All elements that don't change the format of the
    data, only the content. Video and audio effects are an example.
    Other examples include elements that inspect the stream.

  - Some decoders and encoders, where the output format is defined by
    input format, like mulawdec and mulawenc. These decoders usually
    have no headers that define the content of the stream. They are
    usually more like conversion elements.

Below is an example of a negotiation steps of a typical transform
element. In the sink pad CAPS event handler, we compute the caps for the
source pad and set those.

``` c

  [...]

static gboolean
gst_my_filter_setcaps (GstMyFilter *filter,
               GstCaps *caps)
{
  GstStructure *structure;
  int rate, channels;
  gboolean ret;
  GstCaps *outcaps;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &rate);
  ret = ret && gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
    return FALSE;

  outcaps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, GST_AUDIO_NE(S16),
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels, NULL);
  ret = gst_pad_set_caps (filter->srcpad, outcaps);
  gst_caps_unref (outcaps);

  return ret;
}

static gboolean
gst_my_filter_sink_event (GstPad    *pad,
                  GstObject *parent,
                  GstEvent  *event)
{
  gboolean ret;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_my_filter_setcaps (filter, caps);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

  [...]


```

### Dynamic negotiation

A last negotiation method is the most complex and powerful dynamic
negotiation.

Like with the transform negotiation in [Transform
negotiation](#transform-negotiation), dynamic negotiation will perform a
transformation on the downstream/upstream caps. Unlike the transform
negotiation, this transform will convert fixed caps to unfixed caps.
This means that the sink pad input caps can be converted into unfixed
(multiple) formats. The source pad will have to choose a format from all
the possibilities. It would usually like to choose a format that
requires the least amount of effort to produce but it does not have to
be. The selection of the format should also depend on the caps that can
be accepted downstream (see a QUERY\_CAPS function in [Implementing a
CAPS query function](#implementing-a-caps-query-function)).

A typical flow goes like this:

  - Caps are received on the sink pad of the element.

  - If the element prefers to operate in passthrough mode, check if
    downstream accepts the caps with the ACCEPT\_CAPS query. If it does,
    we can complete negotiation and we can operate in passthrough mode.

  - Calculate the possible caps for the source pad.

  - Query the downstream peer pad for the list of possible caps.

  - Select from the downstream list the first caps that you can
    transform to and set this as the output caps. You might have to
    fixate the caps to some reasonable defaults to construct fixed caps.

Examples of this type of elements include:

  - Converter elements such as videoconvert, audioconvert,
    audioresample, videoscale, ...

  - Source elements such as audiotestsrc, videotestsrc, v4l2src,
    pulsesrc, ...

Let's look at the example of an element that can convert between
samplerates, so where input and output samplerate don't have to be the
same:

``` c

static gboolean
gst_my_filter_setcaps (GstMyFilter *filter,
               GstCaps *caps)
{
  if (gst_pad_set_caps (filter->srcpad, caps)) {
    filter->passthrough = TRUE;
  } else {
    GstCaps *othercaps, *newcaps;
    GstStructure *s = gst_caps_get_structure (caps, 0), *others;

    /* no passthrough, setup internal conversion */
    gst_structure_get_int (s, "channels", &filter->channels);
    othercaps = gst_pad_get_allowed_caps (filter->srcpad);
    others = gst_caps_get_structure (othercaps, 0);
    gst_structure_set (others,
      "channels", G_TYPE_INT, filter->channels, NULL);

    /* now, the samplerate value can optionally have multiple values, so
     * we "fixate" it, which means that one fixed value is chosen */
    newcaps = gst_caps_copy_nth (othercaps, 0);
    gst_caps_unref (othercaps);
    gst_pad_fixate_caps (filter->srcpad, newcaps);
    if (!gst_pad_set_caps (filter->srcpad, newcaps))
      return FALSE;

    /* we are now set up, configure internally */
    filter->passthrough = FALSE;
    gst_structure_get_int (s, "rate", &filter->from_samplerate);
    others = gst_caps_get_structure (newcaps, 0);
    gst_structure_get_int (others, "rate", &filter->to_samplerate);
  }

  return TRUE;
}

static gboolean
gst_my_filter_sink_event (GstPad    *pad,
                  GstObject *parent,
                  GstEvent  *event)
{
  gboolean ret;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_my_filter_setcaps (filter, caps);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static GstFlowReturn
gst_my_filter_chain (GstPad    *pad,
             GstObject *parent,
             GstBuffer *buf)
{
  GstMyFilter *filter = GST_MY_FILTER (parent);
  GstBuffer *out;

  /* push on if in passthrough mode */
  if (filter->passthrough)
    return gst_pad_push (filter->srcpad, buf);

  /* convert, push */
  out = gst_my_filter_convert (filter, buf);
  gst_buffer_unref (buf);

  return gst_pad_push (filter->srcpad, out);
}


```

## Upstream caps (re)negotiation

Upstream negotiation's primary use is to renegotiate (part of) an
already-negotiated pipeline to a new format. Some practical examples
include to select a different video size because the size of the video
window changed, and the video output itself is not capable of rescaling,
or because the audio channel configuration changed.

Upstream caps renegotiation is requested by sending a
GST\_EVENT\_RECONFIGURE event upstream. The idea is that it will
instruct the upstream element to reconfigure its caps by doing a new
query for the allowed caps and then choosing a new caps. The element
that sends out the RECONFIGURE event would influence the selection of
the new caps by returning the new preferred caps from its
GST\_QUERY\_CAPS query function. The RECONFIGURE event will set the
GST\_PAD\_FLAG\_NEED\_RECONFIGURE on all pads that it travels over.

It is important to note here that different elements actually have
different responsibilities here:

  - Elements that want to propose a new format upstream need to first
    check if the new caps are acceptable upstream with an ACCEPT\_CAPS
    query. Then they would send a RECONFIGURE event and be prepared to
    answer the CAPS query with the new preferred format. It should be
    noted that when there is no upstream element that can (or wants) to
    renegotiate, the element needs to deal with the currently configured
    format.

  - Elements that operate in transform negotiation according to
    [Transform negotiation](#transform-negotiation) pass the RECONFIGURE
    event upstream. Because these elements simply do a fixed transform
    based on the upstream caps, they need to send the event upstream so
    that it can select a new format.

  - Elements that operate in fixed negotiation ([Fixed
    negotiation](#fixed-negotiation)) drop the RECONFIGURE event. These
    elements can't reconfigure and their output caps don't depend on the
    upstream caps so the event can be dropped.

  - Elements that can be reconfigured on the source pad (source pads
    implementing dynamic negotiation in [Dynamic
    negotiation](#dynamic-negotiation)) should check its
    NEED\_RECONFIGURE flag with `gst_pad_check_reconfigure ()` and it
    should start renegotiation when the function returns TRUE.

## Implementing a CAPS query function

A `_query ()`-function with the GST\_QUERY\_CAPS query type is called
when a peer element would like to know which formats this pad supports,
and in what order of preference. The return value should be all formats
that this elements supports, taking into account limitations of peer
elements further downstream or upstream, sorted by order of preference,
highest preference first.

``` c

static gboolean
gst_my_filter_query (GstPad *pad, GstObject * parent, GstQuery * query)
{
  gboolean ret;
  GstMyFilter *filter = GST_MY_FILTER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS
    {
      GstPad *otherpad;
      GstCaps *temp, *caps, *filt, *tcaps;
      gint i;

      otherpad = (pad == filter->srcpad) ? filter->sinkpad :
                                           filter->srcpad;
      caps = gst_pad_get_allowed_caps (otherpad);

      gst_query_parse_caps (query, &filt);

      /* We support *any* samplerate, indifferent from the samplerate
       * supported by the linked elements on both sides. */
      for (i = 0; i < gst_caps_get_size (caps); i++) {
        GstStructure *structure = gst_caps_get_structure (caps, i);

        gst_structure_remove_field (structure, "rate");
      }

      /* make sure we only return results that intersect our
       * padtemplate */
      tcaps = gst_pad_get_pad_template_caps (pad);
      if (tcaps) {
        temp = gst_caps_intersect (caps, tcaps);
        gst_caps_unref (caps);
        gst_caps_unref (tcaps);
        caps = temp;
      }
      /* filter against the query filter when needed */
      if (filt) {
        temp = gst_caps_intersect (caps, filt);
        gst_caps_unref (caps);
        caps = temp;
      }
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}


```

## Pull-mode Caps negotiation

WRITEME, the mechanism of pull-mode negotiation is not yet fully
understood.

Using all the knowledge you've acquired by reading this chapter, you
should be able to write an element that does correct caps negotiation.
If in doubt, look at other elements of the same type in our git
repository to get an idea of how they do what you want to do.
