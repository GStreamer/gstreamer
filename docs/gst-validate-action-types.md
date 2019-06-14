# GstValidate action types

## description


``` validate-scenario
description,
    [summary=(string)],
    [is-config=(boolean)],
    [handles-states=(boolean)],
    [seek=(boolean)],
    [reverse-playback=(boolean)],
    [need-clock-sync=(boolean)],
    [min-media-duration=(double)],
    [min-audio-track=(int)],
    [min-video-track=(int)],
    [duration=(double, int)],
    [pipeline-name=(string)];
```

Allows to describe the scenario in various ways
 * Implementer namespace: core
 * Is config action (meaning it will be executing right at the beginning of the execution of the pipeline)

### Parameters

* `summary`:(optional): Whether the scenario is a config only scenario (ie. explain what it does)

  Possible types: `string`

  Default: 'Nothing'

* `is-config`:(optional): Whether the scenario is a config only scenario

  Possible types: `boolean`

  Default: false

* `handles-states`:(optional): Whether the scenario handles pipeline state changes from the beginning
in that case the application should not set the state of the pipeline to anything
and the scenario action will be executed from the beginning

  Possible types: `boolean`

  Default: false

* `seek`:(optional): Whether the scenario executes seek actions or not

  Possible types: `boolean`

  Default: false

* `reverse-playback`:(optional): Whether the scenario plays the stream backward

  Possible types: `boolean`

  Default: false

* `need-clock-sync`:(optional): Whether the scenario needs the execution to be synchronized with the pipeline's
clock. Letting the user know if it can be used with a 'fakesink sync=false' sink

  Possible types: `boolean`

  Default: false

* `min-media-duration`:(optional): Lets the user know the minimum duration of the stream for the scenario
to be usable

  Possible types: `double`

  Default: 0.0

* `min-audio-track`:(optional): Lets the user know the minimum number of audio tracks the stream needs to contain
for the scenario to be usable

  Possible types: `int`

  Default: 0

* `min-video-track`:(optional): Lets the user know the minimum number of video tracks the stream needs to contain
for the scenario to be usable

  Possible types: `int`

  Default: 0

* `duration`:(optional): Lets the user know the time the scenario needs to be fully executed

  Possible types: `double, int`

  Default: infinite (GST_CLOCK_TIME_NONE)

* `pipeline-name`:(optional): The name of the GstPipeline on which the scenario should be executed.
It has the same effect as setting the pipeline using pipeline_name->scenario_name.

  Possible types: `string`

  Default: NULL

## seek


``` validate-scenario
seek,
    [playback-time=(double,string)],
    start=(double or string),
    flags=(string describing the GstSeekFlags to set),
    [rate=(double)],
    [start_type=(string)],
    [stop_type=(string)],
    [stop=(double or string)];
```

Seeks into the stream. This is an example of a seek happening when the stream reaches 5 seconds
or 1 eighth of its duration and seeks to 10s or 2 eighths of its duration:
  seek, playback-time="min(5.0, (duration/8))", start="min(10, 2*(duration/8))", flags=accurate+flush
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `start`:(mandatory): The starting value of the seek

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double or string`

* `flags`:(mandatory): The GstSeekFlags to use

  Possible types: `string describing the GstSeekFlags to set`

* `rate`:(optional): The rate value of the seek

  Possible types: `double`

  Default: 1.0

* `start_type`:(optional): The GstSeekType to use for the start of the seek, in:
  [none, set, end]

  Possible types: `string`

  Default: set

* `stop_type`:(optional): The GstSeekType to use for the stop of the seek, in:
  [none, set, end]

  Possible types: `string`

  Default: set

* `stop`:(optional): The stop value of the seek

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the streamGST_CLOCK_TIME_NONE

  Possible types: `double or string`

  Default: (null)

## pause


``` validate-scenario
pause,
    [playback-time=(double,string)],
    [duration=(double)];
```

Sets pipeline to PAUSED. You can add a 'duration'
parameter so the pipeline goes back to playing after that duration
(in second)
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `duration`:(optional): The duration during which the stream will be paused

  Possible types: `double`

  Default: 0.0

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

## switch-track


``` validate-scenario
switch-track,
    [playback-time=(double,string)],
    [type=(string)],
    [index=(string: to switch track relatively
int: To use the actual index to use)];
```

The 'switch-track' command can be used to switch tracks.
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `type`:(optional): Selects which track type to change (can be 'audio', 'video', or 'text').

  Possible types: `string`

  Default: audio

* `index`:(optional): Selects which track of this type to use: it can be either a number,
which will be the Nth track of the given type, or a number with a '+' or
'-' prefix, which means a relative change (eg, '+1' means 'next track',
'-1' means 'previous track')

  Possible types: `string: to switch track relatively
int: To use the actual index to use`

  Default: +1

## wait


``` validate-scenario
wait,
    [playback-time=(double,string)],
    [duration],
    [target-element-name=(string)],
    [signal-name=(string)],
    [message-type=(string)];
```

Waits for signal 'signal-name', message 'message-type', or during 'duration' seconds
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `duration`:(optional): the duration while no other action will be executed

  Default: (null)

* `target-element-name`:(optional): The name of the GstElement to wait @signal-name on.

  Possible types: `string`

  Default: (null)

* `signal-name`:(optional): The name of the signal to wait for on @target-element-name

  Possible types: `string`

  Default: (null)

* `message-type`:(optional): The name of the message type to wait for (on @target-element-name if specified)

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
    [playback-time=(double,string)],
    state=(string);
```

