---
title: Creating special element types
...

# Creating special element types

By now, we have looked at pretty much any feature that can be embedded
into a GStreamer element. Most of this has been fairly low-level and
given deep insights in how GStreamer works internally. Fortunately,
GStreamer contains some easier-to-use interfaces to create such
elements. In order to do that, we will look closer at the element types
for which GStreamer provides base classes (sources, sinks and
transformation elements). We will also look closer at some types of
elements that require no specific coding such as scheduling-interaction
or data passing, but rather require specific pipeline control (e.g.
N-to-1 elements and managers).
