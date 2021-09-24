---
short-description: Tool to test GStreamer components
...

# gst-validate

`gst-validate` is the simplest `gst-launch`-like pipeline launcher
running inside GstValidate monitoring infrastructure. Monitors are added
to it to identify issues in the used elements. At the end it will print
a report with some information about all the issues encountered during
its run. To view issues as they are detected, set the environment
variable `GST_DEBUG=validate:2`{.shell} and they will get printed in the
GStreamer debug log. You can basically run any [GstPipeline](GstPipeline) pipeline
using this tool. If you are not familiar with `gst-launch` syntax,
please refer to `gst-launch`'s documentation.

Simple playback pipeline:

    gst-validate-1.0 playbin uri=file:///path/to/some/media/file

Transcoding pipeline:

    gst-validate-1.0 filesrc location=/media/file/location ! qtdemux name=d ! queue \
            ! x264enc ! h264parse ! mpegtsmux name=m ! progressreport \
            ! filesink location=/root/test.ts d. ! queue ! faac ! m.

It will list each issue that has been encountered during the execution
of the specified pipeline in a human readable report like:

    issue : buffer is out of the segment range Detected on theoradec0.srcpad at 0:00:00.096556426

    Details : buffer is out of segment and shouldn't be pushed. Timestamp: 0:00:25.000 - duration: 0:00:00.040 Range: 0:00:00.000 - 0:00:04.520
    Description : buffer being pushed is out of the current segment's start-stop  range. Meaning it is going to be discarded downstream without any use

The return code of the process will be 18 in case a `CRITICAL` issue has
been found.

# Invocation

`gst-validate` takes a mandatory description of the pipeline to launch,
similar to `gst-launch`, and some extra options.

## Options

* `--set-scenario`: Let you set a scenario, it can be a full path to a scenario file or
  the name of the scenario (name of the file without the `.scenario`
  extension).
* `-l`, `--list-scenarios`:   List the avalaible scenarios that can be run.
* `--scenarios-defs-output-file`: The output file to store scenarios details. Implies
  `--list-scenario`.
* `-t`, `--inspect-action-type`: Inspect the avalaible action types with which to write scenarios if
  no parameter passed, it will list all avalaible action types
  otherwize will print the full description of the wanted types.
* `--set-media-info`: Set a media\_info XML file descriptor to share information about the
  media file that will be reproduced.
* `--set-configs`: Let you set a config scenario. The scenario needs to be set as
  `config`. You can specify a list of scenarios separated by "`:`". It
  will override the GST\_VALIDATE\_SCENARIO environment variable.
