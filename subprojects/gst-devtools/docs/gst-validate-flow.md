---
title: Validate Flow
short-description: Validate a pad data flow
...

# Validate Flow

Validate Flow — GStreamer validate component to record a log of buffers and
events flowing in a specified pad and compare it with an expectation file.

## Description

This component exists for the purpose of testing non-regular-playback use cases
where the test author specifies the full pipeline, a series of actions and needs
to check whether the generated buffers and events make sense.

The testing procedure goes like this:

1. The test author writes a [.validatetest](gst-validate-test-file.md) test
   where validateflow is used. A pad where monitoring will occur is specified
   and possibly a list of [actions](gst-validate-action-types.md) to run can
   also be specified.

2. The test author runs the test with the desired pipeline, the configuration
   and the actions. Since an expectation file does not exist at
   this point, validateflow will create one. The author should check its
   contents for any missing or unwanted events. No actual checking is done by
   validateflow in this step, since there is nothing to compare to yet.

3. Further executions of the test will also record the produced buffers and
   events, but now they will be compared to the previous log (expectation file).
   Any difference will be reported as a test failure. The original expectation
   file is never modified by validateflow. Any desired changes can be made by
   editing the file manually or deleting it and running the test again.

## Example

### Simplest example

The following is an example of a `fakesrc.simple.validatetest` file using
validateflow.

{{ fakesrc.simple.validatetest.ini }}

Then generate the expectation file with:

``` bash
gst-validate-1.0 --set-test-file /path/to/fakesrc.simple.validatetest
```

This will generate the
`/path/to/fakesrc.simple/flow-expectations/log-sink-sink-expected` file
containing:

{{ plugins/fakesrc.simple/flow-expectations/log-sink-sink-expected.log }}

Note that the test will be marked as "SKIPPED" when we generate expectation
files.

The test can now be run with:

```
gst-validate-1.0 --set-test-file /path/to/fakesrc.simple.validatetest
```

### Example controlling the source

The following is an example of the `qtdemux_change_edit_list.validatetest` file using validateflow.

``` ini
set-globals, media_dir="$(test_dir)/../../../medias/"
meta,
    seek=false,
    handles-states=false,
    args = {
         "appsrc ! qtdemux ! fakesink async=false",
    },
    configs = {
       "$(validateflow), pad=fakesink0:sink, record-buffers=false",
    }

# Scenario action types
appsrc-push, target-element-name=appsrc0, file-name="$(media_dir)/fragments/car-20120827-85.mp4/init.mp4"
appsrc-push, target-element-name=appsrc0, file-name="$(media_dir)/fragments/car-20120827-85.mp4/media1.mp4"
checkpoint, text="A moov with a different edit list is now pushed"
appsrc-push, target-element-name=appsrc0, file-name="$(media_dir)/fragments/car-20120827-86.mp4/init.mp4"
appsrc-push, target-element-name=appsrc0, file-name="$(media_dir)/fragments/car-20120827-86.mp4/media2.mp4"
stop
```

This example shows the elements of a typical validate flow test (a pipeline, a
config and a scenario). Some actions typically used together with validateflow
can also be seen. Notice variable interpolation is used to fill absolute paths
for media files in the scenario (`$(test_dir)`). In the configuration,
`$(validateflow)` is expanded to something like this, containing proper paths
for expectations and actual results (these values are interpolated from the
`.validatetest` file location):

``` ini
validateflow, expectations-dir="/validate/test/file/path/validateqtdemux_change_edit_list/flow-expectations/", actual-results-dir="$(GST_VALIDATE_LOGSDIR)/logs/validate/launch_pipeline/qtdemux_change_edit_list"
```

The resulting log looks like this:

