# Tracing

This subsystem will provide a mechanism to get structured tracing info
from GStreamer applications. This can be used for post-run analysis as
well as for live introspection.

## Use cases

  - I’d like to get statistics from a running application.

  - I’d like to to understand which parts of my pipeline use how many
    resources.

  - I’d like to know which parts of the pipeline use how much memory.

  - I’d like to know about ref-counts of parts in the pipeline to find
    ref-count issues.

## Non use-cases

  - Some element in the pipeline does not play along the rules, find out
    which one. This could be done with generic tests.

## Design

The system brings the following new items: core hooks: probes in the
core api, that will expose internal state when tracing is in use
tracers: plugin features that can process data from the hooks and emit a
log tracing front-ends: applications that consume logs from tracers

Like the logging, the tracer hooks can be compiled out and if not use a
local condition to check if active.

Certain GStreamer core function (such as `gst_pad_push()` or
`gst_element_add_pad()`) will call into the tracer subsystem to dispatch
into active tracing modules. Developers will be able to select a list of
plugins by setting an environment variable, such as
`GST_TRACERS="meminfo;dbus"`. One can also pass parameters to plugins, e.g:

```
GST_TRACERS='leaks(filters="GstEvent,GstMessage",stack-traces-flags=none);latency(flags=pipeline+element+reported)'
```

When then plugins are loaded, we’ll add them to certain hooks according to
which they are interested in.

Right now tracing info is logged as `GstStructures` to the TRACE level.
Idea: Another env var `GST_TRACE_CHANNEL` could be used to send the
tracing to a file or a socket. See
<https://bugzilla.gnome.org/show_bug.cgi?id=733188> for discussion on
these environment variables.

## Hook api

We’ll wrap interesting api calls with two macros, e.g. `gst_pad_push()`:

``` c
GstFlowReturn gst_pad_push (GstPad * pad, GstBuffer * buffer) {
  GstFlowReturn res;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  GST_TRACER_PAD_PUSH_PRE (pad, buffer);
  res = gst_pad_push_data (pad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH, buffer);
  GST_TRACER_PAD_PUSH_POST (pad, res);
  return res;
}
```

TODO(ensonic): gcc has some magic for wrapping functions -
<http://gcc.gnu.org/onlinedocs/gcc/Constructing-Calls.html> -
<http://www.clifford.at/cfun/gccfeat/#gccfeat05.c>

TODO(ensonic): we should eval if we can use something like jump_label
in the kernel - <http://lwn.net/Articles/412072/> +
<http://lwn.net/Articles/435215/> -
<http://lxr.free-electrons.com/source/kernel/jump_label.c> -
<http://lxr.free-electrons.com/source/include/linux/jump_label.h> -
<http://lxr.free-electrons.com/source/arch/x86/kernel/jump_label.c>
TODO(ensonic): liblttng-ust provides such a mechanism for user-space -
but this is mostly about logging traces - it is linux specific :/

In addition to api hooks we should also provide timer hooks. Interval
timers are useful to get e.g. resource usage snapshots. Also absolute
timers might make sense. All this could be implemented with a clock
thread. We can use another env-var `GST_TRACE_TIMERS="100ms,75ms"` to
configure timers and then pass them to the tracers like,
`GST_TRACERS="rusage(timer=100ms);meminfo(timer=75ms)"`. Maybe we can
create them ad-hoc and avoid the `GST_TRACE_TIMERS` var.

Hooks (\* already implemented)

```
* gst_bin_add
* gst_bin_remove
* gst_element_add_pad
* gst_element_post_message
* gst_element_query
* gst_element_remove_pad
* gst_element_factory_make
* gst_pad_link
* gst_pad_pull_range
* gst_pad_push
* gst_pad_push_list
* gst_pad_push_event
* gst_pad_unlink
```

## Tracer api

Tracers are plugin features. They have a simple api:

class init Here the tracers describe the data they will emit.

instance init Tracers attach handlers to one or more hooks using
`gst_tracing_register_hook()`. In case they are configurable, they can
read the options from the *params* property. This is the extra detail
from the environment var.

