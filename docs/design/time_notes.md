# Time Notes

Some notes on time coordinates and time effects in GES.

## Time Coordinate Definitions

A timeline will have a single time coordinate, which runs from `0` onwards in `GstClockTime`. Each track in the timeline will share the same time.

For a given track, at any given timeline time `time`, we have a stack of `GESTrackElement`s whose interval `[start, start + duration]` contains `time`. The elements are linked in order or priority. Each element will have four time coordinates per *each* unique stack it is part of:

+ external sink coordinates: the coordinates used at the boundary between the upstream element and itself. This is the external source coordinates of the upstream element minus `(downstream-start - upstream-start)`. If it has no upstream element, these coordinates do not exist.
+ external source coordinates: the coordinates used at the boundary between the downstream element and itself. This is the external sink coordinates of the downstream element minus `(upstream-start - downstream-start)`. If it has no downstream element, these coordinates can be translated to the timeline coordinates by adding the `start` of the element.
+ internal sink coordinates: the coordinates used for the sink of the first internal `GstElement`. This is the external sink coordinates plus `in-point`.
+ internal source coordinates: the coordinates used at the source of the last internal `GstElement`. This will differ from the internal sink coordinates if one of the `GstElement`s applies a rate-changing effects. This is the external source coordinates plus `in-point`. Note that an element that changes the consumption rate should always have its in-point set to `0`. This is because nleghostpad is not able to 'undo' this shift by `in-point` at the opposite pad.

The following diagram shows where these coordinates are used, and how they are transformed. Below we have a `GESSource`, followed by a `GESOperation` that does not perform any rate-changing effect, followed by a `GESEffect` that does apply a rate-changing effect (and so its `in-point` is `0`).

```
time                                        coordinate             coordinate
coords                  object              transformation         transformation
used                                        upwards                downwards
______________________________________________________________________________________

            |        source (1)         |
int. src    |---------------------------|
            |                           |   + in-point-1           - in-point-1
ex. src     '==========================='
                                            - start-1 + start-2    + start-1 - start-2
ex. sink    .===========================.
            |                           |   - in-point-2           + in-point-2
int. sink   |---------------------------|
            |       operation (2)       |   identity ()            identity ()
int. src    |---------------------------|
            |                           |   + in-point-2           - in-point-2
ex. src     '==========================='
                                            - start-2 + start-3    + start-2 - start-3
ex. sink    .===========================.
            |                           |   - 0                    + 0
int. sink   |---------------------------|
            |     time effect (3)       |   f ()                   f^-1 ()
int. src    |---------------------------|
            |                           |   + 0                    - 0
ex. src     '==========================='
                                            - start-3              + start-3
timeline    +++++++++++++++++++++++++++++
                      timeline
```

The given function `f` summarises how a seek will transform as it goes from from the source to the sink of the internal `GstElement`, and `f^-1` summarises how a segment stream time will transform.

In particular, `f` will be a function

```
f: [0, MAX] -> [0, G_MAXUINT64 - 1]
```

where `MAX` is some `guint64`. For what follows, we will only fully support time effects whose function `f`:

+ is monotonically increasing. This would exclude time effects that play some later content, and then jump back to earlier content.
+ is 'continuously reversible'. We define `T_d` for the time `t` as the set of all the times `t'` such that `|f (t') - t| <= d`. This property requires that, for any `t` between `f`'s minimum and maximum values, we can choose a small `d` such that `T_d` is not empty, is small and has no gaps. The word "small" refers to an unnoticeable difference (the times are in nanoseconds).  This means that `f` can be approximately reversed at all points between its minimum and maximum, which means that `f^-1` can act as a close inverse of `f`. For a monotonically increasing function, this means that `f` is *steadily* increasing.

 For example, if `f` simply doubles the time, then for time `t = 501`, we can choose `d=1`, and `T_d` would be `{250, 251}`.

 This would exclude a time effect which has a large jump, because there would be a time `t` between this jump, whose `T_d` would be empty for all small `d`.

 This would also exclude a time effect that creates a freeze-frame effect by always seeking to the same spot, because at the time `t` of this freeze-frame, `T_d` would be large for all `d`.

