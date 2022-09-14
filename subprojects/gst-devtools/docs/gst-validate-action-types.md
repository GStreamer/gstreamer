# GstValidate action types
## meta


``` validate-scenario
meta,
    [allow-errors=(boolean)],
    [duration=(double, int)],
    [handles-states=(boolean)],
    [ignore-eos=(boolean)],
    [is-config=(boolean)],
    [max-dropped=(int)],
    [max-latency=(double, int)],
    [min-audio-track=(int)],
    [min-media-duration=(double)],
    [min-video-track=(int)],
    [need-clock-sync=(boolean)],
    [pipeline-name=(string)],
    [reverse-playback=(boolean)],
    [seek=(boolean)],
    [summary=(string)];
```

Scenario metadata.
NOTE: it used to be called "description"
 * Implementer namespace: core
 * Is config action (meaning it will be executing right at the beginning of the execution of the pipeline)

### Parameters

* `allow-errors`:(optional): Ignore error messages and keep executing the
scenario when it happens. By default a 'stop' action is generated on ERROR messages

  Possible types: `boolean`

  Default: false

* `duration`:(optional): Lets the user know the time the scenario needs to be fully executed

  Possible types: `double, int`

  Default: infinite (GST_CLOCK_TIME_NONE)

* `handles-states`:(optional): Whether the scenario handles pipeline state changes from the beginning
in that case the application should not set the state of the pipeline to anything
and the scenario action will be executed from the beginning

  Possible types: `boolean`

  Default: false

* `ignore-eos`:(optional): Ignore EOS and keep executing the scenario when it happens.
 By default a 'stop' action is generated one EOS

  Possible types: `boolean`

  Default: false

* `is-config`:(optional): Whether the scenario is a config only scenario

  Possible types: `boolean`

  Default: false

* `max-dropped`:(optional): The maximum number of buffers which can be dropped by the QoS system allowed for this pipeline.
It can be overridden using core configuration, like for example by defining the env variable GST_VALIDATE_CONFIG=core,max-dropped=100

  Possible types: `int`

  Default: infinite (-1)

* `max-latency`:(optional): The maximum latency in nanoseconds allowed for this pipeline.
It can be overridden using core configuration, like for example by defining the env variable GST_VALIDATE_CONFIG=core,max-latency=33000000

  Possible types: `double, int`

  Default: infinite (GST_CLOCK_TIME_NONE)

* `min-audio-track`:(optional): Lets the user know the minimum number of audio tracks the stream needs to contain
for the scenario to be usable

  Possible types: `int`

  Default: 0

* `min-media-duration`:(optional): Lets the user know the minimum duration of the stream for the scenario
to be usable

  Possible types: `double`

  Default: 0.0

* `min-video-track`:(optional): Lets the user know the minimum number of video tracks the stream needs to contain
for the scenario to be usable

  Possible types: `int`

  Default: 0

* `need-clock-sync`:(optional): Whether the scenario needs the execution to be synchronized with the pipeline's
clock. Letting the user know if it can be used with a 'fakesink sync=false' sink

  Possible types: `boolean`

  Default: true if some action requires a playback-time false otherwise

* `pipeline-name`:(optional): The name of the GstPipeline on which the scenario should be executed.
It has the same effect as setting the pipeline using pipeline_name->scenario_name.

  Possible types: `string`

  Default: NULL

* `reverse-playback`:(optional): Whether the scenario plays the stream backward

  Possible types: `boolean`

  Default: false

* `seek`:(optional): Whether the scenario executes seek actions or not

  Possible types: `boolean`

  Default: false

* `summary`:(optional): Whether the scenario is a config only scenario (ie. explain what it does)

  Possible types: `string`

  Default: 'Nothing'

## seek


``` validate-scenario
seek,
    flags=(string describing the GstSeekFlags to set),
    start=(double or string (GstClockTime)),
    [rate=(double)],
    [start_type=(string)],
    [stop=(double or string (GstClockTime))],
    [stop_type=(string)],
    [playback-time=(double,string)];
```

