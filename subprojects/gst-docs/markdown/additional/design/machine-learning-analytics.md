# Machine Learning Based Analytics

Analytics refer to the process of extracting information from the content of the
media (or medias). The analysis can be spatial only, for example, image analysis, or
temporal only, like sound detection, or even spatio-temporal tracking or action recognition,
multi-modal image+sound to detect a environment or behaviour. There's also
scenarios where the results of the analysis is used as the input, with or without an
additional media. This design aim is to support ML-based analytics and CV
analytics and offer a way to bridge both techniques.

## Vision

With this design we aim at allowing GStreamer application developers to develop
analytics pipeline easily while taking full advantage of the acceleration
available on the platform where they deploy. The effort of moving the analytic
pipeline to a different platform should be minimal.

## Refinement Using Analytics Pipeline

Similarly to content agnostic media processing (ex. Scaling, color-space change,
serialization, ...), this design promote re-usability and simplicity by allowing
the composition of complex analytics pipelines from simple dedicated analytics
elements that complement each other.

### Example
Simple hypothetical example of an analytic pipeline.

```
+---------+    +----------+    +---------------+    +----------------+
| v4l2src |    | video    |    | onnxinference |    | tensor-decoder |
|         |    |  convert |    |               |    |                |
|        src-sink  scale src-sink1           src1-sink              src---
|         |    |(pre-proc)|    | (analysis)    |    | (post-proc)    |   /
+---------+    +----------+    +---------------+    +----------------+  /
                                                                       /
----------------------------------------------------------------------
|  +-------------+    +------+
|  | Analytic-   |    | sink |
|  |  overlay    |    |      |
-sink           src-sink     |
   | (analysis   |    |      |
   |  -results   |    +------+
   |  -consumer) |
   +-------------+

```

## Supporting Neural Network Inference

There are multiple frameworks supporting neural network inference. Those can be
described more generally as computing graphs, as they are generally not limited
to NN inference applications. Existing NN inference or computing graph frameworks,
like ONNX-Runtime, are encapsulated into a GstElement/Filter. The inference element loads
a model, describing the computing graph, specified by a property. The model
expects inputs in a specific format and produce outputs in specific
format. Depending on the model format, input/output formats can be extracted
from the model, like with ONNX, but it is not always the case.

### Inference Element
Inference elements are an encapsulation of an NN Inference framework. Therefore
they are specific to a framework, like ONNX-Runtime or TensorFlow-Lite.
Other inference elements can be added.

### Inference Input(s)
The input format is defined by the model. Using the model input format the
inference element can constrain its sinkpad(s) capabilities. Note, because tensors
are very generic, the term also encapsulates images/frames, and the term input tensor is
also used to describe inference input.

### Inference Output(s)
Output(s) of the inference are tensors and their format are also dictated by the
model. Analysis results are generally encoded in the output tensor in a way that
is specific to the model. Even models that target the same type of analysis
encode results in different ways.

### Models Format Not Describing Inputs/Outputs Tensor Format
With some models, the input/output tensor format are not described. In
this context, it's the responsibility of the analytics pipeline to push input
tensors with the correct format into the inference process. In this context,
the inference element designer is left with two choices: supporting a model manifest
where inputs/outputs are described or leaving the constraining/fixing the
inputs/outputs to analytics pipeline designer who can use caps filters to
constrain inputs/outputs of the model.

### Tensor Decoders
In order to preserve the generality of the inference element, tensor decoding is
omitted from the inference element and left to specialized elements that have a
specific task of decoding tensor from a specific model. Additionally
tensor decoding does not depend on a specific NN framework or inference element,
this allow reusing the tensor decoders with a same model used with a
different inference element. For example, a YOLOv3 tensor decoder can used to
decode tensor from inference using YOLOv3 model with an element encapsulating
ONNX or TFLite. Note that a tensor decoder can handle multiple tensors that have
similar encoding.

### Tensor
N-dimensional vector.

