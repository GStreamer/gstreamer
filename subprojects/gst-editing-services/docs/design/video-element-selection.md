# Video Element Selection

How GES picks a compositor and the raw-video primitives it plugs around
every clip (converter, scaler, deinterlacer, videoflip, uploader,
downloader) - no hard-coded element names, one registry-driven lookup per
process.

All logic lives in `ges/ges-video-element-selector.c`; state is a
lazily-resolved process singleton reached via
`ges_video_element_selector()`.

## Compositor

`find_best_compositor_factory()` walks the registry, keeps features whose
klass contains `Compositor`, instantiates each and requires either:

- a `mixer` property returning a `GstAggregator` sub-instance (bin-style
  compositors, e.g. `glvideomixer`), or
- the factory itself being a `GstAggregator` subclass (direct-style
  compositors, e.g. `compositor`, `glvideomixerelement`, `cudacompositor`).

A sink pad template named `sink_%u` with `xpos`/`ypos`/`width`/`height` on
the pad type is also required - that's the interface GES relies on when
setting clip geometry.

Candidates are sorted by rank and the highest wins. Tests / applications
can swap the chosen compositor by setting ranks before the selector first
resolves, via `GST_PLUGIN_FEATURE_RANK` or validatetest
`meta, ges="compositor-factory=<name>"`.

## Memory family

Once the compositor is known, its `sink_%u` pad-template caps are walked
to collect the `memory:*` features it advertises (`memory:GLMemory`,
`memory:CUDAMemory`, `memory:DMABuf`, ...). System memory presence is
tracked separately (`video/x-raw` with no features, `SystemMemory`, or
`ANY`).

Each non-sysmem feature is tried in order by `populate_selector()`. The
first feature that resolves every required role wins. If none resolve,
`populate_selector(NULL)` picks software elements.

## Roles

Each role below is a `FactoryQuery`: klass keywords (required + excluded)
+ pad memory-feature constraints + (optional) interface constraint.
`find_factory()` runs the registry filter, then sorts candidates by rank
with a tiebreaker preferring the compositor's own plugin (so
`glcolorconvert` wins over `cudaconvert` when `glvideomixer` is the
compositor).

| Role            | Required klass                     | Excluded         | Interface            |
|-----------------|------------------------------------|------------------|----------------------|
| `colorconvert`  | `Converter/Video/Colorspace`       | `Scaler`         | -                    |
| `convert_scale` | `Converter/Video/Scaler/Colorspace`| -                | -                    |
| `scale`         | `Video/Scaler`                     | `Colorspace`     | -                    |
| `deinterlace`   | `Video/Deinterlace`                | -                | -                    |
| `videoflip`     | -                                  | `Converter/Scaler` | `GstVideoDirection`|
| `uploader`      | `Uploader/Video`                   | -                | -                    |
| `downloader`    | `Downloader/Video`                 | -                | -                    |

`convert_scale` is optional: if no combined converter+scaler is found the
chain falls back to `colorconvert ! scale ! colorconvert` (see _Wrappers_).

### Pad-template ANY rejection

Sysmem-only elements (`deinterlace`, `videoflip`, ...) often declare a
second `video/x-raw(ANY)` template for passthrough of specific inputs.
That template matches a `memory:X` query even though the element can't
actually process non-sysmem memory. Every processing-role query therefore
sets `reject_any_on_memory_match = TRUE`: only explicit non-sysmem
features count. Uploader and downloader clear the flag - ANY is
legitimate there (memory transit is their entire job).

## Sysmem fallback

If a role has no native factory for the selected memory, the query is
retried with sysmem caps. On success a `GES_SELECTOR_FALLBACK_<ROLE>` bit
is set on `sw_fallback_mask`, and the maker wraps the core with
`downloader ! sw_core ! uploader` so the role runs on sysmem while the
surrounding chain stays native.

This requires both `uploader` and `downloader` for the native memory. If
either is missing but a fallback would be needed, the backend is rejected
entirely (`populate_selector` returns `FALSE`) and the next memory family
is tried, eventually falling through to pure sysmem.

## Wrappers

Every maker (`ges_video_element_selector_make_<role>`) returns one of
three shapes:

1. **Software** (selector has no `uploader`): the bare core element.
2. **Strict compositor** (sink only accepts its native memory, no
   fallbacks needed): the bare core element. The surrounding chain is
   already native; no download needed at the tail.
3. **Non-strict compositor** (sink also accepts sysmem, or any fallback
   is in play): a bin wrapping `uploader ! core ! downloader` (or
   `downloader ! sw_core ! uploader` for a sysmem-fallback role). The
   chain stays on sysmem between roles; only the compositor sees native
   memory.

Strictness is computed as `strict = !has_sysmem && !sw_fallback_mask`.
Any sysmem leakage - either the compositor advertising sysmem on its
sink, or a role needing a sysmem core - forces non-strict.

## Cache invalidation

The selector singleton caches its resolution for the process lifetime.
It also connects `notify::rank` on every `Compositor`-klass feature;
when any rank changes (via `gst_plugin_feature_set_rank()` or
`GST_PLUGIN_FEATURE_RANK`) the cache is dropped and the next
`ges_video_element_selector()` call re-resolves against the new ranks.
The frame-positioner's compositor-operator enum cache is invalidated
in the same callback so the two stay in sync.

## Worked examples

Default software compositor:

```
compositor + sysmem:
  cc=videoconvert cs=videoconvertscale sc=videoscale
  flip=videoflip di=avdeinterlace up=(none) down=(none)
  strict=FALSE
Wrappers: bare core elements.
```

glvideomixer (bin, accepts sysmem + `memory:GLMemory`):

```
cc=glcolorconvert cs=(none) sc=glcolorscale
flip=glvideoflip di=gldeinterlace up=glupload down=gldownload
strict=FALSE, sw_fallback=0
Wrappers: glupload ! gl-core ! gldownload.
convert_scale: glcolorconvert ! glcolorscale ! glcolorconvert sandwich
since cs is (none).
```

glvideomixerelement (only `memory:GLMemory`):

```
Same roles as above.
strict=TRUE, sw_fallback=0
Wrappers: bare core elements; producers must call
ges_video_element_selector_make_uploader() before feeding the mixer.
```

cudacompositor with partial fallback:

```
cc=cudaconvert cs=cudaconvertscale sc=cudascale
flip=videoflip (sysmem fallback)
di=avdeinterlace (sysmem fallback)
up=cudaupload down=cudadownload
strict=FALSE, sw_fallback=0xC (VIDEOFLIP | DEINTERLACE)
Wrappers: flip and di run as cudadownload ! sw_core ! cudaupload; the
other roles run as cudaupload ! cuda-core ! cudadownload.
```