Seeks into the stream. This is an example of a seek happening when the stream reaches 5 seconds
or 1 eighth of its duration and seeks to 10s or 2 eighths of its duration:
  seek, playback-time="min(5.0, (duration/8))", start="min(10, 2*(duration/8))", flags=accurate+flush
 * Implementer namespace: core

### Parameters

* `flags`:(mandatory): The GstSeekFlags to use

  Possible types: `string describing the GstSeekFlags to set`

* `start`:(mandatory): The starting value of the seek

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double or string (GstClockTime)`

* `rate`:(optional): The rate value of the seek

  Possible types: `double`

  Default: 1.0

* `start_type`:(optional): The GstSeekType to use for the start of the seek, in:
  [none, set, end]

  Possible types: `string`

  Default: set

* `stop`:(optional): The stop value of the seek

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double or string (GstClockTime)`

  Default: GST_CLOCK_TIME_NONE

* `stop_type`:(optional): The GstSeekType to use for the stop of the seek, in:
  [none, set, end]

  Possible types: `string`

  Default: set

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## pause


``` validate-scenario
pause,
    [duration=(double or string (GstClockTime))],
    [playback-time=(double,string)];
```

Sets pipeline to PAUSED. You can add a 'duration'
parameter so the pipeline goes back to playing after that duration
(in second)
 * Implementer namespace: core

### Parameters

* `duration`:(optional): The duration during which the stream will be paused

  Possible types: `double or string (GstClockTime)`

  Default: 0.0

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## play


``` validate-scenario
play,
    [playback-time=(double,string)];
```

Sets the pipeline state to PLAYING
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## stop


``` validate-scenario
stop,
    [playback-time=(double,string)];
```

Stops the execution of the scenario. It will post a 'request-state' message on the bus with NULL as a requested state and the application is responsible for stopping itself. If you override that action type, make sure to link up.
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## eos


``` validate-scenario
eos,
    [playback-time=(double,string)];
```

Sends an EOS event to the pipeline
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## switch-track


``` validate-scenario
switch-track,
    [index=(string: to switch track relatively
int: To use the actual index to use)],
    [type=(string)],
    [playback-time=(double,string)];
```

The 'switch-track' command can be used to switch tracks.
 * Implementer namespace: core

### Parameters

* `index`:(optional): Selects which track of this type to use: it can be either a number,
which will be the Nth track of the given type, or a number with a '+' or
'-' prefix, which means a relative change (eg, '+1' means 'next track',
'-1' means 'previous track')

  Possible types: `string: to switch track relatively
int: To use the actual index to use`

  Default: +1

* `type`:(optional): Selects which track type to change (can be 'audio', 'video', or 'text').

  Possible types: `string`

  Default: audio

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## wait


``` validate-scenario
wait,
    [check=(structure)],
    [duration=(double or string (GstClockTime))],
    [expected-values=(structure)],
    [message-type=(string)],
    [non-blocking=(string)],
    [on-clock=(boolean)],
    [signal-name=(string)],
    [target-element-factory-name=(string)],
    [target-element-name=(string)],
    [playback-time=(double,string)];
```

Waits for signal 'signal-name', message 'message-type', or during 'duration' seconds
 * Implementer namespace: core

### Parameters

* `check`:(optional): The check action to execute when non blocking signal is received

  Possible types: `structure`

  Default: (null)

* `duration`:(optional): the duration while no other action will be executed

  Possible types: `double or string (GstClockTime)`

  Default: (null)

* `expected-values`:(optional): Expected values in the message structure (valid only when `message-type`). Example: wait, on-client=true, message-type=buffering, expected-values=[values, buffer-percent=100]

  Possible types: `structure`

  Default: (null)

* `message-type`:(optional): The name of the message type to wait for (on @target-element-name if specified)

  Possible types: `string`

  Default: (null)