#### Tensor Type Identifier
This is an identifier, string or quark, that uniquely identifies a tensor type. The
tensor type describes the specific format used to encode analysis result in
memory. This identifier is used by tensor-decoders to know if they can handle
the decoding of a tensor. For this reason, from an implementation perspective,
the tensor decoder is the ideal location to store the tensor type identifier as the code
is already model specific. Since the tensor decoder is by design specific to a
model, no generality is lost by storing it the tensor type identifier.

#### Tensor Datatype
This is the primitive type used to store tensor-data. Like `int8`,
`uint8`, `float16`, `float32`, ...

#### Tensor Dimension Cardinality

Number of dimensions in the tensor.

#### Tensor Dimension

Tensor shape.

- [a], 1-dimensional vector
- [a x b], 2-dimensional vector
- [a x b x c], 3-dimensional vector
- [a x b x ... x n], N-dimensional vector

### Tensor Decoders Need to Recognize Tensor(s) They Can Handle

As mention before, tensor decoders need to be able to recognize tensor(s) they can
handle. It's important to keep in mind that multiple tensors can be attached to
a buffer, when tensors are transported as a meta. It could be easy to
believe that tensor's (cardinality + dimension + data type) is sufficient to
recognize a specific tensor format but we need to remember that analysis results
are encoded into the tensor and retrieve analysis results require a decoding
process specific to the model. In other words a tensor A:{cardinality:3,
dimension: 100 x 5, datatype:int8) and a tensor B:{cardinality:3, 100 x 5,
datatype:int8) can have completely different meaning.

A could be: (Object-detection where each candidate is encoded with (top-left)
coordinates, width, height and object location confidence level)

```
0 : [ x1, y1, w, h, location confidence]
1 : [ x1, y1, w, h, location confidence]
...
99: [ x1, y1, w, h, location confidence]
```

B could be: (Object-detection where each candidate is encoded with (top-left)
coordinates, (bottom-right) coordinate and object class confidence level)
```
0 : [ x1, y1, x2, y2, class confidence]
1 : [ x1, y1, x2, y2, class confidence]
...
99: [ x1, y1, x2, y2, class confidence]
```
We can see that even if A and B have same (cardinality, dimension, data type) a
tensor-decoder expecting A and decoding B would wrong.

In general, for high cardinality tensors, the risk of having two tensors with same
(cardinality + dimension + data type) is low, but if we think of low cardinality
tensors typical of classification (1 x C), we can see that the risk is much
higher. For this reason, we believe it's not sufficient for tensor-decoder to
only rely on (cardinality + dimension + data type) to identify tensor it can
handle.

#### A Tensor Decoder's Second Job: Non-Maximum Suppression (NMS)

The main functionality of Tensor-Decoders is to extract analytics-results from tensors,
but in addition to decoding tensors, in general a second phase of post-processing
is handled by tensor-decoder. This post-processing phase is called non-maximum
suppression (NMS). A simplest example of NMS, is with classification. For every
input, the classification model will produce a probability for potential class.
In general, we're mostly interested in the most probable class or few most
probable class, but there's little value in transport all classes
probability. In addition to keeping only most the probable class (or classes), we
often want the probability to be above a certain threshold, otherwise we're
not interested in the result. Because a significant portion of analytics results
from the inference process don't have much value, we want to filter them out
as early as possible. Since analytics results are only available after tensor
decoding, the tensor decoder is tasked with this type filtering (NMS). The same
concept exists for object detection, where NMS generally involves calculating
the intersection-of-union (IoU) in combination with location and class probability.
Because ML-based analytics are probabilistic by nature, they generally need a form of
NMS post-processing.

