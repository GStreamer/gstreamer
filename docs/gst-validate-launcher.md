---
short-description: Integration testsuite builder and launcher
...

# gst-validate-launcher

`gst-validate-launcher` is an application to create full testsuites on
top of the GstValidate tools, testing behaviour with dynamic pipelines
and user actions (seeking, changing the pipeline state, etc.) as
described by the [scenario](GstValidateScenario) format.

## Run the GstValidate default testsuite

GstValidate comes with a default testsuite to be executed on a default
set of media samples. Those media samples are stored with `git-annex` so
you will need it to be able to launch the default testsuite.

The first time you launch the testsuite, you will need to make sure that
the media samples are downloaded. To do so and launch the testsuite you
can simply do:

    gst-validate-launcher validate --sync

This will only launch the GstValidate tests and not other applications
that might be supported (currently `ges-launch` is also supported and
has its own default testsuite).

Launching the default testsuite will open/close many windows, you might
want to mute it so you can keep using your computer:

    gst-validate-launcher validate --sync --mute

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
`gst-validate-media-check-GST_API_VERSION`. For an http stream you can
for example do:

    gst-validate-media-check-GST_API_VERSION http://someonlinestream.com/thestream \
                  --output-file /path/to/testsuite_folder/sample_files/thestream.stream_info


The `gst-validate-launcher` will use the generated `.media_info` and
`.stream_info` files to validate the tests as those contain the
necessary information.

Then you will need to write the `testsuite.py` file. You can for example
implement the following testsuite:

``` python
import os

# Make sure gst-validate-launcher uses our media files
options.paths = os.path.dirname(os.path.realpath(__file__))

# Make sure GstValidate is able to use our scenarios
# from the testsuite_folder/scenarios folder
os.environ["GST_VALIDATE_SCENARIOS_PATH"] = \
    os.path.join(os.path.dirname(os.path.realpath(__file__)), "scenarios")

# You can activate the following if you only care about critical issues in
# the report:
# os.environ["GST_VALIDATE"] = "print_criticals"

# Make gst-validate use our scenarios
validate.add_scenarios(["scenario", "scenario1"])


# Now add "Theora and Vorbis in OGG container" as a wanted transcoding format. That means
# that conversion to this format will be tested on all the media files/streams.
validate.add_encoding_formats([MediaFormatCombination("ogg", "vorbis", "theora")])

# Use the GstValidatePlaybinTestsGenerator to generate tests that will use playbin
# and GstValidateTranscodingTestsGenerator to create media transcoding tests that
# will use all the media format added with validate.add_encoding_formats
validate.add_generators([validate.GstValidatePlaybinTestsGenerator(validate),
                         GstValidateTranscodingTestsGenerator(self)])

# Blacklist some tests that are known to fail because a feature is not supported
# or due to any other reason.
# The tuple defining those tests is of the form:
# ("regex defining the test name", "Reason why the test should be disabled")
validate.set_default_blacklist([
        ("validate.*.scenario1.*ogv$"
         "oggdemux does not support some action executed in scenario1")]
        )
```

Once this is done, you've got a testsuite that will:

-   Run playbin pipelines on `file.mp4`, `file1.mkv` and `file2.ogv`&gt;
    executing `scenario` and `scenario1` scenarios

-   Transcode `file.mp4,` `file1.mkv` and `file2.ogv` to Theora and
    Vorbis in a OGG container

The only thing to do to run the testsuite is:


    gst-validate-launcher --config /path/to/testsuite_folder/testsuite.py

# Invocation

You can find detailed information about the launcher by launching it:

    gst-validate-launcher --help