hook functions Hooks marshal the parameters given to a trace hook into
varargs and also add some extra into such as a timestamp. Hooks will be
called from misc threads. The trace plugins should only consume (=read)
the provided data. Expensive computation should be avoided to not affect
the execution too much. Most trace plugins will log data to a trace
channel.

instance destruction Tracers can output results and release data. This
would ideally be done at the end of the applications, but `gst_deinit()`
is not mandatory. `gst_tracelib` was using a `gcc_destructor`. Ideally
tracer modules log data as they have them and leave aggregation to a
tool that processes the log.

## tracer event classes

Most tracers will log some kind of *events* : a data transfer, an event,
a message, a query or a measurement. Every tracer should describe the
data format. This way tools that process tracer logs can show the data
in a meaningful way without having to know about the tracer plugin.

Tracers can use `gst_tracer_record_new` in their `tracer_class_init()`
to describe their format:

``` c
fmt = gst_tracer_record_new ("thread-rusage.class",
    // value in the log record (order does not matter)
    // *thread-id* is a *key* to relate the record to something as indicated
    // by *scope* substructure
    "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
        "type", G_TYPE_GTYPE, G_TYPE_GUINT64,
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_THREAD,
        NULL),
    // next value in the record
    // *average-cpuload* is a measurement as indicated by the *value*
    // substructure
    "average-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
        // value type
        "type", G_TYPE_GTYPE, G_TYPE_UINT,
        // human readable description, that can be used as a graph label
        "description", G_TYPE_STRING, "average cpu usage per thread",
        // flags that help to use the right graph type
        // flags { aggregated, windowed, cumulative, … }
        "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_AGGREGATED,
        // value range
        "min", G_TYPE_UINT, 0,
        "max", G_TYPE_UINT, 100,
        NULL),
    …
    NULL);
```

Later tracers can use the `GstTracerRecord` instance to log values efficiently:

``` c
gst_tracer_record_log (fmt, (guint64) (guintptr) thread_id, avg_cpuload);
```

Below a few more examples for parts of tracer classes:

An optional value. Since the PTS can be GST_CLOCK_TIME_NONE and that is (-1),
we don't want to log this.
``` c
"buffer-pts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
    "type", G_TYPE_GTYPE, G_TYPE_UINT64,
    "description", G_TYPE_STRING, "presentation timestamp of the buffer in ns",
    "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_OPTIONAL,
    "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
    "max", G_TYPE_UINT64, G_MAXUINT64,
    NULL),
```

In the tracer code an optional value is logged using a boolean + the value,
where the boolean indicated the presence:

``` c
GstClockTime pts = GST_BUFFER_PTS (buf);

gst_tracer_record_log (fmt, ..., GST_CLOCK_TIME_IS_VALID (pts), pts, ...);

```


A few ideas that are not yet in the above spec:

- it would be nice to describe the unit of values
    - putting it into the description is not flexible though, e.g. time
      would be a guint64 but a ui would reformat it to e.g. h:m:s.ms
    - other units are e.g.: percent, per-mille, or kbit/s
- we’d like to have some metadata on scopes
    - e.g. we’d like to log the thread-names, so that a UI can show
      that instead of thread-ids
    - ordering: e.g. in the latency tracer we'd like to order by 'sink'
      and then by 'src'

- unique instance ids
    - the stats tracer logs *new-element* and *new-pad* messages
    - they add a unique *ix* to each instance as the memory ptr or the object
      name can be reused for new instances, the data is attached to the
      objects as qdata
    - the latency tracer would like to also reference this metadata
      (right now, it relies on unique element names)
    - the relationship between a scope 'element-ix' ('related-to'=_ELEMENT)
      and an earlier message 'new-element' that has scope 'ix'
      ('related-to'=_ELEMENT) and value 'name' is not obvious

Right now we log the classes as structures, this is important so that the log
is self contained. It would be nice to add them to the registry, so that
gst-inspect can show them. We could also consider to add each value as a
READONLY gobject property. The property has name/description. We could use
qdata for scope and flags (or have some new property flags). We would also
need a new "notify" signal, so that value-change notifications would include a
time-stamp. This way the tracers would not needs to be aware of the
logging. The core tracer would register the notify handlers and emit the
log. Or we just add a `gst_tracer_class_install_event()` and that
mimics the `g_object_class_install_property()`.

