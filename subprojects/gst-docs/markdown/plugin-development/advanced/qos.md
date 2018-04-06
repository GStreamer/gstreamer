---
title: Quality Of Service (QoS)
...

# Quality Of Service (QoS)

Quality of Service in GStreamer is about measuring and adjusting the
real-time performance of a pipeline. The real-time performance is always
measured relative to the pipeline clock and typically happens in the
sinks when they synchronize buffers against the clock.

When buffers arrive late in the sink, i.e. when their running-time is
smaller than that of the clock, we say that the pipeline is having a
quality of service problem. These are a few possible reasons:

  - High CPU load, there is not enough CPU power to handle the stream,
    causing buffers to arrive late in the sink.

  - Network problems

  - Other resource problems such as disk load, memory bottlenecks etc

The measurements result in QOS events that aim to adjust the datarate in
one or more upstream elements. Two types of adjustments can be made:

  - Short time "emergency" corrections based on latest observation in
    the sinks.

    Long term rate corrections based on trends observed in the sinks.

It is also possible for the application to artificially introduce delay
between synchronized buffers, this is called throttling. It can be used
to limit or reduce the framerate, for example.

## Measuring QoS

Elements that synchronize buffers on the pipeline clock will usually
measure the current QoS. They will also need to keep some statistics in
order to generate the QOS event.

For each buffer that arrives in the sink, the element needs to calculate
how late or how early it was. This is called the jitter. Negative jitter
values mean that the buffer was early, positive values mean that the
buffer was late. the jitter value gives an indication of how early/late
a buffer was.

A synchronizing element will also need to calculate how much time
elapsed between receiving two consecutive buffers. We call this the
processing time because that is the amount of time it takes for the
upstream element to produce/process the buffer. We can compare this
processing time to the duration of the buffer to have a measurement of
how fast upstream can produce data, called the proportion. If, for
example, upstream can produce a buffer in 0.5 seconds of 1 second long,
it is operating at twice the required speed. If, on the other hand, it
takes 2 seconds to produce a buffer with 1 seconds worth of data,
upstream is producing buffers too slow and we won't be able to keep
synchronization. Usually, a running average is kept of the proportion.

A synchronizing element also needs to measure its own performance in
order to figure out if the performance problem is upstream of itself.

These measurements are used to construct a QOS event that is sent
upstream. Note that a QoS event is sent for each buffer that arrives in
the sink.

## Handling QoS

An element will have to install an event function on its source pads in
order to receive QOS events. Usually, the element will need to store the
value of the QOS event and use it in the data processing function. The
element will need to use a lock to protect these QoS values as shown in
the example below. Also make sure to pass the QoS event upstream.

``` c

    [...]

    case GST_EVENT_QOS:
    {
      GstQOSType type;
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (decoder);
      priv->qos_proportion = proportion;
      priv->qos_timestamp = timestamp;
      priv->qos_diff = diff;
      GST_OBJECT_UNLOCK (decoder);

      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
    }

    [...]


```

With the QoS values, there are two types of corrections that an element
can do:

### Short term correction

The timestamp and the jitter value in the QOS event can be used to
perform a short term correction. If the jitter is positive, the previous
buffer arrived late and we can be sure that a buffer with a timestamp \<
timestamp + jitter is also going to be late. We can thus drop all
buffers with a timestamp less than timestamp + jitter.

If the buffer duration is known, a better estimation for the next likely
timestamp to arrive in time is: timestamp + 2 \* jitter + duration.

A possible algorithm typically looks like this:

``` c

  [...]

  GST_OBJECT_LOCK (dec);
  qos_proportion = priv->qos_proportion;
  qos_timestamp = priv->qos_timestamp;
  qos_diff = priv->qos_diff;
  GST_OBJECT_UNLOCK (dec);

  /* calculate the earliest valid timestamp */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (qos_timestamp))) {
    if (G_UNLIKELY (qos_diff > 0)) {
      earliest_time = qos_timestamp + 2 * qos_diff + frame_duration;
    } else {
      earliest_time = qos_timestamp + qos_diff;
    }
  } else {
    earliest_time = GST_CLOCK_TIME_NONE;
  }

  /* compare earliest_time to running-time of next buffer */
  if (earliest_time > timestamp)
    goto drop_buffer;

  [...]


```

### Long term correction

Long term corrections are a bit more difficult to perform. They rely on
the value of the proportion in the QOS event. Elements should reduce the
amount of resources they consume by the proportion field in the QoS
message.

Here are some possible strategies to achieve this:

  - Permanently dropping frames or reducing the CPU or bandwidth
    requirements of the element. Some decoders might be able to skip
    decoding of B frames.

  - Switch to lower quality processing or reduce the algorithmic
    complexity. Care should be taken that this doesn't introduce
    disturbing visual or audible glitches.

  - Switch to a lower quality source to reduce network bandwidth.

  - Assign more CPU cycles to critical parts of the pipeline. This
    could, for example, be done by increasing the thread priority.

In all cases, elements should be prepared to go back to their normal
processing rate when the proportion member in the QOS event approaches
the ideal proportion of 1.0 again.

## Throttling

Elements synchronizing to the clock should expose a property to
configure them in throttle mode. In throttle mode, the time distance
between buffers is kept to a configurable throttle interval. This means
that effectively the buffer rate is limited to 1 buffer per throttle
interval. This can be used to limit the framerate, for example.

When an element is configured in throttling mode (this is usually only
implemented on sinks) it should produce QoS events upstream with the
jitter field set to the throttle interval. This should instruct upstream
elements to skip or drop the remaining buffers in the configured
throttle interval.

The proportion field is set to the desired slowdown needed to get the
desired throttle interval. Implementations can use the QoS Throttle
type, the proportion and the jitter member to tune their
implementations.

The default sink base class, has the “throttle-time” property for this
feature. You can test this with: `gst-launch-1.0 videotestsrc !
xvimagesink throttle-time=500000000`

## QoS Messages

In addition to the QOS events that are sent between elements in the
pipeline, there are also QOS messages posted on the pipeline bus to
inform the application of QoS decisions. The QOS message contains the
timestamps of when something was dropped along with the amount of
dropped vs processed items. Elements must post a QOS message under these
conditions:

  - The element dropped a buffer because of QoS reasons.

  - An element changed its processing strategy because of QoS reasons
    (quality). This could include a decoder that decided to drop every B
    frame to increase its processing speed or an effect element
    that switched to a lower quality algorithm.
