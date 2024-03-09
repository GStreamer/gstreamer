# GStreamer elements for TensorFlow Lite #

Given a TensorFlow Lite model, this element executes the inference to produce and add `GstTensorMeta` metadata to the buffer to be consumed by a tensor decoder

Requires the TensorFlow Lite library. Tested with TensorFlow r2.18

# To build TensorFlow Lite:

See detailed info on: [https://www.tensorflow.org/lite/guide/build_cmake](https://www.tensorflow.org/lite/guide/build_cmake)