Frontends can:
- do an events over time histogram
- plot curves of values over time or deltas
- show gauges
- collect statistics (min, max, avg, …)

## latency

- register to buffer, event and query flow
- send custom event on buffer flow at source elements
- catch events on event transfer at sink pads

## meminfo (not yet implemented)

- register to an interval-timer hook.
- call `mallinfo()` and log memory usage rusage
- register to an interval-timer hook.
- call `getrusage()` and log resource usage

## dbus (not yet implemented)

- provide a dbus iface to announce applications that are traced
- tracing UIs can use the dbus iface to find the channels where logging and
  tracing is getting logged to
- one would start the tracing UI first and when the application is started with
  tracing activated, the dbus plugin will announce the new application,
  upon which the tracing UI can start reading from the log channels, this avoid
  missing some data

## topology (not yet implemented)

- register to pipeline topology hooks
- tracing UIs can show a live pipeline graph

## stats

- register to buffer, event, message and query flow
- tracing apps can do e.g. statistics

## refcounts (not yet implemented)

- log ref-counts of objects
- just logging them outside of glib/gobject would still make it hard to detect
  issues though

## opengl (not yet implemented)

- upload/download times
- there is not hardware agnostic way to get e.g. memory usage info (gl
extensions)

## memory (not yet implemented)

- trace live instance (and pointer to the memory)
- use an atexit handler to dump leaked instance
  https://bugzilla.gnome.org/show_bug.cgi?id=756760#c6

## leaks

- track creation/destruction of `GstObject` and `GstMiniObject`

- log those which are still alive when app is exiting and raise an
  error if any

- The tracer takes several parameters in a `GstStructure` like syntax (without the structure name):
    - check-refs (boolean): Whether to also track object ref and unref operations
        example: `GST_TRACERS=leaks(check-refs=true)` COMMAND
    - stack-traces-flags: Flags to use when generating stack trace (does not generate stack trace
      if not set), valid values are “full” to retrieve as much information as possible in the
      backtrace, or “none” for a simple backtrace (usually does not contain line number or source files).
      This may significantly increase memory consumption. (You can also set the `GST_LEAKS_TRACER_STACK_TRACE`
      environment variable for that).
    - filters: (string): A comma separated list of object types to trace (make sure to enclose in
      quotation marks)

**Run the leaks tracer on all `GstProxyPad` objects logging the references with a full backtraces**

```
GST_TRACERS=leaks(stack-traces-flags=full,filters=”GstProxyPad”,check-refs=true) COMMAND
```

**Run the leaks tracer on all (mini)objects logging the references with less complete backtraces**

```
GST_TRACERS=leaks(stack-traces-flags=fast,check-refs=true) COMMAND
```

- If the `GST_LEAKS_TRACER_SIG` env variable is defined the tracer
  will handle the following UNIX signals:

- SIGUSR1: log alive objects

- SIGUSR2: create a checkpoint and print a list of objects created and
  destroyed since the previous checkpoint.

## gst-debug-viewer

gst-debug-viewer could be given the trace log in addition to the debug
log (or a combined log). Alternatively it would show a dialog that shows
all local apps (if the dbus plugin is loaded) and read the log streams
from the sockets/files that are configured for the app.

## gst-tracer

Counterpart of gst-tracelib-ui.

## gst-stats

A terminal app that shows summary/running stats like the summary
gst-tracelib shows at the end of a run. Currently only shows an
aggregated status.

## live-graphers

