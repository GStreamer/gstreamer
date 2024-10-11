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

A validate test file requires a [`meta`](gst-validate-action-types.md?#meta) structure  describing
the test and configuring it. The `meta` structure should be either the
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

## Variables

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
