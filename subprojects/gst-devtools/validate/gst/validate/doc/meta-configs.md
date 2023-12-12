The `configs` field is an array of structures containing the same content as
usual [configs](gst-validate-config.md) files.

For example:

``` yaml
configs = {
    # Set videotestsrc0 pattern value to `blue`
    "core, action=set-property, target-element-name=videotestsrc0, property-name=pattern, property-value=blue",
    "$(validateflow), pad=sink1:sink, caps-properties={ width, height };",
}
```

Note: Since this is GstStructure syntax, we need to have the structures in the
array as strings/within quotes.

**Warning**: This field is validate only for [`.validatetest`](gst-validate-test-file.md) files, and not `.scenario`.