* `non-blocking`:(optional): **Only for signals**.Make the action non blocking meaning that next actions will be
executed without waiting for the signal to be emitted.

  Possible types: `string`

  Default: (null)

* `on-clock`:(optional): Wait until the test clock gets a new pending entry.
See #gst_test_clock_wait_for_next_pending_id.

  Possible types: `boolean`

  Default: (null)

* `signal-name`:(optional): The name of the signal to wait for on @target-element-name. To ensure that the signal is executed without blocking while waiting for it you can set the field 'non-blocking=true'.

  Possible types: `string`

  Default: (null)

* `target-element-factory-name`:(optional): The name factory for which to wait @signal-name on

  Possible types: `string`

  Default: (null)

* `target-element-name`:(optional): The name of the GstElement to wait @signal-name on.

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## dot-pipeline


``` validate-scenario
dot-pipeline,
    [playback-time=(double,string)];
```

Dots the pipeline (the 'name' property will be used in the dot filename).
For more information have a look at the GST_DEBUG_BIN_TO_DOT_FILE documentation.
Note that the GST_DEBUG_DUMP_DOT_DIR env variable needs to be set
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-rank


``` validate-scenario
set-rank,
    name=(string),
    rank=(string, int);
```

Changes the ranking of a particular plugin feature(s)
 * Implementer namespace: core
 * Is config action (meaning it will be executing right at the beginning of the execution of the pipeline)

### Parameters

* `name`:(mandatory): The name of a GstFeature or GstPlugin

  Possible types: `string`

* `rank`:(mandatory): The GstRank to set on @name

  Possible types: `string, int`

## remove-feature


``` validate-scenario
remove-feature,
    name=(string);
```

Remove a plugin feature(s) or a plugin from the registry
 * Implementer namespace: core
 * Is config action (meaning it will be executing right at the beginning of the execution of the pipeline)

### Parameters

* `name`:(mandatory): The name of a GstFeature or GstPlugin to remove

  Possible types: `string`

## set-feature-rank


``` validate-scenario
set-feature-rank,
    feature-name=(string),
    rank=(string, int);
```

Changes the ranking of a particular plugin feature
 * Implementer namespace: core
 * Is config action (meaning it will be executing right at the beginning of the execution of the pipeline)

### Parameters

* `feature-name`:(mandatory): The name of a GstFeature

  Possible types: `string`

* `rank`:(mandatory): The GstRank to set on @feature-name

  Possible types: `string, int`

## set-state


``` validate-scenario
set-state,
    state=(string),
    [playback-time=(double,string)];
```

Changes the state of the pipeline to any GstState
 * Implementer namespace: core

### Parameters

* `state`:(mandatory): A GstState as a string, should be in: 
    * ['null', 'ready', 'paused', 'playing']

  Possible types: `string`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-vars


``` validate-scenario
set-vars,
    [playback-time=(double,string)];
```

Define vars to be used in other actions.
For example you can define vars for buffer checksum to be used in the "check-last-sample" action type as follow:

```
 set-vars, frame1=SomeRandomHash1,frame2=Anotherhash...
 check-last-sample, checksum=frame1
```

 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-timed-value-properties


``` validate-scenario
set-timed-value-properties,
    timestamp=(string or float (GstClockTime)),
    [binding-type=(string)],
    [interpolation-mode=(string)],
    [source-type=(string)],
    [playback-time=(double,string)];
```

Sets GstTimedValue on pads on elements properties using GstControlBindings
and GstControlSource as defined in the parameters.
The properties values to set will be defined as:

```
element-name.padname::property-name=new-value
```

> NOTE: `.padname` is not needed if setting a property on an element

This action also adds necessary control source/control bindings.

 * Implementer namespace: core

### Parameters

* `timestamp`:(mandatory): The timestamp of the keyframe

  Possible types: `string or float (GstClockTime)`

* `binding-type`:(optional): The name of the type of binding to use

  Possible types: `string`

  Default: direct