#### Handling Multiple Tensors Simultaneously In A Tensor Decoder
Sometimes, it is needed or more efficient to have a tensor decoder handle
multiple tensors simultaneously. In some cases, the tensors are complementary and a
tensor decoder needs to have both tensors to decode analytics result. In other
cases, it's just more efficient to do it simultaneously because of the
tensor-decoder's second job doing NMS. Let's consider YOLOv3, where 3 output tensors are
produced for each input. One tensor represents detection of small objects, a second
tensor medium size objects and a third tensor large size objects. In this context,
it's beneficial to have the tensor decoder decode the 3 tensors simultaneously to
perform the NMS on all the results, otherwise analytics results with low value
would remain in the system for longer. This has implications for the negotiation
of tensor decoders, that will be expanded on in  the section dedicated to tensor decoder
negotiation.

### Why Interpreting (decoding) Tensors
As we described above, tensors contain information and are used to store analytics
results. The analytics results are encoded in a model specific way into the
tensor and unless their consumers, processes making use of analytics-results, are
also model specific, they need to be decoded. Deciding if the analytics pipeline
will have elements producing and consuming tensor directly into their encoded
form, or if a tensor-decoding process will done between tensor production and
consumption, is a design decision that involve compromise between re-usability
and performance. As an example, an object detection overlay element would need to
be model specific to directly consume tensor. Therefore, it would need to be
re-written for any object-detection model using a different encoding scheme, but
if the only goal of the analytics pipeline is to do this overlay, it would
probably be the most efficient implementation. Another aspect in favour of
interpreting tensor is that we can have multiple consumers of the analytics
results, and if the tensor decoding is left to the consumers themselves, it implies
decoding the same tensor multiple times. However, we can think of two models
specifically designed to work together where the output of one model becomes the
input of the downstream model. In this context the downstream model is not
re-usable without the upstream model but they bypass the need for
tensor-decoding and are very efficient. Another variation is that multiple
models are merged into one model removing the need the multi-level inference,
but again, this is a design decision involving compromise on re-usability,
performance and effort. We aim to provide support for all these use cases,
and to allow the analytics pipeline designer to make the best design decisions based
on his specific context. It is important to remember that tensor decoders generally
require media details, such as dimensions, to decode a tensor. If the media is
modified between the analysis phase and tensor decoding, the tensor decoder
must be able to retrieve the media details from the time of inference.

#### Analytics Meta
The Analytics Meta (GstAnalyticsRelationMeta) is the foundation of re-usability of
analytics results and its goal is to store analytics results (GstAnalyticsMtd)
in an efficient way, and to allow to define relations between them. GstAnalyticsMtd
is very primitive and is meant to be expanded. GstAnalyticsMtdClassification (storage
for classification result), GstAnalyticsMtdObjectDetection (storage for
object detection result), GstAnalyticsMtdTracking (storage for
object tracking) are specialization and can used as reference to create other
storage, based on GstAnalyticsMtd, for other types of analytics result.

There are two major use case for the ability to define relation between
analytics results. The first one is define a relation between analytics results
that were generated at different stages. A good example of this could be a first
analysis detected cars from an image and a second level analysis where only
section of image presenting a car is pushed to a second analysis to extract
brand/model of the car in a section of the image. This analytics result is then
appended to the original image with a relation defined with the object-detection
result that have localized this car in the image.

The other use case for relations is to create composition by re-using existing
GstAnalyticsMtd specialization. The relation between different analytics result is
completely decoupled from the analytics result themselves.

All relation definitions are stored in
GstAnaltyicsRelationMeta, which is a container of GstAnaltyicsMtd and also contains
an adjacency-matrix storing relations. One of the benefits is the ability of a
consumer of analytics meta to explore the graph and follow relations between
analytics results without having to understand every type of result in the
relation path. Another important aspect is that analytics meta are not
specific to machine learning techniques and can also be used to store analysis
results from computer vision, heuristics or other techniques. It can be used as
a bridge between different techniques.

#### Impact of Tensor Decoder Modifying Media

