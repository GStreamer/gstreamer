# GstPipeline

A `GstPipeline` is usually a toplevel bin and provides all of its children
with a clock.

A `GstPipeline` also provides a toplevel `GstBus` (see [gstbus](additional/design/gstbus.md))

The pipeline also calculates the `running_time` based on the selected
clock (see also clocks.txt and [synchronisation](additional/design/synchronisation.md)).

The pipeline will calculate a global latency for the elements in the
pipeline. (See also [latency](additional/design/latency.md)).

## State changes

In addition to the normal state change procedure of its parent class
`GstBin`, the pipeline performs the following actions during a state
change:

- `NULL` → `READY`:
  - set the bus to non-flushing
- `READY` → `PAUSED`:
  - reset the `running_time` to 0
- `PAUSED` → `PLAYING`:
  - Select a clock.
  - calculate `base_time` using the `running_time`.
  - calculate and distribute latency.
  - set clock and `base_time` on all elements before performing the state
    change.
- `PLAYING` → `PAUSED`:
  - calculate the `running_time` when the pipeline was `PAUSED`.
- `READY` → `NULL`:
  - set the bus to flushing (when auto-flushing is enabled)

The `running_time` represents the total elapsed time, measured in clock
units, that the pipeline spent in the PLAYING state (see
[synchronisation](additional/design/synchronisation.md)). The `running_time` is set to 0 after a
flushing seek.

## Clock selection

Since all of the children of a `GstPipeline` must use the same clock, the
pipeline must select one. This clock selection happens when the
pipeline goes to the `PLAYING` state.

The default clock selection algorithm works as follows:

- If the application selected a clock, use that clock. (see below)

- Use the clock of the most upstream element that can provide one.
This selection is performed by iterating the element starting from
the sinks going upstream.
  - since this selection procedure happens in the `PAUSED` → `PLAYING`
    state change, all the sinks are prerolled and we can thus be
    sure that each sink is linked to some upstream element.
  - in the case of a live pipeline (`NO_PREROLL`), the sink will not
    yet be prerolled and the selection process will select the clock
    of a more upstream element.

- Use `GstSystemClock`, this only happens when no element provides a
usable clock.

The application can influence this clock selection with two methods:
`gst_pipeline_use_clock()` and `gst_pipeline_auto_clock()`.

The `_use_clock()` method forces the use of a specific clock on the
pipeline regardless of what clock providers are available. Passing a
NULL `GstClock *clock` parameter to this method disables all clocking
and makes the pipeline run as fast as possible.

The `_auto_clock()` method removes the fixed clock and reactivates the
auto- matic clock selection algorithm described above.

## GstBus

A `GstPipeline` provides a `GstBus` to the application. The bus can be
retrieved with `gst_pipeline_get_bus()` and can then be used to
retrieve messages posted by the elements in the pipeline (see
[gstbus](additional/design/gstbus.md)).
