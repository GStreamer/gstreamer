---
title: Higher-level interfaces for GStreamer applications
...

# Higher-level interfaces for GStreamer applications

In the previous two parts, you have learned many of the internals and
their corresponding low-level interfaces into GStreamer application
programming. Many people will, however, not need so much control (and as
much code), but will prefer to use a standard playback interface that
does most of the difficult internals for them. In this chapter, we will
introduce you into the concept of autopluggers, playback managing
elements and other such things. Those higher-level interfaces are
intended to simplify GStreamer-based application programming. They do,
however, also reduce the flexibility. It is up to the application
developer to choose which interface he will want to use.
