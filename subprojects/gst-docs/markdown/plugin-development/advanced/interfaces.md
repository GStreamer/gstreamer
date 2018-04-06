---
title: Interfaces
...

# Interfaces

Previously, in the chapter [Adding Properties][plugin-properties], we have
introduced the concept of GObject properties of controlling an element's
behaviour. This is very powerful, but it has two big disadvantages: first of
all, it is too generic, and second, it isn't dynamic.

The first disadvantage is related to the customizability of the end-user
interface that will be built to control the element. Some properties are
more important than others. Some integer properties are better shown in
a spin-button widget, whereas others would be better represented by a
slider widget. Such things are not possible because the UI has no actual
meaning in the application. A UI widget that represents a bitrate
property is the same as a UI widget that represents the size of a video,
as long as both are of the same `GParamSpec` type. Another problem is
that things like parameter grouping, function grouping, or parameter
coupling are not really possible.

The second problem with parameters is that they are not dynamic. In
many cases, the allowed values for a property are not fixed, but depend
on things that can only be detected at runtime. The names of inputs for
a TV card in a video4linux source element, for example, can only be
retrieved from the kernel driver when we've opened the device; this only
happens when the element goes into the READY state. This means that we
cannot create an enum property type to show this to the user.

The solution to those problems is to create very specialized types of
controls for certain often-used controls. We use the concept of
interfaces to achieve this. The basis of this all is the glib
`GTypeInterface` type. For each case where we think it's useful, we've
created interfaces which can be implemented by elements at their own
will.

One important note: interfaces do *not* replace properties. Rather,
interfaces should be built *next to* properties. There are two important
reasons for this. First of all, properties can be more easily
introspected. Second, properties can be specified on the commandline
(`gst-launch-1.0`).

[plugin-properties]: plugin-development/basics/args.md

## How to Implement Interfaces

Implementing interfaces is initiated in the `_get_type ()` of your
element. You can register one or more interfaces after having registered
the type itself. Some interfaces have dependencies on other interfaces
or can only be registered by certain types of elements. You will be
notified of doing that wrongly when using the element: it will quit with
failed assertions, which will explain what went wrong. If it does, you
need to register support for *that* interface before registering support
for the interface that you're wanting to support. The example below
explains how to add support for a simple interface with no further
dependencies.

``` c
static void gst_my_filter_some_interface_init   (GstSomeInterface *iface);

GType
gst_my_filter_get_type (void)
{
  static GType my_filter_type = 0;

  if (!my_filter_type) {
    static const GTypeInfo my_filter_info = {
      sizeof (GstMyFilterClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_my_filter_class_init,
      NULL,
      NULL,
      sizeof (GstMyFilter),
      0,
      (GInstanceInitFunc) gst_my_filter_init
    };
    static const GInterfaceInfo some_interface_info = {
      (GInterfaceInitFunc) gst_my_filter_some_interface_init,
      NULL,
      NULL
    };

    my_filter_type =
    g_type_register_static (GST_TYPE_ELEMENT,
                "GstMyFilter",
                &my_filter_info, 0);
    g_type_add_interface_static (my_filter_type,
                 GST_TYPE_SOME_INTERFACE,
                                 &some_interface_info);
  }

  return my_filter_type;
}

static void
gst_my_filter_some_interface_init (GstSomeInterface *iface)
{
  /* here, you would set virtual function pointers in the interface */
}

```

Or more
conveniently:

``` c
static void gst_my_filter_some_interface_init   (GstSomeInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GstMyFilter, gst_my_filter,GST_TYPE_ELEMENT,
     G_IMPLEMENT_INTERFACE (GST_TYPE_SOME_INTERFACE,
            gst_my_filter_some_interface_init));

GST_ELEMENT_REGISTER_DEFINE(my_filter, "my-filter", GST_RANK_NONE, GST_TYPE_MY_FILTER);
```

## URI interface

WRITEME

## Color Balance Interface

WRITEME

## Video Overlay Interface

The `GstVideoOverlay` interface is used for 2 main purposes:

  - To get a grab on the Window where the video sink element is going to
    render. This is achieved by either being informed about the Window
    identifier that the video sink element generated, or by forcing the
    video sink element to use a specific Window identifier for
    rendering.

  - To force a redrawing of the latest video frame the video sink
    element displayed on the Window. Indeed if the `GstPipeline` is in
    `GST\_STATE\_PAUSED` state, moving the Window around will damage its
    content. Application developers will want to handle the Expose
    events themselves and force the video sink element to refresh the
    Window's content.

A plugin drawing video output in a video window will need to have that
window at one stage or another. Passive mode simply means that no window
has been given to the plugin before that stage, so the plugin created
the window by itself. In that case the plugin is responsible of
destroying that window when it's not needed any more and it has to tell
the applications that a window has been created so that the application
can use it. This is done using the `have-window-handle` message that can
be posted from the plugin with the `gst_video_overlay_got_window_handle`
method.

As you probably guessed already active mode just means sending a video
window to the plugin so that video output goes there. This is done using
the `gst_video_overlay_set_window_handle` method.

It is possible to switch from one mode to another at any moment, so the
plugin implementing this interface has to handle all cases. There are
only 2 methods that plugins writers have to implement and they most
probably look like that :

``` c
static void
gst_my_filter_set_window_handle (GstVideoOverlay *overlay, guintptr handle)
{
  GstMyFilter *my_filter = GST_MY_FILTER (overlay);

  if (my_filter->window)
    gst_my_filter_destroy_window (my_filter->window);

  my_filter->window = handle;
}

static void
gst_my_filter_xoverlay_init (GstVideoOverlayClass *iface)
{
  iface->set_window_handle = gst_my_filter_set_window_handle;
}

```

You will also need to use the interface methods to post messages when
needed such as when receiving a CAPS event where you will know the video
geometry and maybe create the window.

``` c
static MyFilterWindow *
gst_my_filter_window_create (GstMyFilter *my_filter, gint width, gint height)
{
  MyFilterWindow *window = g_new (MyFilterWindow, 1);
  ...
  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (my_filter), window->win);
}

/* called from the event handler for CAPS events */
static gboolean
gst_my_filter_sink_set_caps (GstMyFilter *my_filter, GstCaps *caps)
{
  gint width, height;
  gboolean ret;
  ...
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  if (!ret) return FALSE;

  gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (my_filter));

  if (!my_filter->window)
    my_filter->window = gst_my_filter_create_window (my_filter, width, height);

  ...
}

```

## Navigation Interface

WRITEME
