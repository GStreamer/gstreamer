---
title: Supporting Dynamic Parameters
...

# Supporting Dynamic Parameters

Warning, this part describes 0.10 and is outdated.

Sometimes object properties are not powerful enough to control the
parameters that affect the behaviour of your element. When this is the
case you can mark these parameters as being Controllable. Aware
applications can use the controller subsystem to dynamically adjust the
property values over time.

## Getting Started

The controller subsystem is contained within the `gstcontroller`
library. You need to include the header in your element's source file:

``` c
...
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
...

```

Even though the `gstcontroller` library may be linked into the host
application, you should make sure it is initialized in your
`plugin_init` function:

``` c
  static gboolean
  plugin_init (GstPlugin *plugin)
  {
    ...
    /* initialize library */
    gst_controller_init (NULL, NULL);
    ...
  }

```

It makes no sense for all GObject parameter to be real-time controlled.
Therefore the next step is to mark controllable parameters. This is done
by using the special flag `GST_PARAM_CONTROLLABLE`. when setting up
GObject params in the `_class_init` method.

``` c
  g_object_class_install_property (gobject_class, PROP_FREQ,
      g_param_spec_double ("freq", "Frequency", "Frequency of test signal",
          0.0, 20000.0, 440.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

```

## The Data Processing Loop

In the last section we learned how to mark GObject params as
controllable. Application developers can then queue parameter changes
for these parameters. The approach the controller subsystem takes is to
make plugins responsible for pulling the changes in. This requires just
one action:

``` c
    gst_object_sync_values(element,timestamp);

```

This call makes all parameter-changes for the given timestamp active by
adjusting the GObject properties of the element. Its up to the element
to determine the synchronisation rate.

### The Data Processing Loop for Video Elements

For video processing elements it is the best to synchronise for every
frame. That means one would add the `gst_object_sync_values()` call
described in the previous section to the data processing function of the
element.

### The Data Processing Loop for Audio Elements

For audio processing elements the case is not as easy as for video
processing elements. The problem here is that audio has a much higher
rate. For PAL video one will e.g. process 25 full frames per second, but
for standard audio it will be 44100 samples. It is rarely useful to
synchronise controllable parameters that often. The easiest solution is
also to have just one synchronisation call per buffer processing. This
makes the control-rate depend on the buffer size.

Elements that need a specific control-rate need to break their data
processing loop to synchronise every n-samples.
