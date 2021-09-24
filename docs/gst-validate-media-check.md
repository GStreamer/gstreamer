---
short-description: Tool to test GStreamer media types discovery
...

# gst-validate-media-check

`gst-validate-media-check` is command line tool checking that media
files discovering works properly with `gst-discoverer` over multiple
runs. It needs a reference text file containing valid information about
a media file (which can be generated with the same tool) and then it
will be able to check that the reference matches what will be reported
by `gst-discoverer` in the following runs.

For example, given that we have a valid `reference.media_info` file, we
can run:

    gst-validate-media-check-GST_API_VERSION file:///./file.ogv --expected-results reference.media_info

It will then output any error encountered and return an exit code
different from 0 if any error is found.

# Invocation

`gst-validate-media-check` takes an URI to analyze and some extra
options to control the output.

## Options

* `-o`, `--output-file`: The output file to store the results.
* `-f`, `--full`: Fully analize the file frame by frame.
* `-e`, `--expected-results`: Path to file containing the expected results (or the last results
  found) for comparison with new results.
