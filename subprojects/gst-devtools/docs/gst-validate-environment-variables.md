---
title: Environment variables
short-description: Environment variables influencing runtime behaviour
...

# GstValidate Environment Variables

The runtime behaviour of GstValidate applications can be influenced by a
number of environment variables.

**GST_VALIDATE.**

This environment variable can be set to a list of debug options, which
cause GstValidate to print out different types of test result
information and consider differently the level of the reported issues.

* `fatal-criticals`: Causes GstValidate to consider only critical issues as import enough
  to consider the test failed (default behaviour)
* `fatal-warnings`: Causes GstValidate to consider warning, and critical issues as
  import enough to consider the test failed
* `fatal-issues`: Causes GstValidate to consider issue, warning, and critical issues
  as import enough to consider the test failed
* `print-issues`: Causes GstValidate to print issue, warning and critical issues in
  the final reports (default behaviour)
* `print-warnings`: Causes GstValidate to only print warning and critical issues in the
  final reports
* `print-criticals`: Causes GstValidate to only print critical issues in the final
  reports

**GST_VALIDATE_FILE.**

Set this variable to a colon-separated list of paths to redirect all
GstValidate messages to this file. If left unset, debug messages are
output to standard error.

You can use the special names `stdout` and `stderr` to use those output.

**GST_VALIDATE_APPS_DIR.**

Set this variable to a colon separated list of paths. The validate test
runner will execute all `.py` scripts found within the directories.
By default GstValidate will look for test applications in the folders:
* subprojects/gst-examples/webrtc/check/validate/apps
* subprojects/gst-editing-services/tests/validate

**GST_VALIDATE_PLUGIN_PATH.**

Set this variable to a colon-separated list of paths. GstValidate will
scan these paths for GstPlugin files and add them to the GstRegistry.
By default GstValidate will look for plugins in the user data directory
specified in the [XDG standard]:
`.local/share/gstreamer-GST_API_VERSION/plugins` and the
system wide user data directory:
`/usr/lib/gstreamer-GST_API_VERSION/validate`

**GST_VALIDATE_SCENARIOS_PATH.**

Set this variable to a colon-separated list of paths. GstValidate will
scan these paths for GstValidate scenario files. By default GstValidate
will look for scenarios in the user data directory as specified in the
[XDG standard]:
`.local/share/gstreamer-GST_API_VERSION/validate/scenarios` and the
system wide user data directory:
`/usr/lib/gstreamer-GST_API_VERSION/validate/scenarios`

**GST_VALIDATE_CONFIG.**

Set this variable to a colon-separated list of paths to GstValidate
config files or directly as a string in the GstCaps serialization
format. The config file has a format similar to the scenario file. The
name of the configuration corresponds to the name of the plugin the
configuration applies to.

The special name "core" is used to configure GstValidate core
functionalities (monitors, scenarios, etc...).

If you want to make sure to set a property on a element of a type (for
example to disable QoS on all sinks) you can do:

```
core, action=set-property, target-element-klass=Sink
```

If you want the GstPipeline to get dumped when an issue of a certain
level (and higher) happens, you can do:

```
core, action=dot-pipeline, report-level=issue
```

Note that you will still need to set GST_DEBUG_DUMP_DOT_DIR.

For more examples you can look at the ssim GstValidate plugin
documentation to see how to configure that plugin.

You can also check that a src pad is pushing buffers at a minimum
frequency. For example to check if v4l2src is producing at least 60 frames
per second you can do:

``` yaml
    core,min-buffer-frequency=60,target-element-factory-name=v4l2src
```

This config accepts the following fields:
-   `min-buffer-frequency`: the expected minimum rate, in buffers per
    second, at which buffers are pushed on the pad

-   `target-element-{factory-name,name,klass}`: the factory-name, object
    name or class of the element to check

-   `name`: (optional) only check the frequency if the src pad has this
    name

-   `buffer-frequency-start`: (optional) if defined, validate will
    ignore the frequency of the pad during the time specified in this
    field, in ns. This can be useful when testing live pipelines where
    configuring and setting up elements can take some time slowing down
    the first buffers until the pipeline reaches its cruising speed.
**GST_VALIDATE_OVERRIDE.**

Set this variable to a colon-separated list of dynamically linkable
files that GstValidate will scan looking for overrides. By default
GstValidate will look for scenarios in the user data directory as
specified in the [XDG standard]:
`.local/share/gstreamer-GST_API_VERSION/validate/scenarios` and the
system wide user data directory:
`/usr/lib/gstreamer-GST_API_VERSION/validate/scenarios`

**GST_VALIDATE_SCENARIO_WAIT_MULITPLIER.**

A decimal number to set as a multiplier for the wait actions. For
example if you set `GST_VALIDATE_SCENARIO_WAIT_MULITPLIER=0.5`, for a
wait action that has a duration of 2.0 the waiting time will only be of
1.0 second. If set to 0, wait action will be ignored.

**GST_VALIDATE_REPORTING_DETAILS.**

The reporting level can be set through the
GST_VALIDATE_REPORTING_DETAILS environment variable, as a
comma-separated list of (optional) object categories / names and levels.
Omit the object category / name to set the global level.

Examples:

```
GST_VALIDATE_REPORTING_DETAILS=synthetic,h264parse:all
GST_VALIDATE_REPORTING_DETAILS=none,h264parse::sink_0:synthetic
```

Levels being:

* `none`: No debugging level specified or desired. Used to deactivate
  debugging output.
* `synthetic`: Summary of the issues found, with no details.
* `subchain`: If set as the default level, similar issues can be reported multiple
  times for different subchains. If set as the level for a particular
  object (`my_object:subchain`), validate will report the issues where
  the object is the first to report an issue for a subchain.
* `monitor`: If set as the default level, all the distinct issues for all the
  monitors will be reported. If set as the level for a particular
  object, all the distinct issues for this object will be reported.
  Note that if the same issue happens twice on the same object, up
  until this level that issue is only reported once.
* `all`: All the issues will be reported, even those that repeat themselves
  inside the same object. This can be **very** verbose if set
  globally.

Setting the reporting level allows to control the way issues are
reported when calling [gst_validate_runner_printf()](gst_validate_runner_printf).

**GST_VALIDATE_LAUNCHER_DEBUG.**

You can activate debug logs setting the environment variable GST_VALIDATE_LAUNCHER_DEBUG.
Examples:
```
$GST_VALIDATE_LAUNCHER_DEBUG=6 gst-validate-launcher
```
It uses the same syntax as PITIVI_DEBUG
(more information at: https://developer.pitivi.org/Bug_reporting.html#debug-logs).

  [XDG standard]: http://www.freedesktop.org/wiki/Software/xdg-user-dirs/