+ obeys `f (0) = 0`. This would exclude a time effect that introduces an initial shift in the requested source time.
+ has a `MAX` that is large enough. For example, 24 hours would be fine for a timeline. This would exclude a rate effect with a very large speedup.
+ does not depend on any property outside of the effect element, or on the data it receives. This would exclude a time effect that, say, goes faster if there is more red in the image.

In what follows, a time effect that breaks one of these can still be used, but not all the features will work.

### Translations Handled by `nleobject` Pads

An `nleobject` source pad will translate outgoing times by applying

```
time-out = time-in + start - in-point
```

This will translate from the internal source coordinates to the timeline coordinates *if* it is the most downstream element. Similarly, an `nleobject` sink pad will translate incoming times by applying

```
time-out = time-in - start + in-point
```

If we have two `nle-object`s, `object-up` and `object-down`, that have their pads linked, then a time `time-up` from `object-up`'s internal `GstElement`, would be translated at the link to

```
time-down
 = time-up + start-up - in-point-up - start-down + in-point-down
```

So the pads will overall translate from the internal source coordinates of the upstream element to the internal sink coordinates of the downstream element.

### Undefined Translations

Note that the coordinate transformation from the timeline time to an upstream time may be undefined depending on the configuration of elements in the timeline. For example, consider the earlier example stack, with the operation starting later than the time effect, such that

```
d = (start-2 - start-3) > 0
```

And we choose the `time`

```
time = start-2
     = start-3 + d
```

Then, when `time` is transformed to the external source coordinates of the operation, we have

```
operation-source-time = f (time - start-3) - start-2 + start-3
                      = f (d) - d
```

If the time effect slows down the consumption rate, then `f (d) < d`, which would make the time undefined in the external source coordinates (we can not have a negative `GstClockTime`). Basically, the effect is trying to access content that is before the operation.

We can similarly have an effect that tries to access content that is later than the operation, but this wouldn't lead to an underflow of the time. It can however lead to a request for data that is outside the internal content of the operation.

### Mismatched Coordinates

The coordinates of an element are only defined relative to the stack that they are in. However, if we have no time effects, these coordinates will line up. Consider the following source and operation configuration.

```
            |        source (1)         |
            |---------------------------|
            |                           |
            '==========================='

.===========================.
|                           |
|---------------------------|
|       operation (2)       |
|---------------------------|
|                           |
'==========================='

+++++++++++++++++++++++++++++++++++++++++
              timeline
```

This gives us three stacks

```
                          |      (1)      |            |    (1)    |
                          |---------------|            |-----------|
                          |               |            |           |
                          '===============.            '==========='
                          0           (s1-s2+d1)   (s1-s2+d1)      d2

 0        (s2-s1)      (s2-s1)           d1
 .===========.            .===============.
 |           |            |               |
 |-----------|            |---------------|
 |    (2)    |            |      (2)      |
 |-----------|            |---------------|
 |           |            |               |
 '==========='            '==============='
 0        (s2-s1)      (s2-s1)           d1

 +++++++++++++            +++++++++++++++++            +++++++++++++
s1          s2           s2            (s1+d1)      (s1+d1)     (s2+d2)
```

where we have written in times in the external coordinates of the elements, where `s1` and `d1` are the `start` and `duration` of the source, and similarly for `s2` and `d2` for the operation. We can see that the edge times of all coordinates match up with their neighbours. Therefore, for both elements, there coordinates across each stack can be combined into a single coordinate system.

Consider that instead of the operation we have a time effect, then we would have