Maybe we can even feed the log into existing live graphers, with a
little driver * <https://github.com/dkogan/feedgnuplot>

  - should tracers log into the debug.log or into a separate log?

  - separate log

  - use a binary format?

  - worse performance (we’re writing two logs at the same time)

  - need to be careful when people to `GST_DEBUG_CHANNEL=stderr` and
    `GST_TRACE_CHANNEL=stderr` (use a shared channel, but what about the
    formats?)

  - debug log

  - the tracer subsystem would need to log the `GST_TRACE` at a level
    that is active

  - should the tracer call `gst_debug_category_set_threshold()` to
    ensure things work, even though the levels don’t make a lot of sense
    here

  - make logging a tracer (a hook in `gst_debug_log_valist()`, move
    `gst_debug_log_default()` to the tracer module)

  - log all debug log to the tracer log, some of the current logging
    statements can be replaced by generic logging as shown in the
    log-tracer

  - add tools/gst-debug to extract a human readable debug log from the
    trace log

  - we could maintain a list of log functions, where
    `gst_tracer_log_trace()` is the default one. This way e.g.
    `gst-validate` could consume the traces directly.

  - when hooking into a timer, should we just have some predefined
    intervals?

  - can we add a tracer module that registers the timer hook? then we
    could do `GST_TRACER="timer(10ms);rusage"` right now the tracer hooks
    are defined as an enum though.

  - when connecting to a running app, we can’t easily get the *current*
    state if logging is using a socket, as past events are not
    explicitly stored, we could determine the current topology and emit
    events with `GST_CLOCK_TIME_NONE` as ts to indicate that the events
    are synthetic.

  - we need stable ids for scopes (threads, elements, pads)

  - the address can be reused

  - we can use `gst_util_seqnum_next()`

  - something like `gst_object_get_path_string()` won’t work as
    objects are initially without parent

  - right now the tracing-hooks are enabled/disabled from configure with
    `--{enable,disable}-gst-tracer-hooks` The tracer code and the plugins
    are still built though. We should add a
    `--{enable,disable}-gst-tracer` to disabled the whole system,
    allthough this is a bit confusing with the `--{enable,disable}-trace`
    option we have already.

## Try it

### Traces for buffer flow, events and messages in TRACE level:

```
GST_DEBUG="GST_TRACER:7,GST_BUFFER*:7,GST_EVENT:7,GST_MESSAGE:7"
GST_TRACERS=log gst-launch-1.0 fakesrc num-buffers=10 ! fakesink -
```

### Print some pipeline stats on exit:

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage"
GST_DEBUG_FILE=trace.log gst-launch-1.0 fakesrc num-buffers=10
sizetype=fixed ! queue ! fakesink && gst-stats-1.0 trace.log
```

### get ts, average-cpuload, current-cpuload, time and plot

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage"
GST_DEBUG_FILE=trace.log /usr/bin/gst-play-1.0 $HOME/Videos/movie.mp4 &&
./scripts/gst-plot-traces.sh --format=png | gnuplot eog trace.log.*.png
```

### print processing latencies

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS=latency gst-launch-1.0 \
audiotestsrc num-buffers=10 ! audioconvert ! volume volume=0.7 ! \
autoaudiosink
```

### print processing latencies for each element

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS=latency(flags=element) gst-launch-1.0 \
audiotestsrc num-buffers=10 ! audioconvert ! volume volume=0.7 ! \
autoaudiosink
```

### print reported latencies for each element

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS=latency(flags=reported) gst-launch-1.0 \
audiotestsrc num-buffers=10 ! audioconvert ! volume volume=0.7 ! \
autoaudiosink
```

### print all type of latencies for a pipeline

```
GST_DEBUG="GST_TRACER:7" \
GST_TRACERS=latency(flags=pipeline+element+reported) gst-launch-1.0 \
alsasrc num-buffers=20 ! flacenc ! identity ! \
fakesink
```

### Raise a warning if a leak is detected

```
GST_TRACERS="leaks" gst-launch-1.0 videotestsrc num-buffers=10 !
fakesink
```

### check if any GstEvent or GstMessage is leaked and raise a warning

```
GST_DEBUG="GST_TRACER:7" GST_TRACERS="leaks(GstEvent,GstMessage)"
gst-launch-1.0 videotestsrc num-buffers=10 ! fakesink
```

## Performance

```
run ./tests/benchmarks/tracing.sh <tracer(s)> <media>

egrep -c "(proc|thread)-rusage" trace.log 658618 grep -c
"gst_tracer_log_trace" trace.log 823351
```

- we can optimize most of it by using quarks in structures or
eventually avoid structures totally
