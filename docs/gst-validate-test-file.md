---
title: Test file
short-description: GstValidate test file
...

# GstValidate Test file

A `.validatetest` file describes a fully contained validate test case. It
includes the arguments of the tool supposed to be used to run the test as well
as possibly a [configuration](gst-validate-config.md) and a set of action to
describe the validate [scenario](gst-validate-scenarios.md).

# The file format

A validate test file requires a `meta` structure which contains the same
information as the [scenario](gst-validate-scenarios.md) `meta` with some
additional fields described below. The `meta` structure should be either the
first or the one following the `set-globals` structure. The `set-globals`
structures allows you to set global variables for the rest of the
`.validatetest` file and is a free form variables setter. For example you can
do:

``` yaml
set-globals, media_dir=$(test_dir)/../../media
```

## Tool arguments

In the case of [`gst-validate`](gst-validate.md) it **has to** contain an
`args` field with `gst-validate` argv arguments like:

``` yaml
# This is the default tool so it is not mandatory for the `gst-validate` tool
tool = "gst-validate-$(gst_api_version)",
args = {
    # pipeline description
    videotestrc num-buffers=2 ! $(videosink),
    # Random extra argument
    --set-media-info $(test-dir)/some.media_info
}
```

## configs

The `configs` field is an array of structures containing the same content as
usual [configs](gst-validate-config.md) files.

For example:

``` yaml
configs = {
    # Set videotestsrc0 pattern value to `blue`
    "core, action=set-property, target-element-name=videotestsrc0, property-name=pattern, property-value=blue",
    "$(validateflow), pad=sink1:sink, caps-properties={ width, height };",
}
```

Note: Since this is GstStructure synthax, we need to have the structures in the
array as strings/within quotes.

## expected-issues

The `expected-issues` field is an array of `expected-issue` structures containing
information about issues to expect (which can be known bugs or not).

Use `gst-validate-1.0 --print-issue-types` to print information about all issue types.

For example:

``` yaml
expected-issues = {
    "expected-issue, issue-id=scenario::not-ended",
}
```

Note: Since this is GstStructure synthax, we need to have the structures in the
array as strings/within quotes.

### Fields:

* `issue-id`: (string): Issue ID - Mandatory if `summary` is not provided.
* `summary`: (string): Summary - Mandatory if `issue-id` is not provided.
* `details`: Regex string to match the issue details `detected-on`: (string):
             The name of the element the issue happened on `level`: (string):
             Issue level
* `sometimes`: (boolean): Default: `false` -  Wheteher the issue happens only
               sometimes if `false` and the issue doesn't happen, an error will
               be issued.
* `issue-url`: (string): The url of the issue in the bug tracker if the issue is
               a bug.

### Variables

The same way

Validate testfile will define some variables to make those files relocable:

* `$(test_dir)`: The directory where the `.validatetest` file is in.

* `$(test_name)`: The name of the test file (without extension).

* `$(test_name_dir)`: The name of the test directory (test_name with folder
                      separator instead of `.`).

* `$(validateflow)`: The validateflow structure name with the default/right
                     values for the `expectations-dir` and `actual-results-dir`
                     fields. See [validateflow](gst-validate-flow.md) for more
                     information.

* `$(videosink)`: The GStreamer videosink to use if the test can work with
                  different sinks for the video. It allows the tool to use
                  fakesinks when the user doesn't want to have visual feedback
                  for example.

* `$(audiosink)`: The GStreamer audiosink to use if the test can work with
                  different sinks for the audio. It allows the tool to use
                  fakesinks when the user doesn't want to have audio feedback
                  for example.