```
                          |      (1)      |            |    (1)    |
                          |---------------|            |-----------|
                          |               |            |           |
                          '===============.            '==========='
                      f(s2-s1)          f(d1)      (s1-s2+d1)      d2
                      -(s2-s1)         -(s2-s1)

f(0)     f(s2-s1)     f(s2-s1)          f(d1)
 .===========.            .===============.
 |           |            |               |
 |-----------|            |---------------|
 |    (2)    |            |      (2)      |
 |-----------|            |---------------|
 |           |            |               |
 '==========='            '==============='
 0        (s2-s1)      (s2-s1)           d1

 +++++++++++++            +++++++++++++++++            +++++++++++++
s1          s2           s2            (s1+d1)      (s1+d1)     (s2+d2)
```

We can see that the coordinates of the source now start at `f(s2-s1) - (s2-s1)`, rather than `0`. We can also see that the external source coordinates of the source jump by `(d1 - f(d1))` when the time effect ends. Therefore, most time effects will prevent the coordinates from different stacks from being combined. This can lead to counter-intuitive behaviour.

A further example would be a rate effect with `rate=3` that covers two sources that are side by side. The rate effect will **not** treat this as playing the sources concatenated, at triple speed. Instead, it would play the first source at triple speed, and once it reaches the starting timeline time of the second source, it will start playing the second source instead, but starting from the internal source coordinates

```
3 * (source-start - rate-start) - (source-start - rate-start) + source-in-point
 = 2 * (source-start - rate-start) + source-in-point
```

Note that if this was a slowed down rate, this would have been an undefined (negative) time, as we mentioned earlier.

Therefore, in general, time effects should only be placed at a higher priority than elements that share the same `start` and `duration` as it. Note that it is fine to place an operation with a higher priority on top of a time effect with a different `start` or `duration` because this will not lead to a change in the coordinates.

This is why only a `GESSourceClip` can have time effects added to it.

There is a general exception to this: if a time effect obeys `f (0) = 0`, then it will not introduce mismatched coordinates downstream if it has a later `start` than all the elements it has a higher priority than, **and** its end timeline time matches all of theirs. Note that this is because the effect would only exist in a single stack, and starts by apply no change to the times it receives.

## GESTimelineElement times

The `start` and `duration` of an element use the timeline time coordinates. `in-point` and `max-duration` use the internal source coordinates. These last two should be `0` and `GST_CLOCK_TIME_NONE` respectively for time effects.

## How to Translate Between Time Coordinates of a Clip

Consider a `GESTrackElement` `element` in a `GESClip` `clip` in a timeline. It has `n` `active` elements with higher priority in the same `clip` and track, labelled by `i=1,...,n`, where element 1 has a higher priority than element 2, and so on. Each element has an associated function `f_i` that translates from its external source coordinates to its external sink coordinates. Note that for elements that apply no time effect, this will be an identity, regardless of their `in-point`. We can define the function `F`, such that

```
F(t) = f_n (f_n-1 ( ... f_1 (t)...))
```

Note that if each `f_i` has the desired properties, then so will `F`, with the exception that the maximum value it can translate may have become too small. For example, if several rate effects accumulate into a very large speedup.

Given such an `F`, we can translate from the timeline time `t` to the internal source coordinate time of `element` using

```
F (t - start) + in-point
```

This is what is done in `ges_clip_get_internal_time_from_timeline_time`.

Note that this works because all the elements in `clip` share the same `start`. Note that this would not work if there existed an overlapping higher priority time effect outside of the clip because the highest priority clip element would **not** be receiving a timeline time at its source pads. This is not a problem if there are non-time effects at higher priority because they will pass through a timeline time unchanged.

If `F` has the desired properties, it will have a well defined inverse `F^-1`, based on the inverses of `f_i`, which we can use to reverse this translation:

```
F^-1 (t - in-point) + start
```

This is what is done in `ges_clip_get_timeline_time_from_internal_time`.

## `duration-limit`

The `duration-limit` is meant to be the largest value we can set the clip's `duration` to.

It would be given by the minimum

```
ges_clip_get_timeline_time_from_internal_time (clip, child, child-max-duration) - start
```