* `interpolation-mode`:(optional): The name of the GstInterpolationMode to set on the source

  Possible types: `string`

  Default: linear

* `source-type`:(optional): The name of the type of ControlSource to use

  Possible types: `string`

  Default: GstInterpolationControlSource

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## check-properties


``` validate-scenario
check-properties,
    [playback-time=(double,string)];
```

Check elements and pads properties values.
The properties values to check will be defined as:

```
element-name.padname::property-name
```

> NOTE: `.padname` is not needed if checking an element property


 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-properties


``` validate-scenario
set-properties,
    [playback-time=(double,string)];
```

Set elements and pads properties values.
The properties values to set will be defined as:

```
    element-name.padname::property-name
```

> NOTE: `.padname` is not needed if set an element property


 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-property


``` validate-scenario
set-property,
    property-name=(string),
    property-value=(The same type of @property-name),
    [on-all-instances=(boolean)],
    [target-element-factory-name=(string)],
    [target-element-klass=(string)],
    [target-element-name=(string)],
    [playback-time=(double,string)];
```

Sets a property of an element or klass of elements in the pipeline.
Besides property-name and value, either 'target-element-name' or
'target-element-klass' needs to be defined
 * Implementer namespace: core

### Parameters

* `property-name`:(mandatory): The name of the property to set on @target-element-name

  Possible types: `string`

* `property-value`:(mandatory): The value of @property-name to be set on the element

  Possible types: `The same type of @property-name`

* `on-all-instances`:(optional): Whether to set property on all instances matching the requirements

  Possible types: `boolean`

  Default: (null)

* `target-element-factory-name`:(optional): The name factory for which to set a property on built elements

  Possible types: `string`

  Default: (null)

* `target-element-klass`:(optional): The klass of the GstElements to set a property on

  Possible types: `string`

  Default: (null)

* `target-element-name`:(optional): The name of the GstElement to set a property on

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)
     optional                   : Don't raise an error if this action hasn't been executed or failed
                                  ### Possible types:
                                    boolean
                                  Default: false

## check-property


``` validate-scenario
check-property,
    property-name=(string),
    property-value=(The same type of @property-name),
    [target-element-factory-name=(string)],
    [target-element-klass=(string)],
    [target-element-name=(string)],
    [playback-time=(double,string)];
```

Check the value of property of an element or klass of elements in the pipeline.
Besides property-name and value, either 'target-element-name' or
'target-element-klass' needs to be defined
 * Implementer namespace: core

### Parameters

* `property-name`:(mandatory): The name of the property to set on @target-element-name

  Possible types: `string`

* `property-value`:(mandatory): The expected value of @property-name

  Possible types: `The same type of @property-name`

* `target-element-factory-name`:(optional): The name factory for which to check a property value on built elements

  Possible types: `string`

  Default: (null)

* `target-element-klass`:(optional): The klass of the GstElements to check a property on

  Possible types: `string`

  Default: (null)

* `target-element-name`:(optional): The name of the GstElement to check a property value

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-debug-threshold


``` validate-scenario
set-debug-threshold,
    debug-threshold=(string),
    [playback-time=(double,string)];
```

Sets the debug level to be used, same format as
setting the GST_DEBUG env variable
 * Implementer namespace: core

### Parameters

* `debug-threshold`:(mandatory): String defining debug threshold
See gst_debug_set_threshold_from_string

  Possible types: `string`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## emit-signal


``` validate-scenario
emit-signal,
    signal-name=(string),
    target-element-name=(string),
    [params=(ValueArray)],
    [playback-time=(double,string)];
```

Emits a signal to an element in the pipeline
 * Implementer namespace: core

### Parameters

* `signal-name`:(mandatory): The name of the signal to emit on @target-element-name

  Possible types: `string`

* `target-element-name`:(mandatory): The name of the GstElement to emit a signal on

  Possible types: `string`

* `params`:(optional): The signal parameters

  Possible types: `ValueArray`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## appsrc-push


