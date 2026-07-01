# Source to Track Routing

How a `GESUriClip` chooses which streams of its URI it uses and which track
each is placed in, and how top effects follow the source they apply to. The
routing is a `GESSourceTrackMap` set on the clip; the effect-to-source link
lives on the `GESBaseEffect`.

Without it, a clip uses every stream and the timeline places one core child of
each type per track, picking arbitrarily between same-type streams and copying
the first into every matching track - so a URI with several streams of one type
cannot be routed, and effects, placed by type, land on whichever source shares
their track.

## The map

`GESSourceTrackMap` (`ges-source-track-map.c`) is an immutable, boxed,
atomically-refcounted value that maps each used source - a `GESUriSourceAsset`,
one per stream of the URI - to the track(s) it is placed in. It is built with a
`GESSourceTrackMapBuilder` (`ges_source_track_map_builder_new()`,
`ges_source_track_map_builder_add()`, `ges_source_track_map_builder_build()`,
which consumes the builder) or, in C, all at once with
`ges_source_track_map_new_full()`, and set on a clip through the
`GESUriClip:source-track-map` property
(`ges_uri_clip_set_source_track_map()`).

- Only mapped sources produce a core child.
- A source mapped to one track is placed there; to several, its source is
  copied into each; to `NULL`, it is created but left in no track.

The map is never edited in place; to change the routing, build a new one and
set it again.

## Placement

`ges_uri_clip_create_track_elements()` skips any stream absent from the map, so
only the mapped sources are created.

The track of each element is resolved by `_get_selected_tracks()`
(`ges-timeline.c`), which first calls the `GESClipClass.select_element_tracks`
vmethod and, only when it returns `NULL`, falls back to the
`select-element-track` and `select-tracks-for-object` signals. `GESUriClip`
overrides the vmethod to return the mapped track(s) of the element's source (an
empty array meaning "no track"), so a clip with a map is authoritative over the
signals, while a clip without one returns `NULL` and keeps the default
behaviour.

## Effects

A top effect applies on top of a single core source. `ges_source_add_effect()`
adds the effect - as `ges_clip_add_top_effect()` does - and records the
`GESSource` it sits on in a `GWeakRef` on the effect. The
`select_element_tracks` vmethod then routes the effect to its bound source's
track(s), so it follows that source instead of being placed on every
matching-type track.

`ges_base_effect_get_source()` returns the bound source, or, for effects added
through `ges_clip_add_top_effect()`, the core source of the effect's own track
(the implicit rule that already governs unbound effects). Its `index` is a
position within the source's own effect chain, translated to the clip-global
priority the effect list stores.

An effect added while its source is not yet in a clip is remembered and applied
once the source is parented. `ges_clip_add_top_effect()` and
`ges_source_add_effect()` should not be mixed on one clip: adding an unbound
effect to a clip that already has bound ones warns (when warning-level debug is
enabled). Only `GESBaseEffect` top effects bind to a source; `GESOperation`s
(transitions, mixers) and time effects remain clip- and track-level.

## Serialization

`_save_source()` (`ges-xml-formatter.c`) writes a `stream-number` attribute on
the `<source>` element when the clip has a map, bumping the format to `0.9`. On
load, `ges_base_xml_formatter_add_source()` routes the source of that
stream-number into the recorded track, rebuilds the clip's map, and, at
`</clip>`, prunes the sources the default placement created for unrouted
streams, so the routing round-trips. Streams are keyed by stream-number rather
than stream-id, as stream-ids are not stable across machines.

## Command line

`ges-launch-1.0`'s `+clip` takes `selected-streams=` (short `ss=`): a `+`
separated list of `<stream>[:<track>]`, where `<stream>` is a stream-number (as
reported by `gst-discoverer-1.0`) or a full stream-id, and the optional
`:<track>` is a track index in `+track` declaration order. Without a track, a
stream goes to the matching-type track at its per-type position.

```
ges-launch-1.0 +clip file.mov selected-streams=1+3
ges-launch-1.0 +track audio +track audio +clip file.mov selected-streams=2:0+4:1
```