we calculate amongst all its children that have a `max-duration`. Note that the implementation of `_calculate_duration_limit` does not use this method directly, but it should give the same result.

Note that this would fail if `max-duration` is not reachable through a seek. E.g. if the corresponding function `F` of the time effects acted like

```
F (t) = t + max-duration + 1
```

then `F^-1 (t)` will be undefined for `t=max-duartion` because its domain will be `[max-duration + 1, inf)`. Note that this function `F` does not obey `F (0)=0`, so is not supported in GES.

Note that `duration-limit` may not be *exactly* the largest end time possible. If the corresponding function `F` is monotonically increasing, then there is no source time below `max-duration` that could give a larger value, but there may be some times beyond `max-time` that would correspond to the *same* source time. However, these extra times will only differ from the `max-time` by a small amount if `F` is 'continuously reversible', and so `max-time` would be close enough. Otherwise, we would not have a simple way to know which is the actual largest `duration`.

## Trimming a clip

Normally, trimming is meant to keep the internal content in the same position relative to the timeline. If we are applying a non-constant rate effect, it may not be possible to keep all the internal content appearing in the timeline at the same time whilst changing the `start` and `duration`. However, we can keep the start or end frames/samples in the same timeline position.

#### Trimming the start of a clip to a later time

When trimming the start edge of a clip from timeline time `old-start` to `new-start`, where `old-start < new-start <= (old-start + duration)`, we set the `in-point` of the clip's children such that the internal content that appeared at `new-start` before the trim, still appears at `new-start` afterwards.

This would require

```
new-in-point = old-in-point + F (new-start - old-start)
```

because this is the internal source time corresponding to `new-start`.

Note that, after we have finished trimming, *assuming* the corresponding `F` has not changed and `F (0) = 0`,

```
ges_clip_get_internal_timeline_from_timeline_time (clip, child, new-start)
 = F (new-start - new-start) + new-in-point
 = new-in-point
```

So after trimming, `new-start` will correspond to the same source position as before. Note that this would not work if the time effects changed depending on the data they receive (such as a "go faster if we have more red" time effect) because the corresponding `F` would have changed after setting the `in-point`. However, we already stated earlier that these are not supported in GES.

#### Trimming the start of a clip to an earlier time

When trimming the start edge of a clip from timeline time `old-start` to `new-start`, where `new-start < old-start`, we set the `in-point` of the clip's children such that the internal content that appeared at `old-start` before the trim, still appears at `old-start` afterwards.

```
new-in-point = old-in-point - F (old-start - new-start)
```

Note that this will fail if the second argument is too big, which indicates that it would be before there is any internal content.

In terms of the function `F` earlier, since this is calculated using the new `start` and old `in-point`, the `source-time` would be

Note, after we have finished trimming, *assuming* the corresponding `F` has not changed,

```
ges_clip_get_internal_time_from_timeline_time (clip, child, old-start)
 = F (old-start - new-start) + new-in-point
 = F (old-start - new-start) + old-in-point - F (old-start - new-start)
 = old-in-point
```

So after trimming, `old-start` will correspond to the same source time as before.

Note that `ges_clip_get_internal_time_from_timeline_time` will perform this same calculation if it receives a timeline time before the `start` of the clip. So timeline-tree is simply able to call `ges_clip_get_internal_time_from_timeline_time (clip, child, new_start, error)` in both cases.

#### Trimming the end of a clip

This is as simple as changing the `duration` of the clip since everything will stay at the same timeline position anyway (assuming `F` does not change, as required by GES). It just cannot go above the clip's `duration-limit`.

## Splitting a clip

The `in-point` of the new clip is chosen to match the new `out-point` of the split clip. This won't work well if different core children of the clip will end up with very different `out-point`s. But if these differences are within half a source frame, GES will not complain. The same can happen when trimming a clip, since all the core children must share the same `in-point`.

## Buffer Timestamps

NOTE: As of 21 May, the recommended changes are not implemented in GES. This delves into why translations will be needed for non-linear time effects.