It is preferable for tensor decoders to limit themselves to producing normalized
analytics results. This approach makes tensor decoders more reusable and the overall
system more flexible. Interpretation and utilization of analytics results should
be delegated to other elements. For example, `ssdtensordecoder` adds only normalized
analytics results to the output without modifying the media itself. This design allows
normalized analytics results to be consumed by both `objectdetectionoverlay` and
`relationmeta2onvifmeta` elements. Even when tensor decoders can generate new media,
it is preferable to delegate media rendering to a dedicated renderer element
if visualization is the objective.

### Tensor Transport Mode
Two transport mode are envisioned as Meta or as Media. Both mode have pros and
cons which justify supporting both mode. Currently tensor are only transported
as meta.

#### Tensor Transport As Meta

In this mode, the tensor is attached to the buffer (the media) on which the analysis was performed. The advantage of this mode is that the original media is kept in a direct association with the analytics results. Further refinement analysis or consumption (like overlay) of the analytics result are easier when the media on which the analysis was performed is available and easily identifiable. Another advantage is the ability to keep a relation description between tensors in a refinement context. On the other hand, this mode of transporting analytics results makes negotiation of tensor-decoder, in particular, difficult.

### Pre/Post Processing Negotiation

Allowing the negotiation of required analysis pre/post-processing and automatically injecting the required elements to enable their processing would be very valuable and minimize the effort of porting an analytics pipeline between different platforms and making use of available acceleration. Tensor-decoders bin, auto-plugging of pre-processing (considering available acceleration), auto-plugging of inference element (optimized for the platform), post-processing, tensor-decoder bin selecting required tensor-decoders potentially from multiple functionally equivalent, but more adapted to the platform, are all aspects to consider when designing negotiation involved in an analytics pipeline.

#### Tensor Decoder Negotiation

Goals:
- Validate compatibility of tensor decoder with tensors produced upstream.
- Ability to create tensordecoderbin

Pipeline examples:

Example 1: simplest inference pipeline
```
... ! inference model-file=ssd ! ssdtensordecoder ! ...
```

Example 2: Cascaded inferences
```
... ! inference model-file=ssd
    ! inference model-file=mobilenet !
    ! ssdtensordecoder ! classifiertensordecoder ! ...
```

Example 3: Tensor decoders auto-plugging
```
... ! inference model-file=ssd
    ! inference model-file=mobilenet ! tensordecoderbin ! ...
```

#### Tensor Capabilities Anatomy
Tensor capabilities are describing the capabilities of elements that produce
or can use a specific tensor.

Example of tensor capability. Capitalized words are placeholders.
```
video/x-raw,
  width=1920,
  height=1080,
  ...
  tensors=(structure)[
    tensorgroups,
      TENSOR_GROUP_ID = (/set) {
        "(GstCaps)["
          "tensor/strided,"
            "tensor-id"=TENSOR_ID,
            "dims=<(int[0,1], (int)[1,max])>,"
            "dims-order"=(string)row-major,
            "type"={float32, uint8};
          "tensor/strided,"
            "tensor-id"=TENSOR_ID,
            "dims=<(int)[1, max]>",
            "dims-order"=(string)row-major,
            "type={float32, uint8}"
      }
  ]
}
```
When an element can produce a tensor, it will add or append to the `tensors` field. This
field describes the tensor(s) that will be attached to the media buffer. Also
note the media can be of any type: `audio/x-raw` `text/x-raw`. It's more common
to have a raw media, but there is no limitation on the media type.

Tensor capability explained line by line.

```
  tensors=(structure)[
```
This is the top-level tensor caps constraint. By having all tensor capabilities
in a single field, negotiation complexity and impact on other elements that are
not handling tensors is limited. Additionally, it allows inference elements to know
which field contains the tensor capability description. Inference
elements are generic and don't know about tensor-group-id or tensor-id other
than the one from the model they are loading. Without this top-level tensor caps constraint,
generic elements would have to inspect all fields and try to identify if a
field contains a tensor capability description. The first inference element in
the pipeline needs to remove the tensor capabilities from the
overall capabilities when transforming srcpad capabilities to sinkpad
capabilities to avoid propagating tensor capabilities upstream from the first
inference element. To accomplish this task, it needs to be able to recognize tensor
capabilities from the overall capabilities.

