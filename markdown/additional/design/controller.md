# Controller

The controller subsystem allows to automate element property changes. It
works so that all parameter changes are time based and elements request
property updates at processing time.

## Element view

Elements don’t need to do much. They need to: - mark object properties
that can be changed while processing with `GST_PARAM_CONTROLLABLE` -
call `gst_object_sync_values (self, timestamp)` in the processing
function before accessing the parameters.

All ordered property types can be automated (int, double, boolean,
enum). Other property types can also be automated by using special
control bindings. One can e.g. write a control-binding that updates a
text property based on timestamps.

## Application view

Applications need to setup the property automation. For that they need
to create a `GstControlSource` and attach it to a property using
`GstControlBinding`. Various control-sources and control-bindings exist.
All control sources produce control value sequences in the form of
gdouble values. The control bindings map them to the value range and
type of the bound property.

One control-source can be attached to one or more properties at the same
time. If it is attached multiple times, then each control-binding will
scale and convert the control values to the target property type and
range.

One can create complex control-curves by using a
`GstInterpolationControlSource`. This allows the classic user editable
control-curve (often seen in audio/video editors). Another way is to use
computed control curves. `GstLFOControlSource` can generate various
repetitive signals. Those can be made more complex by chaining the
control sources. One can attach another control-source to e.g. modulate
the frequency of the first `GstLFOControlSource`.

In most cases `GstDirectControlBinding` will be the binding to be used.
Other control bindings are there to handle special cases, such as having
1-4 control- sources and combine their values into a single guint to
control a rgba-color property.

## TODO

* control-source value ranges - control sources should ideally emit values
between \[0.0 and 1.0\] - right now lfo-control-sources emits values
between \[-1.0 and 1.0\] - we can make control-sources announce that or
fix it in a lfo2-control-source

* ranged-control-binding - it might be a nice thing to have a
control-binding that has scale and offset properties - when attaching a
control-source to e.g. volume, one needs to be aware that the values go
from \[0.0 to 4.0\] - we can also have a "mapping-mode"={AS\_IS,
TRANSFORMED} on direct-control-binding and two extra properties that are
used in TRANSFORMED mode

* control-setup descriptions - it would be nice to have a way to parse a
textual control-setup description. This could be used in gst-launch and
in presets. It needs to be complemented with a formatter (for the preset
storage or e.g. for debug logging). - this could be function-style:
direct(control-source=lfo(waveform=*sine*,offset=0.5)) or gst-launch
style (looks weird) lfo wave=sine offset=0.5 \! direct .control-source
