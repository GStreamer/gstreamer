---
title: Configuration
short-description: GstValidate configuration
...

# GstValidate Configuration

GstValidate comes with some possible configuration files
to setup its plugins (and potentially core behaviour),

You can check the [ssim plugin](plugins/ssim.md)
and the [validate flow plugin](plugins/validateflow.md)
for examples.


## Variables

You can use variables in the configs the same way you can
set them in [gst-validate-scenarios](gst-validate-scenarios.md).

Defaults variables are:

- `$(TMPDIR)`: The default temporary directory as returned by `g_get_tmp_dir`.
- `$(CONFIG_PATH)`: The path of the running scenario.
- `$(CONFIG_DIR)`: The directory the running scenario is in.
- `$(CONFIG_NAME)`: The name of the config file
- `$(LOGSDIR)`: The directory where to place log files. This uses the
   `GST_VALIDATE_LOGSDIR` environment variable if avalaible or `$(TMPDIR)`
   if the variables hasn't been set. (Note that the
   [gst-validate-launcher](gst-validate-launcher.md) set the environment
   variables.

You can also set you own variables by using the `set-vars=true` argument:

``` yaml
core, set-vars=true, log-path=$(CONFIG_DIR/../log)
```

It is also possible to set global variables (also usable from [scenarios](gst-validate-scenarios.md))
with

``` yaml
set-globals, TESTSUITE_ROOT_DIR=$(CONFIG_DIR)
```