Capabilities without top-level `tensors` field:
```
video/x-raw,
  ...
  "ssd-mobilenet-v1-variant-1-out" = (/set) {
    "(GstCaps)["
      "tensor/strided,"
        "tensor-id"=TENSOR_ID,
        "dims=<(int[0,1], (int)[1,max])>,"
        "dims-order"=(string)row-major,
        "type"={float32, uint8};
      "tensor/strided,"
        "tensor-id"=TENSOR_ID,
        "dims=<(int)[1, max]>",
        "dims-order"=(string)row-major,
        "type"={float32, uint8}
  },
  "classification-generic-out" = (/set) {
    "(GstCaps)["
      "tensor/strided,"
        "tensor-id"=TENSOR_ID,
        "dims=<(int[0,1], (int)[1,max])>,"
        "dims-order"=(string)row-major,
        "type"={float32, uint8};
      "tensor/strided,"
        "tensor-id"=TENSOR_ID,
        "dims=<(int)[1, max]>",
        "dims-order"=(string)row-major,
        "type"={float32, uint8}
  }
}
```

and the following pipeline:

```
... ! inference name=i1 model-file=./ssd-model.onnx
    ! inference name=i2 model-file=./mobilenet.onnx ! ...
```

The tensor capability `ssd-mobilenet-v1-variant-1-out` is added by `i1`, and
`classification-generic-out` is added by `i2`. Since inference `i1` is the
first inference element in the pipeline, during negotiation, it must
ensure that no tensor capabilities are propagated upstream
from itself. Without the top-level `tensors` field, `i1` would need to know that
`classification-generic-out` is a tensor capability. However, since it doesn't know
about the tensor-group-id `classification-generic-out`, it would have to inspect the
value of this field to determine if it's a tensor capability. Additionally, `i1` needs
to identify that it is the most upstream element in the pipeline. To achieve
this, it must verify if there are tensor capabilities on its sinkpad without knowing
about any specific tensor-group-id or tensor-id. With the assurance that
all tensor capabilities are within the `tensors` field, `i1` and `i2` can simply
verify if `tensors` is present in the upstream capabilities. If `i1` is first, it
removes the `tensors` field before intersecting its srcpad capabilities with its sinkpad
capabilities. In the future, other elements like tensor batch splitting, which
is generic, as they don't need to know about tensor-group-id or tensor-id,
will need to be able to identify tensor capabilities from the overall
capabilities without knowing any specific tensor-group-id or tensor-id.

```
 tensorgroups,
```
Simply the name of the structure that is required. This level contains tensor
groups.

```
 TENSOR_GROUP_ID = (/set) {
```

`TENSOR_GROUP_ID` is a placeholder that would be replaced with a specific tensor
group id from the tensor registry. This level describes tensors that are related.
For example ssd models produce 4 tensors that need to be interpreted together during
tensor decoding. We use a `GstSet` because the order is irrelevant and a fixed value is required.

```
 "(GstCaps)["
```
This level uses `GstCaps` to allow the tensor decoder to describe one or more shapes of tensors that it can accept.

```
 "tensor/strided,"
```
This indicates the tensor is dense. It's mainly present to indicate the tensor is
not sparse. A sparse tensor will require additional handling at the tensor decoder.


```
 "tensor-id"=TENSOR_ID,
```

TENSOR_ID is a placeholder that would have the value of a specific tensor-id
from the tensor-id registry. A tensor-id identify an tensor encoding. Tensor
encoding can present some flexibility that is described by other tensor caps
parameters like dims, datatype and dims-order, but the encoding pattern remains
the same.

```
 "dims=<(int[0,1], (int)[1,max])>,"
```

