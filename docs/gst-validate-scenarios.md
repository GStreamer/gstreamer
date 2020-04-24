---
title: Scenarios
short-description: The GstValidate Scenario format
...

# GstValidate Scenario File Format

To be able to define a list of actions to execute on a [`GstPipeline`],
a dedicated file format is used. The name of the scenario is the name of
the file without its `.scenario` extension. The scenario file format is
based on the [`GstStructure`] serialized format which is a basic, type
aware, key value format. It takes the type of the action in the first
comma separated field, and then some key value pairs in the form
`parameter=value` separated by commas. The values type will be guessed
if not casted as in `parameter=(string)value`. You can force the type
guessing system to actually know what type you want by giving it the
right hints. For example to make sure the value is a double, you should
add a decimal (ie. `1` will be considered as a `int`, but `1.0` will be
considered as a `double` and `"1.0"` will be considered as a `string`).

For example to represent a seek action, you should add the following
line in the `.scenario` file.

    seek, playback-time=10.0, start=0.0, flags=accurate+flush

The files to be used as scenario should have a `.scenario` extension and
should be placed either in
`$USER_DATA_DIR/gstreamer-1.0/validate/scenarios` ,
`$GST_DATADIR/gstreamer-1.0/validate/scenarios` or in a path defined in
the \$GST\_VALIDATE\_SCENARIOS\_PATH environment variable.

Each line in the `.scenario` file represent an action (you can also use
`\ ` at the end of a line write a single action on multiple lines).
Usually you should start you scenario with a `meta` structure
in order for the user to have more information about the
scenario. It can contain a `summary` field which is a string explaining
what the scenario does and then several info fields about the scenario.
You can find more info about it running:

    gst-validate-1.0 --inspect-action-type action_type_name

So a basic scenario file that will seek three times and stop would look
like:

```
meta, summary="Seeks at 1.0 to 2.0 then at \
3.0 to 0.0 and then seeks at \
1.0 to 2.0 for 1.0 second (between 2.0 and 3.0).", \
seek=true, duration=5.0, min-media-duration=4.0
seek, playback-time=1.0, rate=1.0, start=2.0, flags=accurate+flush
seek, playback-time=3.0, rate=1.0, start=0.0, flags=accurate+flush
seek, playback-time=1.0, rate=1.0, start=2.0, stop=3.0, flags=accurate+flush
```

Many action types have been implemented to help users define their own
scenarios. For example there are:

-   `seek`: Seeks into the stream.
-   `play`: Set the pipeline state to `GST_STATE_PLAYING`.
-   `pause`: Set the pipeline state to `GST_STATE_PAUSED`.
-   `stop`: Stop the execution of the pipeline.

>   **NOTE**: This action actually posts a [`GST_MESSAGE_REQUEST_STATE`]
>   message requesting [`GST_STATE_NULL`] on the bus and the application
>   should quit.

To get all the details about the registered action types, you can list
them all with:

```
gst-validate-1.0 --inspect-action-type
```

and to include transcoding specific action types:

```
gst-validate-transcoding-1.0 --inspect-action-type
```

Many scenarios are distributed with `gst-validate`, you can list them
all using:

```
gst-validate-1.0 --list-scenarios
```

You can find more information about the scenario implementation and
action types in the [`GstValidateScenario` section].

  [`GstPipeline`]: GstPipeline
  [`GstStructure`]: GstStructure
  [`GST_MESSAGE_REQUEST_STATE`]: GST_MESSAGE_REQUEST_STATE
  [`GST_STATE_NULL`]: GST_STATE_NULL
  [`GstValidateScenario` section]: GstValidateScenario

## Default variables

Any action can use the default variables:

- `$(position)`: The current position in the pipeline as reported by
  [gst_element_query_position()](gst_element_query_position)
- `$(duration)`: The current duration of the pipeline as reported by
  [gst_element_query_duration()](gst_element_query_duration)
- `$(TMPDIR)`: The default temporary directory as returned by `g_get_tmp_dir`.
- `$(SCENARIO_PATH)`: The path of the running scenario.
- `$(SCENARIO_DIR)`: The directory the running scenario is in.
- `$(SCENARIO_NAME)`: The name the running scenario


It is also possible to set variables in scenario with the `set-vars` action.