``` validate-scenario
appsrc-push,
    file-name=(string),
    target-element-name=(string),
    [caps=(caps)],
    [dts=(GstClockTime)],
    [duration=(GstClockTime)],
    [offset=(uint64)],
    [pts=(GstClockTime)],
    [segment=((GstStructure)segment,[start=(GstClockTime)][stop=(GstClockTime)][base=(GstClockTime)][offset=(GstClockTime)][time=(GstClockTime)][postion=(GstClockTime)][duration=(GstClockTime)])],
    [size=(uint64)],
    [playback-time=(double,string)];
```

Queues a sample in an appsrc. If the pipeline state allows flow of buffers,  the next action is not run until the buffer has been pushed.
 * Implementer namespace: core

### Parameters

* `file-name`:(mandatory): Relative path to a file whose contents will be pushed as a buffer

  Possible types: `string`

* `target-element-name`:(mandatory): The name of the appsrc to push data on

  Possible types: `string`

* `caps`:(optional): Caps for the buffer to be pushed

  Possible types: `caps`

  Default: (null)

* `dts`:(optional): Buffer DTS

  Possible types: `GstClockTime`

  Default: (null)

* `duration`:(optional): Buffer duration

  Possible types: `GstClockTime`

  Default: (null)

* `offset`:(optional): Offset within the file where the buffer will start

  Possible types: `uint64`

  Default: (null)

* `pts`:(optional): Buffer PTS

  Possible types: `GstClockTime`

  Default: (null)

* `segment`:(optional): The GstSegment to configure as part of the sample

  Possible types: `(GstStructure)segment,[start=(GstClockTime)][stop=(GstClockTime)][base=(GstClockTime)][offset=(GstClockTime)][time=(GstClockTime)][postion=(GstClockTime)][duration=(GstClockTime)]`

  Default: (null)

* `size`:(optional): Number of bytes from the file that will be pushed as a buffer

  Possible types: `uint64`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## appsrc-eos


``` validate-scenario
appsrc-eos,
    target-element-name=(string),
    [playback-time=(double,string)];
```

Queues a EOS event in an appsrc.
 * Implementer namespace: core

### Parameters

* `target-element-name`:(mandatory): The name of the appsrc to emit EOS on

  Possible types: `string`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## flush


``` validate-scenario
flush,
    target-element-name=(string),
    [reset-time=(boolean)],
    [playback-time=(double,string)];
```

Sends FLUSH_START and FLUSH_STOP events.
 * Implementer namespace: core

### Parameters

* `target-element-name`:(mandatory): The name of the appsrc to flush on

  Possible types: `string`

* `reset-time`:(optional): Whether the flush should reset running time

  Possible types: `boolean`

  Default: TRUE

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## disable-plugin


``` validate-scenario
disable-plugin,
    plugin-name=(string),
    [as-config=(boolean)],
    [playback-time=(double,string)];
```

Disables a GstPlugin
 * Implementer namespace: core

### Parameters

* `plugin-name`:(mandatory): The name of the GstPlugin to disable

  Possible types: `string`

* `as-config`:(optional): Execute action as a config action (meaning when loading the scenario)

  Possible types: `boolean`

  Default: false

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## check-last-sample


``` validate-scenario
check-last-sample,
    [checksum=(string)],
    [sink-factory-name=(string)],
    [sink-name=(string)],
    [sinkpad-caps=(string)],
    [timecode-frame-number=(string)],
    [playback-time=(double,string)];
```

Checks the last-sample checksum or frame number (set on its  GstVideoTimeCodeMeta) on declared Sink element. This allows checking the checksum of a buffer after a 'seek' or after a GESTimeline 'commit' for example
 * Implementer namespace: core

### Parameters

* `checksum`:(optional): The reference checksum of the buffer.

  Possible types: `string`

  Default: (null)

* `sink-factory-name`:(optional): The name of the factory of the sink element to check sample on.

  Possible types: `string`

  Default: (null)