This field describes the dimensions of the tensors. In many models, the outermost dimension is
where the batch size would be specified if explicitly described. This field can be interpreted in the context of a specific tensor-id.
In this example, the first dimension allow 0 and 1. A value of 1 is an explicit batch size
of 1, which also means no batching is used. Here, the tensor decoder is guaranteed to receive tensors with a batch size of 1.
A value of 0 means the batch size is dynamic and therefore can be different for each
tensor. It's also possible that batching is not used and first dimension isn't a batch size.

```
 "dims-order"=(string)row-major,
```

This field indicates how the tensor is stored in memory. Possible values are
row-major and col-major. Typically, dims-order is an artifact of the training
framework. For example, JAX use col-major to store its data, which result in
tensor memory encoding to be [column major](https://en.wikipedia.org/wiki/Row-_and_column-major_order).

Here's an example:

YOLOv8 mask tensor's logical tensor encoding:

```
[X, Y, W, H, C0, C1, ..., C79]
```
X, Y, W, H : Describe the object location
C0, ..., C79: Confidence level for 80 different class of objects for the object
located at (X, Y, W, H)

**For 3 objects**, we have:

| x-1 | y-1, | w-1 | h-1 | c0-1 | ... | c79-1 |
| x-2 | y-2, | w-2 | h-2 | c0-2 | ... | c79-2 |
| x-3 | y-3, | w-3 | h-3 | c0-3 | ... | c79-3 |

Memory encoding: dims-order=row-major
With this encoding the object properties are contiguous in memory.

| x-1 | y-1 | w-1 | h-1 | c0-1 | ... | c79-1 | x-2 | y-2 | w-2 | h-2 | ... |
c79-3 |

Memory encoding: dims-order=col-major
With this encoding all objects properties of same type are contiguous in memory.

| x-1 | x-2 | x-3 | y-1 | y-2 | y-3 | ... | c79-1 | c79-2 | c79-3 |

If we draw an analogy between tensor and image, row-major is analogous to interleaved color
components of pixels and col-major is planar color components of pixels.

```
 "type"={float32, uint8};
```
This field indicates the datatype of the tensor.

```
 "tensor/strided,"
   "tensor-id"=TENSOR_ID,
   "dims=<(int)[1, max]>",
   "dims-order"=(string)row-major,
   "type={float32, uint8}"
```

This is a alternative description of the tensor where the dims field is different.
In this case a batch of 1 is implicit.

Cascaded inferences:

If the tensor-group-id is different then the second inference would simply add a new
tensor group.

If two inference share the same tensor-group-id, the second inference will have to
modify the tensor caps to describe both tensors. Since tensor-group-id and
tensor-id remain the same, the same tensor decoder can process tensors from both
inferences. For example, if we have two inference elements loading a
classifications models, where the first inference loads a model classifying the
input/image as genuine or generated and second inference loads a model classifying
input/image based on the city seen in the image. Since classification's tensor
output is frequently a vector of confidence level for each classes then encoding
will be the same for both models and therefore tensor-group-id and tensor-id
will also be the same. The first model would have two classes: genuine and
generated, while the second model might have 1000 classes where each one
represents a city. Assuming an implicit batch of 1, first inference tensor
capabilities will be `dims=<2>` while second inference will be `dims=<1000>`.
Second inference element will have to reconcile `dims` field as they share
the same encoding therefore represented by same tensor-id. Second inference
might set the tensor capabilities to `dims=<0>` meaning a vector of dynamic size or
`dims=(/set){2, 1000}` meaning the vector size can be 2 or 1000.

Selecting a tensor decoder:

A subset operation between upstream tensor caps and tensor decoder caps is
performed and if the upstream tensor caps is a subset, then the tensor decoder can be
selected. Inference elements produce fully constrained (fixed) tensor caps.

### Inference Sinkpad(s) Capabilities
Sinkpad capability, before been constrained based on model, can be any
media type.

### Inference Srcpad(s) Capabilities

Srcpads capabilities, will be identical to sinkpads capabilities.

# Reference
- [Onnx-Refactor-MR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/4916)
- [Analytics-Meta MR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/4962)