Changes the state of the pipeline to any GstState
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `state`:(mandatory): A GstState as a string, should be in:
    * ['null', 'ready', 'paused', 'playing']

  Possible types: `string`

## define-consts


``` validate-scenario
define-consts,
    [playback-time=(double,string)];
```

Define constants to be used in other actions.
For example you can define constants for buffer checksum to be used in the "check-last-sample" action type as follow:

```
 define-consts, frame1=SomeRandomHash1,frame2=Anotherhash...
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

## set-property


``` validate-scenario
set-property,
    [playback-time=(double,string)],
    [target-element-name=(string)],
    [target-element-factory-name=(string)],
    [target-element-klass=(string)],
    property-name=(string),
    property-value=(The same type of @property-name);
```

Sets a property of an element or klass of elements in the pipeline.
Besides property-name and value, either 'target-element-name' or
'target-element-klass' needs to be defined
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `target-element-name`:(optional): The name of the GstElement to set a property on

  Possible types: `string`

  Default: (null)

* `target-element-factory-name`:(optional): The name factory for which to set a property on built elements

  Possible types: `string`

  Default: (null)

* `target-element-klass`:(optional): The klass of the GstElements to set a property on

  Possible types: `string`

  Default: (null)

* `property-name`:(mandatory): The name of the property to set on @target-element-name

  Possible types: `string`

* `property-value`:(mandatory): The value of @property-name to be set on the element

  Possible types: `The same type of @property-name`
     optional                   : Don't raise an error if this action hasn't been executed or failed
                                  ### Possible types:
                                    boolean
                                  Default: false

## set-debug-threshold


``` validate-scenario
set-debug-threshold,
    [playback-time=(double,string)],
    debug-threshold=(string);
```

Sets the debug level to be used, same format as
setting the GST_DEBUG env variable
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `debug-threshold`:(mandatory): String defining debug threshold
See gst_debug_set_threshold_from_string

  Possible types: `string`

## include


``` validate-scenario
include,
    [playback-time=(double,string)],
    location=(string);
```

Include a sub scenario file.
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `location`:(mandatory): The location of the sub scenario to include.

  Possible types: `string`

## emit-signal


``` validate-scenario
emit-signal,
    [playback-time=(double,string)],
    target-element-name=(string),
    signal-name=(string);
```

Emits a signal to an element in the pipeline
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `target-element-name`:(mandatory): The name of the GstElement to emit a signal on

  Possible types: `string`

* `signal-name`:(mandatory): The name of the signal to emit on @target-element-name

  Possible types: `string`

## disable-plugin


``` validate-scenario
disable-plugin,
    [playback-time=(double,string)],
    plugin-name=(string),
    [as-config=(boolean)];
```

Disables a GstPlugin
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `plugin-name`:(mandatory): The name of the GstPlugin to disable

  Possible types: `string`

* `as-config`:(optional): Execute action as a config action (meaning when loading the scenario)

  Possible types: `boolean`

  Default: false

## check-last-sample


``` validate-scenario
check-last-sample,
    [playback-time=(double,string)],
    [sink-name=(string)],
    [sink-factory-name=(string)],
    [sinkpad-caps=(string)],
    checksum=(string);
```

Checks the last-sample checksum on declared Sink element. This allows checking the checksum of a buffer after a 'seek' or after a GESTimeline 'commit' for example
 * Implementer namespace: core

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `sink-name`:(optional): The name of the sink element to check sample on.

  Possible types: `string`

  Default: (null)

* `sink-factory-name`:(optional): The name of the factory of the sink element to check sample on.

  Possible types: `string`

  Default: (null)

* `sinkpad-caps`:(optional): The caps (as string) of the sink to check.

  Possible types: `string`

  Default: (null)

* `checksum`:(mandatory): The reference checksum of the buffer.

  Possible types: `string`

## corrupt-socket-recv


``` validate-scenario
corrupt-socket-recv,
    [playback-time=(double,string)],
    port=(int),
    errno=(string),
    [times=(int)];
```

corrupt the next socket receive
 * Implementer namespace: validatefaultinjection

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `port`:(mandatory): The port the socket to be corrupted listens on

  Possible types: `int`

* `errno`:(mandatory): errno to set when failing

  Possible types: `string`

* `times`:(optional): Number of times to corrupt recv, default is one

  Possible types: `int`

  Default: 1

## gtk-put-event


``` validate-scenario
gtk-put-event,
    [playback-time=(double,string)],
    [keys=(string)],
    [string=(string)],
    [type=(string)],
    [widget-name=(string)];
```

Put a GdkEvent on the event list using gdk_put_event
 * Implementer namespace: validategtk

### Parameters

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

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

## set-subtitle


``` validate-scenario
set-subtitle,
    [playback-time=(double,string)],
    subtitle-file=(string (A URI));
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

* `playback-time`:(optional): The playback time at which the action will be executed

  Possible variables:

  * `position`: The current position in the stream

  * `duration`: The duration of the stream

  Possible types: `double,string`

  Default: 0.0

* `subtitle-file`:(mandatory): Sets a subtitles file on a playbin pipeline

  Possible types: `string (A URI)`