* `sink-name`:(optional): The name of the sink element to check sample on.

  Possible types: `string`

  Default: (null)

* `sinkpad-caps`:(optional): The caps (as string) of the sink to check.

  Possible types: `string`

  Default: (null)

* `timecode-frame-number`:(optional): The frame number of the buffer as specified on its GstVideoTimeCodeMeta

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## crank-clock


``` validate-scenario
crank-clock,
    [expected-elapsed-time=(GstClockTime)],
    [expected-time=(GstClockTime)],
    [playback-time=(double,string)];
```

Crank the clock, possibly checking how much time was supposed to be waited on the clock and/or the clock running time after the crank. Using one `crank-clock` action in a scenario implies that the scenario is driving the  clock and a #GstTestClock will be used. The user will need to crank it the number of  time required (using the `repeat` parameter comes handy here).
 * Implementer namespace: core

### Parameters

* `expected-elapsed-time`:(optional): Check time elapsed during the clock cranking

  Possible types: `GstClockTime`

  Default: (null)

* `expected-time`:(optional): Expected clock time after cranking

  Possible types: `GstClockTime`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## video-request-key-unit


``` validate-scenario
video-request-key-unit,
    direction=(string),
    [all-headers=(boolean)],
    [count=(int)],
    [pad=(string)],
    [running-time=(double or string)],
    [srcpad=(string)],
    [target-element-factory-name=(string)],
    [target-element-klass=(string)],
    [target-element-name=(string)],
    [playback-time=(double,string)];
```

Request a video key unit
 * Implementer namespace: core

### Parameters

* `direction`:(mandatory): The direction for the event to travel, should be in
  * [upstream, downstream]

  Possible types: `string`

* `all-headers`:(optional): TRUE to produce headers when starting a new key unit

  Possible types: `boolean`

  Default: FALSE

* `count`:(optional): integer that can be used to number key units

  Possible types: `int`

  Default: 0

* `pad`:(optional): The name of the GstPad to send a send force-key-unit to

  Possible types: `string`

  Default: sink

* `running-time`:(optional): The running_time can be set to request a new key unit at a specific running_time.
If not set, GST_CLOCK_TIME_NONE will be used so upstream elements will produce a new key unit as soon as possible.

  Possible variables:

  * position: The current position in the stream

  * duration: The duration of the stream

  Possible types: `double or string`

  Default: (null)

* `srcpad`:(optional): The name of the GstPad to send a send force-key-unit to

  Possible types: `string`

  Default: src

* `target-element-factory-name`:(optional): The factory name of the GstElements to send a send force-key-unit to

  Possible types: `string`

  Default: (null)

* `target-element-klass`:(optional): The klass of the GstElements to send a send force-key-unit to

  Possible types: `string`

  Default: Video/Encoder

* `target-element-name`:(optional): The name of the GstElement to send a send force-key-unit to

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## check-position


``` validate-scenario
check-position,
    expected-position=(GstClockTime),
    [playback-time=(double,string)];
```

Check current pipeline position.

 * Implementer namespace: core

### Parameters

* `expected-position`:(mandatory): The expected pipeline position

  Possible types: `GstClockTime`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## check-current-pad-caps


``` validate-scenario
check-current-pad-caps,
    [comparison-type=(string in [intersect, equal])],
    [expected-caps=(caps,structure)],
    [pad=(string)],
    [target-element-factory-name=(string)],
    [target-element-klass=(string)],
    [target-element-name=(string)],
    [playback-time=(double,string)];
```

Check currently set caps on a particular pad.

 * Implementer namespace: core

### Parameters

* `comparison-type`:(optional): __No description__

  Possible types: `string in [intersect, equal]`

  Default: (null)

* `expected-caps`:(optional): The expected caps. If not present, expected no caps to be set

  Possible types: `caps,structure`

  Default: (null)

* `pad`:(optional): The name of the GstPad to get pad from

  Possible types: `string`

  Default: (null)

