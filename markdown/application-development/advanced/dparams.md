---
title: Dynamic Controllable Parameters
...

# Dynamic Controllable Parameters

## Getting Started

The controller subsystem offers a lightweight way to adjust gobject
properties over stream-time. Normally these properties are changed using
`g_object_set()`. Timing those calls reliably so that the changes affect
certain stream times is close to impossible. The controller takes time
into account. It works by attaching control-sources to properties using
control-bindings. Control-sources provide values for a given time-stamp
that are usually in the range of 0.0 to 1.0. Control-bindings map the
control-value to a gobject property they are bound to - converting the
type and scaling to the target property value range. At run-time the
elements continuously pull values changes for the current stream-time to
update the gobject properties. GStreamer includes a few different
control-sources and control-bindings already, but applications can
define their own by sub-classing from the respective base classes.

Most parts of the controller mechanism is implemented in GstObject. Also
the base classes for control-sources and control-bindings are included
in the core library. The existing implementations are contained within
the `gstcontroller` library. You need to include the header in your
application's source file:

``` c
...
#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>
...

```

Your application should link to the shared library
`gstreamer-controller`. One can get the required flag for compiler and
linker by using pkg-config for gstreamer-controller-1.0.

## Setting up parameter control

If we have our pipeline set up and want to control some parameters, we
first need to create a control-source. Lets use an interpolation
control-source:

``` c
  csource = gst_interpolation_control_source_new ();
  g_object_set (csource, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);

```

Now we need to attach the control-source to the gobject property. This
is done with a control-binding. One control source can be attached to
several object properties (even in different objects) using separate
control-bindings.

``` c
      gst_object_add_control_binding (object, gst_direct_control_binding_new (object, "prop1", csource));

```

This type control-source takes new property values from a list of
time-stamped parameter changes. The source can e.g. fill gaps by
smoothing parameter changes This behavior can be configured by setting
the mode property of the control-source. Other control sources e.g.
produce a stream of values by calling `sin()` function. They have
parameters to control e.g. the frequency. As control-sources are
GstObjects too, one can attach control-sources to these properties too.

Now we can set some control points. These are time-stamped gdouble
values and are usually in the range of 0.0 to 1.0. A value of 1.0 is
later mapped to the maximum value in the target properties value range.
The values become active when the timestamp is reached. They still stay
in the list. If e.g. the pipeline runs a loop (using a segmented seek),
the control-curve gets repeated as
well.

``` c
  GstTimedValueControlSource *tv_csource = (GstTimedValueControlSource *)csource;
  gst_timed_value_control_source_set (tv_csource, 0 * GST_SECOND, 0.0);
  gst_timed_value_control_source_set (tv_csource, 1 * GST_SECOND, 1.0);

```

Now everything is ready to play. If the control-source is e.g. bound to
a volume property, we will head a fade-in over 1 second. One word of
caution, the volume element that comes with gstreamer has a value range
of 0.0 to 4.0 on its volume property. If the above control-source is
attached to the property the volume will ramp up to 400%\!

One final note - the controller subsystem has a built-in live-mode. Even
though a property has a control-source assigned one can change the
GObject property through the `g_object_set()`. This is highly useful
when binding the GObject properties to GUI widgets. When the user
adjusts the value with the widget, one can set the GObject property and
this remains active until the next programmed control-source value
overrides it. This also works with smoothed parameters. It does not work
for control-sources that constantly update the property (e.g. the
lfo\_control\_source).