Currently, the `nleobject` pads will leave the buffer times unchanged. Which means that an `nlesource` will send out buffer timestamped using its *internal* source coordinates.

This is in contrast to a `pitch` within an `nleoperation`, which would translate the buffer times from its internal sink coordinates to its internal source coordinates.

Since the internal source coordinates of the `nlesource` do **not** match the internal sink coordinates of the `nleoperation` (they will differ by `in-point`), this will result in buffer times that are not in **any** coordinates.

This will make it difficult to use control bindings which are to be given in stream time, which is linked to the buffer timestamps.

We can explore this in more detail. [According to the GStreamer design docs](https://gstreamer.freedesktop.org/documentation/additional/design/synchronisation.html#stream-time), the stream time is used for

+ report the POSITION query in the pipeline
+ the position used in seek events/queries
+ the position used to synchronize controller values

Therefore, in our case, we can say that the stream time at a given position in an nle stack should match the corresponding seek time.

If we have no applied rate, which shouldn't be the case for a normal uses of a timeline, the `stream-time` is given by

```
    stream-time = buffer.timestamp - seg.start + seg.time
```

Thus, the stream time is basically the internal source or sink coordinates. In `GESTrackElement` control sources are meant to be given in the internal source coordinates.

We will now look at what these time values **currently** are set to in a stack of an `nlesource` and an `nleoperation` that share the same `start` and `duration`.

Currently, the `nleobject` pads will only change the `seg.time` of the segments it receives by adding or subtracting `(start - in-point)`.

We will assume that the `GstElement` that the `nleoperation` wraps is applying its time effect to `seg.time`, `seg.start` and `buffer.timestamp`, which is given by the function `g`. Note that this is what `pitch` currently does. Its not clear to me what `videorate` does to the `buffer.timestamp`, but it does transform `seg.start` the same way as `seg.time`.

The following is a table of what the `seg.time`, `seg.start` and `buffer.timestamp` values are when *leaving* a pad. The "internal src pad" refers to the source pad of the internal `GstElement`. `s` is the `start` of the objects, and `i` is the `in-point` of the `nlesource`. Following these is what the corresponding stream time would be using these values. The final row is what the corresponding seek position would be coming *into* the pad, if were seeking to the same media time `T`.

```
           nlesrc       nlesrc       nleop        nleop        nleop
           internal     external     external     internal     external
           src pad      src pad      sink pad     src pad      src pad

seg.time   i            s            0            g (0)        g (0) + s

seg.start  i            i            i            g (i)        g (i)

buffer.    T            T            T            g (T)        g (T)
timestamp
------------------------------------------------------------------------
stream     T            T            T            g (T)        g (T)
time                    - i          - i          - g (i)      - g (i)
                        + s                       + g (0)      + g (0)
                                                               + s
------------------------------------------------------------------------
seek       T            T            T            g (T - i)    g (T - i)
time                    - i          - i                       + s
                        + s
```

We can see that after the `nleoperation`, the seek time and stream time will generally be out of sync.

Note that if `g` corresponds to a constant rate effect, then

```
g (t) = r * t
```

for some rate `r`. Then, at the `nleoperation` external source pad.

```
stream-time = r * T - r * i + r * 0 + s
            = g (T - i) + s
            = seek-time
```

so the two will match up under the current behaviour for this special case. However, if the rate varies, this will break down.

If, instead, we translate `seg.start` and `buffer.timestamp` in the same way as `seg.time` on the `nleobject` pads, by adding or subtracting `(start - in-point)`, then we will always have

```
seg.start = seg.time
```

which means that we would also have

```
seek-time = stream-time = buffer.timestamp
```

Finally, it would be a good if the convention for a time effect was to use the *output* stream time in `gst_object_sync_values`, rather than the *input* stream time. This would make them compatible with GES's rule that control sources are given in the internal source coordinates. Luckily, it seems that `pitch` already uses the output stream time. `videorate` doesn't currently use `gst_object_sync_values`.