* `target-element-factory-name`:(optional): The factory name of the GstElements to get pad from

  Possible types: `string`

  Default: (null)

* `target-element-klass`:(optional): The klass of the GstElements to get pad from

  Possible types: `string`

  Default: (null)

* `target-element-name`:(optional): The name of the GstElement to send a send force-key-unit to

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## run-command


``` validate-scenario
run-command,
    argv=((string){array,}),
    [env=(structure)],
    [playback-time=(double,string)];
```

Run an external command.

 * Implementer namespace: core

### Parameters

* `argv`:(mandatory): The subprocess arguments, include the program name itself

  Possible types: `(string){array,}`

* `env`:(optional): Extra environment variables to set

  Possible types: `structure`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)
     optional                   : Don't raise an error if this action hasn't been executed or failed
                                  ### Possible types:
                                    boolean
                                  Default: false

## foreach


``` validate-scenario
foreach,
    actions=({array of [structures]}),
    [playback-time=(double,string)];
```

Run actions defined in the `actions` array the number of times specified
with an iterator parameter passed in. The iterator can be
a range like: `i=[start, end, step]` or array of values
such as: `values=<value1, value2>`.
One and only one iterator field is supported as parameter.
 * Implementer namespace: core

### Parameters

* `actions`:(mandatory): The array of actions to repeat

  Possible types: `{array of [structures]}`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## corrupt-socket-recv


``` validate-scenario
corrupt-socket-recv,
    errno=(string),
    port=(int),
    [times=(int)],
    [playback-time=(double,string)];
```

corrupt the next socket receive
 * Implementer namespace: validatefaultinjection

### Parameters

* `errno`:(mandatory): errno to set when failing

  Possible types: `string`

* `port`:(mandatory): The port the socket to be corrupted listens on

  Possible types: `int`

* `times`:(optional): Number of times to corrupt recv, default is one

  Possible types: `int`

  Default: 1

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## gtk-put-event


``` validate-scenario
gtk-put-event,
    [keys=(string)],
    [string=(string)],
    [type=(string)],
    [widget-name=(string)],
    [playback-time=(double,string)];
```

Put a GdkEvent on the event list using gdk_put_event
 * Implementer namespace: validategtk

### Parameters

* `keys`:(optional): The keyboard keys to be used for the event, parsed with gtk_accelerator_parse_with_keycode, so refer to its documentation for more information

  Possible types: `string`

  Default: (null)

* `string`:(optional): The string to be 'written' by the keyboard sending KEY_PRESS GdkEvents

  Possible types: `string`

  Default: (null)

* `type`:(optional): The event type to get executed. the string should look like the ones in GdkEventType but without the leading 'GDK_'. It is not mandatory as it can be computed from other present fields (e.g, an action with 'keys' will consider the type as 'key_pressed' by default).

  Possible types: `string`

  Default: (null)

* `widget-name`:(optional): The name of the target GdkWidget of the GdkEvent. That widget has to contain a GdkWindow. If not specified, the event will be sent to the first toplevel window

  Possible types: `string`

  Default: (null)

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)

## set-subtitle


``` validate-scenario
set-subtitle,
    subtitle-file=(string (A URI)),
    [playback-time=(double,string)];
```

Action to set a subtitle file to use on a playbin pipeline.
The subtitles file that will be used should be specified
relative to the playbin URI in use thanks to the subtitle-file
action property. You can also specify a folder with subtitle-dir
For example if playbin.uri='file://some/uri.mov'
and action looks like 'set-subtitle, subtitle-file=en.srt'
the subtitle URI will be set to 'file:///some/uri.mov.en.srt'

 * Implementer namespace: validate-launcher

### Parameters

* `subtitle-file`:(mandatory): Sets a subtitles file on a playbin pipeline

  Possible types: `string (A URI)`

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `on-message`:(optional): Specify on what message type the action will be executed.
 If both 'playback-time' and 'on-message' is specified, the action will be executed
 on whatever happens first.

  Possible types: `string`

  Default: (null)
