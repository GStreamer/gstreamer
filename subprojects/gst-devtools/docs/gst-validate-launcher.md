---
short-description: Integration testsuite builder and launcher
...

# gst-validate-launcher

`gst-validate-launcher` is an application to run unit or integration testsuites
providing a set of options and features to help debugging them.

## Run the GStreamer unit tests

Running GStreamer unit tests is as simple as doing:

```
gst-validate-launcher check.gst*
```

If you only want to run GStreamer core tests:

```
gst-validate-launcher check.gstreamer*
```

Or to run unit tests from gst-plugins-base

```
gst-validate-launcher check.gst-plugins-base
```

You can also run them inside valgrind with the `-vg` option or inside gdb with
`--gdb` for example.

## Run the GstValidate default testsuite

GstValidate comes with a default testsuite to be executed on a default
set of media samples. Those media samples are stored with `git-lfs` so
you will need it to be able to launch the default testsuite.

Then you can run:

```
gst-validate-launcher validate
```

This will only launch the GstValidate tests and not other applications
that might be supported (currently `ges-launch` is also supported and
has its own default testsuite).

## Example of a testsuite implementation

To implement a testsuite, you will have to write some simple python code
that defines the tests to be launched by `gst-validate-launcher`.

In this example, we will assume that you want to write a whole new
testsuite based on your own media samples and [scenarios](GstValidateScenario). The
set of media files and the testsuite implementation file will be
structured as follow:

    testsuite_folder/
      |-> testsuite.py
      |-> sample_files/
          |-> file.mp4
          |-> file1.mkv
          |-> file2.ogv
      |-> scenarios
          |-> scenario.scenario
          |-> scenario1.scenario

You should generate the `.media_info` files. To generate them for local
files, you can use:

    gst-validate-launcher --medias-paths /path/to/sample_files/ --generate-media-info

For remote streams, you should use
`gst-validate-media-check-1.0`. For an http stream you can
for example do:

    gst-validate-media-check-GST_API_VERSION http://someonlinestream.com/thestream \
                  --output-file /path/to/testsuite_folder/sample_files/thestream.stream_info


The `gst-validate-launcher` will use the generated `.media_info` and
`.stream_info` files to validate the tests as those contain the
necessary information.

Then you will need to write the `testsuite.py` file. You can for example
implement the following testsuite:

``` python
"""
The GstValidate custom testsuite
"""

import os
from launcher.baseclasses import MediaFormatCombination
from launcher.apps.gstvalidate import *
TEST_MANAGER = "validate"

KNOWN_ISSUES = {}

def setup_tests(test_manager, options):
    print("Setting up the custom testsuite")
    assets_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".", "samples_files"))
    options.add_paths(assets_dir)

    # This step will register default data for the test manager:
    # - scenarios such as `play_15s`, `reverse_playback` etc.
    # - encoding formats such as "mp4,h264,mp3" etc.
    # - blacklist such as dash.media_check.*
    # - test generators:
    #   - GstValidatePlaybinTestsGenerator
    #   - GstValidateMediaCheckTestsGenerator
    #   - GstValidateTranscodingTestsGenerator
    # This 'defaults' can be found in 'gst-devtools/validate/launcher/apps/gstvalidate.py#register_defaults'
    # test_manager.register_defaults()

    # Add scenarios
    scenarios = []
    scenarios.append("play_15s")
    scenarios.append("seek_backward")
    test_manager.set_scenarios(scenarios)

    # Add encoding formats used by the transcoding generator
    test_manager.add_encoding_formats([
            MediaFormatCombination("mp4", "mp3", "h264"),])

    # Add generators
    # GstValidatePlaybinTestsGenerator needs at least one media file
    test_manager.add_generators([GstValidateMediaCheckTestsGenerator(test_manager)])
    # GstValidatePlaybinTestsGenerator needs at least one scenario
    test_manager.add_generators([GstValidatePlaybinTestsGenerator(test_manager)])
    # GstValidateTranscodingTestsGenerator needs at least one MediaFormatCombination
    test_manager.add_generators([GstValidateTranscodingTestsGenerator(test_manager)])

    # list of combo to blacklist tests. Here it blacklists all tests with playback.seek_backward
    test_manager.set_default_blacklist([
            ("custom_testsuite.file.playback.seek_backward.*",
             "Not supported by this testsuite."),])

    # you can even pass known issues to bypass an existing error in your custom testsuite
    test_manager.add_expected_issues(KNOWN_ISSUES)
    return True
```

Once this is done, you've got a testsuite that will:

-   Run playbin pipelines on `file.mp4`, `file1.mkv` and `file2.ogv`&gt;
    executing `play_15s` and `seek_backward` scenarios

-   Transcode `file.mp4,` `file1.mkv` and `file2.ogv` to h264 and
    mp3 in a MP4 container

The only thing to do to run the testsuite is:


    gst-validate-launcher --testsuites-dir=/path/to/testsuite_folder/ testsuite


# Invocation

You can find detailed information about the launcher by launching it:

    gst-validate-launcher --help

You can list all the tests with:

    gst-validate-launcher --testsuites-dir=/path/to/testsuite_folder/ testsuite -L
