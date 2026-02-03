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

## `tested-elements`

To register action types for specific GStreamer plugins and elements this field
can be used so GstValidate will ensure that the `class_init` function of that
specific element will be called before running the scenario. Such elements
should register action types from there.

# Adding a test to gst-integration-testsuites

The `gst-integration-testsuites` subproject contains the main collection of
validate tests. Tests are placed under
`subprojects/gst-integration-testsuites/testsuites/validate/` and organized by
element or category:

```
testsuites/validate/<category>/<test_name>.validatetest
```

For example:

- `validate/appsrc/single_push.validatetest`
- `validate/mp4/qtdemux_seek_past_end.default.validatetest`
- `validate/h264/parse.trickmode_predicted.seek_trickmode_predicted.validatetest`

## Test discovery

Tests are automatically discovered by
[`GstValidateSimpleTestsGenerator`](gst-validate-launcher.md) in
`testsuites/validate.py`. There is no manual registration step — placing a
`.validatetest` file under `testsuites/validate/` is sufficient.

The test name is derived from the file path relative to `testsuites/validate/`,
with path separators replaced by dots and the `.validatetest` extension
stripped. For example `validate/mp4/qtdemux_seek_past_end.default.validatetest`
becomes `validate.mp4.qtdemux_seek_past_end.default`.

## ValidateFlow expectations

When a test uses `$(validateflow)`, the expected output is stored in a sibling
directory whose name matches the test filename (without the `.validatetest`
extension):

```
validate/<category>/<test_name>/flow-expectations/log-<element>-<pad>-expected
```

For example `validate/appsrc/single_push.validatetest` stores its expectations
in `validate/appsrc/single_push/flow-expectations/log-fakesink0-sink-expected`.

To generate the initial expectation files, run the test once — it will fail
and write the expected files in the right place. Review them before committing.

## Step by step

1. Create your `.validatetest` file under the appropriate category directory in
   `testsuites/validate/<category>/`. Create the category directory if needed.

2. If the test uses `$(validateflow)`, run it once to generate the expected
   output, review it, and place it under the corresponding
   `flow-expectations/` directory.

3. Regenerate the tests list and commit it:

```
gst-validate-launcher -L
git add testsuites/validate.testslist
```

4. Commit the `.validatetest` file, any flow-expectations, and the updated
   testslist together.

## Adding media files

Media files used by tests live in the `media/` directory of
`gst-integration-testsuites`, which is a separate git repository using
[git-lfs](https://git-lfs.com/) for binary file storage. The files are
organized by container format under `media/defaults/` (e.g. `mp4/`,
`matroska/`, `ogg/`, etc.) with additional directories for specialized content
(`fragments/`, `encrypted/`, `adaptivecontent/`, etc.).

Keep media files as small as possible — they are downloaded by every developer
and CI run. A few seconds of content is usually enough to exercise the code
path you need to test.

To reference media files from a `.validatetest` file, use `set-globals` to
define a relative path through `$(test_dir)`:

``` yaml
set-globals, media_dir="$(test_dir)/../../../media/"
meta,
    args = {
        "filesrc location=$(media_dir)/defaults/mp4/raw_h264.0.mp4 ! qtdemux ! fakesink",
    }
```

### Steps to add a new media file

Since the `media/` directory is a separate git repository, the new media file
must be available on `origin` before the `.validatetest` that uses it can run in
CI. This means the media change has to either be merged first or pushed to a
branch on `origin`, and the submodule reference in the main repository updated
to point to that commit.

1. Make sure the media file extension is tracked by git-lfs in the `media/`
   repository (check `.gitattributes`). If not, add it:

   ``` bash
   cd media/
   git lfs track "*.ext"
   ```

2. Place the file under the appropriate subdirectory in `media/defaults/` (or
   `fragments/`, `encrypted/`, etc.).

3. Commit and push the media file (and any updated `.gitattributes`) to `origin`
   in the `media/` repository.

4. Update the submodule reference in the main `gstreamer` repository to point to
   the commit containing the new media file.

> **Note**: You may see `.media_info` files next to some media files. These are
> used by the legacy test generation system that automatically creates test
> combinations by running all existing `.scenario` files against each media
> file. Adding a `.media_info` for a new file is generally not needed — writing
> a `.validatetest` file that references the media directly is the preferred
> approach.
