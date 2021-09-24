---
title: Configuration
short-description: GstValidate configuration
...

# GstValidate Configuration

GstValidate comes with some possible configuration files
to setup its plugins and core behaviour. The config format is very similar
to the [scenario](gst-validate-scenarios.md) file format.

You can check the [ssim plugin](plugins/ssim.md)
and the [validate flow plugin](gst-validate-flow.md)
for examples.

## Core settings parameters

Config name should be `core`.

### `verbosity`

Default: `position`

See [GstValidateVerbosityFlags](GstValidateVerbosityFlags) for possible values.

### `action`

The [action type](gst-validate-action-types.md) to execute, the action type
must be a CONFIG action or the action type must have a `as-config` argument. When the `action`
is specified in a parameter, a validate action is executed using the other parameters of the
config as configuration for the validate scenario action.

#### Example:

```
GST_VALIDATE_CONFIG="core, action=set-property, target-element-name="videotestsrc0", property-name=pattern, property-value=blue" gst-validate-1.0 videotestsrc ! autovideosink
```

This will execute the `set-property, target-element-name="videotestsrc0",
property-name=pattern, property-value=blue` validate action directly from the
config file

### `scenario-action-execution-interval`

Default: `0` meaning that action are executed in `idle` callbacks.

Set the interval between [GstValidateScenario](gst-validate-scenarios.md) actions execution.

### `max-latency`

Default: `GST_CLOCK_TIME_NONE` - disabled

Set the maximum latency reported by the pipeline, over that defined latency the scenario will report
an `config::latency-too-high` issue.

### `max-dropped`

Default: `GST_CLOCK_TIME_NONE` - disabled

The maximum number of dropped buffers, a `config::too-many-buffers-dropped` issue will be reported
if that limit is reached.

### `fail-on-missing-plugin`

Default: `false` meaning that tests are marked as skipped when a GStreamer plugin is missing.

## Variables

You can use variables in the configs the same way you can set them in
[gst-validate-scenarios](gst-validate-scenarios.md).

Defaults variables are:

- `$(TMPDIR)`: The default temporary directory as returned by `g_get_tmp_dir`.
- `$(CONFIG_PATH)`: The path of the running scenario.
- `$(CONFIG_DIR)`: The directory the running scenario is in.
- `$(CONFIG_NAME)`: The name of the config file
- `$(LOGSDIR)`: The directory where to place log files. This uses the
   `GST_VALIDATE_LOGSDIR` environment variable if available or `$(TMPDIR)` if
   the variables hasn't been set. (Note that the
   [gst-validate-launcher](gst-validate-launcher.md) set the environment
   variables).

You can also set you own variables by using the `set-vars=true` argument:

``` yaml
core, set-vars=true, log-path=$(CONFIG_DIR/../log)
```

It is also possible to set global variables (also usable from
[scenarios](gst-validate-scenarios.md)) with:

``` yaml
set-globals, TESTSUITE_ROOT_DIR=$(CONFIG_DIR)
```

## `change-issue-severity` settings parameters

You can change issues severity with the `change-issue-severity` configuration
with the following parameters:

* `issue-id`: The GQuark name of the issue, for example: `event::segment-has-wrong-start`,
  You can use `gst-validate-1.0 --print-issue-types` to list all issue types.
* `new-severity`: The new [`severity`](GstValidateReportLevel) of the issue
* `element-name` (*optional*): The name of the element the severity
   change applies to
* `element-factory-name` (*optional*): The element factory name of the elements the
   severity change applies to
* `element-classification` (*optional*): The classification of the elements the
   severity change applies to
