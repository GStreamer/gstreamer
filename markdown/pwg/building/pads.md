---
title: Specifying the pads
...

# Specifying the pads

As explained before, pads are the port through which data goes in and
out of your element, and that makes them a very important item in the
process of element creation. In the boilerplate code, we have seen how
static pad templates take care of registering pad templates with the
element class. Here, we will see how to create actual elements, use an
`_event
()`-function to configure for a particular format and how to register
functions to let data flow through the element.

In the element `_init ()` function, you create the pad from the pad
template that has been registered with the element class in the
`_class_init ()` function. After creating the pad, you have to set a
`_chain ()` function pointer that will receive and process the input
data on the sinkpad. You can optionally also set an `_event ()` function
pointer and a `_query ()` function pointer. Alternatively, pads can also
operate in looping mode, which means that they can pull data themselves.
More on this topic later. After that, you have to register the pad with
the element. This happens like this:

``` c



static void
gst_my_filter_init (GstMyFilter *filter)
{
  /* pad through which data comes in to the element */
  filter->sinkpad = gst_pad_new_from_static_template (
    &sink_template, "sink");
  /* pads are configured here with gst_pad_set_*_function () */



  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  /* pad through which data goes out of the element */
  filter->srcpad = gst_pad_new_from_static_template (
    &src_template, "src");
  /* pads are configured here with gst_pad_set_*_function () */



  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  /* properties initial value */
  filter->silent = FALSE;
}

```