``` ini
event stream-start: GstEventStreamStart, flags=(GstStreamFlags)GST_STREAM_FLAG_NONE, group-id=(uint)1;
event caps: video/x-h264, stream-format=(string)avc, alignment=(string)au, level=(string)2.1, profile=(string)main, codec_data=(buffer)014d4015ffe10016674d4015d901b1fe4e1000003e90000bb800f162e48001000468eb8f20, width=(int)426, height=(int)240, pixel-aspect-ratio=(fraction)1/1;
event segment: format=TIME, start=0:00:00.000000000, offset=0:00:00.000000000, stop=none, time=0:00:00.000000000, base=0:00:00.000000000, position=0:00:00.000000000
event tag: GstTagList-stream, taglist=(taglist)"taglist\,\ video-codec\=\(string\)\"H.264\\\ /\\\ AVC\"\;";
event tag: GstTagList-global, taglist=(taglist)"taglist\,\ datetime\=\(datetime\)2012-08-27T01:00:50Z\,\ container-format\=\(string\)\"ISO\\\ fMP4\"\;";
event tag: GstTagList-stream, taglist=(taglist)"taglist\,\ video-codec\=\(string\)\"H.264\\\ /\\\ AVC\"\;";
event caps: video/x-h264, stream-format=(string)avc, alignment=(string)au, level=(string)2.1, profile=(string)main, codec_data=(buffer)014d4015ffe10016674d4015d901b1fe4e1000003e90000bb800f162e48001000468eb8f20, width=(int)426, height=(int)240, pixel-aspect-ratio=(fraction)1/1, framerate=(fraction)24000/1001;

CHECKPOINT: A moov with a different edit list is now pushed

event caps: video/x-h264, stream-format=(string)avc, alignment=(string)au, level=(string)3, profile=(string)main, codec_data=(buffer)014d401effe10016674d401ee8805017fcb0800001f480005dc0078b168901000468ebaf20, width=(int)640, height=(int)360, pixel-aspect-ratio=(fraction)1/1;
event segment: format=TIME, start=0:00:00.041711111, offset=0:00:00.000000000, stop=none, time=0:00:00.000000000, base=0:00:00.000000000, position=0:00:00.041711111
event tag: GstTagList-stream, taglist=(taglist)"taglist\,\ video-codec\=\(string\)\"H.264\\\ /\\\ AVC\"\;";
event tag: GstTagList-stream, taglist=(taglist)"taglist\,\ video-codec\=\(string\)\"H.264\\\ /\\\ AVC\"\;";
event caps: video/x-h264, stream-format=(string)avc, alignment=(string)au, level=(string)3, profile=(string)main, codec_data=(buffer)014d401effe10016674d401ee8805017fcb0800001f480005dc0078b168901000468ebaf20, width=(int)640, height=(int)360, pixel-aspect-ratio=(fraction)1/1, framerate=(fraction)24000/1001;
```

## Configuration

In order to use the plugin a validate configuration must be provided,
containing a line starting by `validateflow` followed by a number of settings.
Every `validateflow` line creates a `ValidateFlowOverride`, which listens to a
given pad. A test may have several `validateflow` lines, therefore having
several overrides and listening to different pads with different settings.

* `pad`: Required. Name of the pad that will be monitored.
* `record-buffers`: Default: false. Whether buffers will be logged. By default
   only events are logged.
* `buffers-checksum`: Default: 'none'. Define the type of checksums to be used
   valid values are:
  * `none`: No checksum recorded
  * `as-id`: Record checksum as 'ids' where the IDs are incremented on each new
             checksum passed in
  * `md5`: md5 checksum
  * `sha1`: sha1 checksum
  * `sha256`: sha256 checksum
  * `sha512`: sha512 checksum
  * *Note*: for backward compatibility reasons, this can be passed as a
            boolean and it will default to 'sha1' if true, 'none' if false.
* `ignored-fields`: Default: `"stream-start={ stream-id }"` (as they are often
   non reproducible). Key with a serialized GstValueList(str) of fields to not
   record.
* `logged-fields`: Default: `NULL` Key with a serialized GstValueList(str) of
   fields to record, eg. `logged-event-fields="stream-start={flags},
   caps={width, height, framerate}, buffer={pts}"`. Overrides
   `ignored-event-fields` for specified event types.
* `ignored-event-types`: Default: `{ }`. List of event type names to not record
* `logged-event-types`: Default: `NULL`. List of event type names to not record,
   if noone provided, all events are logged, except the ones defined in the
   `ignored-event-types`.
* `expectations-dir`: Path to the directory where the expectations will be
   written if they don't exist, relative to the current working directory. By
   default the current working directory is used, but this setting is usually
   set automatically as part of the `%(validateflow)s` expansion to a correct
   path like `~/gst-validate/gst-integration-testsuites/flow-expectations/<test
   name>`.
* `actual-results-dir`: Path to the directory where the events will be recorded.
   The expectation file will be compared to this. By default the current working
   directory is used, but this setting is usually set automatically as part of
   the `%(validateflow)s` expansion to the test log directory, i.e.
   `~/gst-validate/logs/validate/launch_pipeline/<test name>`.
* `generate-expectations`: Default: unset. When set to `true` the expectation
   file will be written and no testing will be done and if set to `false`, the
   expectation file will be required. If a validateflow config is used without
   specifying any other parametters, the validateflow plugin will consider that
   all validateflow overrides will use that value.


## Scenario actions

Scenarios with validateflow work in the same way as other tests. Often
validatetests will use appsrc in order to control the flow of data precisely,
possibly interleaving events in between. The following is a list of useful
actions.

 * `appsrc-push`: Pushes a buffer from an appsrc element and waits for the chain
   operation to finish. A path to a file is provided, optionally with an offset
   and/or size.
 * `appsrc-eos`: Queues an EOS event from the appsrc. The action finishes
   immediately at this point.
 * `stop`: Tears down the pipeline and stops the test.
 * `checkpoint`: Records a "checkpoint" message in all validateflow overrides,
   with an optional explanation message. This is useful to check certain events
   or buffers are sent at a specific moment in the scenario, and can also help
   to the comprehension of the scenario.

More details on these actions can be queried from the command line, like this:

``` bash
gst-validate-1.0 --inspect-action-type appsrc-push
```
