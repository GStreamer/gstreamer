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
gst-validate-launcher check.gstreamer
```

Or to run unit tests from gst-plugins-base:

```
gst-validate-launcher check.gst-plugins-base
```

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

Run specific integration testsuites:

```
gst-validate-launcher validate.file
gst-validate-launcher validate.dash
```

GStreamer Editing Services has its own dedicated testsuite:

```
gst-validate-launcher ges
```

Python binding tests:

```
gst-validate-launcher check.gst-python
```

## Listing tests

```
gst-validate-launcher validate.dash -L
gst-validate-launcher ges -L
```

## Debugging options

Run with verbose output:

```
gst-validate-launcher -v check.gstreamer
```

Run under gdb:

```
gst-validate-launcher --gdb check.gstreamer
```

Run under valgrind:

```
gst-validate-launcher -vg check.gstreamer
```

## Writing custom testsuites

See [Writing testsuites](gst-validate-launcher-writing-testsuites.md) for
how to create your own testsuite with custom media samples and scenarios.

# Invocation

You can find detailed information about the launcher by launching it:

    gst-validate-launcher --help
