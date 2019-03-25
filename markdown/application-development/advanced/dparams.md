---
title: Dynamic Controllable Parameters
...

# Dynamic Controllable Parameters

## Getting Started

GStreamer properties are normally set using `g_object_set()`, but timing these
calls reliably so that the changes affect certain stream times is close to
impossible. The controller subsystem offers a lightweight way to adjust
`GObject` properties over stream-time.

The controller takes time into account; it works by attaching
`GstControlSource`s to properties using control-bindings. Control-sources
provide values for a given time-stamp that are usually in the range of 0.0 to 1.0.
Control-bindings map the control-value to the `GObject` property they are bound
to, converting the type and scaling to the target property's value range. At
run-time the elements continuously pull value changes for the current
stream-time to update the `GObject` properties. GStreamer already includes a
few different `GstControlSource`s and control-bindings, but applications can
define their own by sub-classing the respective base classes.

Most parts of the controller mechanism are implemented in `GstObject`.
The base classes for `GstControlSource`s and control-bindings are also included
in the core library but the existing implementations are contained within
the `gstcontroller` library, so you need to include these headers in your
application's source file as needed:

``` c
#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>
...
```

Beyond including the proper headers, your application should link to the
`gstreamer-controller` shared library. To get the required compiler and
linker flags, you can use:

```
pkg-config --libs --cflags gstreamer-controller-1.0
```

## Setting up parameter control

If we have our pipeline set up and want to control some parameters, we
first need to create a `GstControlSource`. Let's use an interpolation
`GstControlSource`:

``` c
csource = gst_interpolation_control_source_new ();
g_object_set (csource, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
```

Now, we need to attach the `GstControlSource` to the gobject property. This
is done with a control-binding. One control source can be attached to
several object properties (even in different objects) using separate
control-bindings.

``` c
gst_object_add_control_binding (object, gst_direct_control_binding_new (object, "prop1", csource));
```

This type `GstControlSource` takes new property values from a list of
time-stamped parameter changes. The source can e.g. fill gaps by
smoothing parameter changes. This behavior can be configured by setting
the mode property of the `GstControlSource`. Other control sources e.g.
produce a stream of values by calling `sin()` function. They have
parameters to control e.g. the frequency. As `GstControlSource`s are also
`GstObject`s, one can attach `GstControlSource`s to these properties too.

Now we can set some control points. These are time-stamped gdouble
values and are usually in the range of 0.0 to 1.0. A value of 1.0 is
later mapped to the maximum value in the target properties value range.
The values become active when the timestamp is reached. They still stay
in the list. If e.g. the pipeline runs a loop (using a segmented seek),
the control-curve gets repeated as well.

``` c
GstTimedValueControlSource *tv_csource = (GstTimedValueControlSource *)csource;
gst_timed_value_control_source_set (tv_csource, 0 * GST_SECOND, 0.0);
gst_timed_value_control_source_set (tv_csource, 1 * GST_SECOND, 1.0);
```

Now everything is ready to play. If we bound the `GstControlSource` to a volume
property, we will hear a 1 second fade-in. One word of caution: GStreamer's
stock volume element has a `volume` property with a range from 0.0 to 10.0. If
the above `GstControlSource` is attached to this property the volume will ramp
up to 400%\!

One final note: the controller subsystem has a built-in live-mode. Even
though a property has a `GstControlSource` assigned, one can set the
`GObject` property with `g_object_set()`. This is highly useful when binding
the `GObject` properties to GUI widgets. When the user adjusts the value with
the widget, one can set the `GObject` property and this remains active until
the next programmed `GstControlSource` value overrides it. This also works with
smoothed parameters but it does not work for `GstControlSource`s that constantly
update the property, like `GstLFOControlSource`.
