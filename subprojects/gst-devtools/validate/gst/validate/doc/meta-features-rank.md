The `features-rank` field is an array of structures that defines how to
override `GstPluginFeature::rank` to ensure some features will be used,
or at contrary won't be used.

For example:

``` yaml
features-rank = {
  [mandatory, glvideomixer=9999],
  [optional, someoptionalfeature=0],
},
```

One could also use the `set-feature-rank` scenario action, but that
happens after GStreamer or other components are initialized which might
be a problem in some cases.
