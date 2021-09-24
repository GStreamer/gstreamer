---
title: Application Development Manual
short-description: Complete walkthrough for building an application using GStreamer
...

# Application Development Manual

## Foreword

GStreamer is an extremely powerful and versatile framework for creating
streaming media applications. Many of the virtues of the GStreamer
framework come from its modularity: GStreamer can seamlessly incorporate
new plugin modules. But because modularity and power often come at a
cost of greater complexity, writing new applications is not always easy.

This guide is intended to help you understand the GStreamer framework
so you can develop applications based on it. The first
chapters will focus on development of a simple audio player, with much
effort going into helping you understand GStreamer concepts. Later
chapters will go into more advanced topics related to media playback and
other forms of media processing (capture, editing, etc.).

## Introduction

### Who should read this manual?

This book is about GStreamer from an application developer's point of
view; it describes how to write a GStreamer application using the
GStreamer libraries and tools. For an explanation about writing plugins,
we suggest the [Plugin Writer's Guide](plugin-development/index.md).

### Preliminary reading

In order to understand this manual, you need to have a basic
understanding of the *C language*.

Since GStreamer adheres to the GObject programming model, this guide
also assumes that you understand the basics of
[GObject](http://library.gnome.org/devel/gobject/stable/) and
[glib](http://library.gnome.org/devel/glib/stable/) programming.
Especially,

  - GObject instantiation

  - GObject properties (set/get)

  - GObject casting

  - GObject referencing/dereferencing

  - glib memory management

  - glib signals and callbacks

  - glib main loop

### Structure of this manual

To help you navigate through this guide, it is divided into several
large parts. Each part addresses a particular broad topic concerning
GStreamer application development. The parts of this guide are laid out
in the following order:

[About GStreamer][about] gives you an overview of GStreamer, its design
principles and foundations.

[Building an Application][app-building] covers the basics of GStreamer
application programming. At the end of this part, you should be able to
build your own audio player using GStreamer

In [Advanced GStreamer concepts][advanced], we will move on to advanced
subjects which make GStreamer stand out from its competitors. We will discuss
application-pipeline interaction using dynamic parameters and interfaces, we
will discuss threading and threaded pipelines, scheduling and clocks (and
synchronization). Most of those topics are not just there to introduce you to
their API, but primarily to give a deeper insight into solving application
programming problems with GStreamer and understanding their concepts.

Next, in [Higher-level interfaces for GStreamer applications][highlevel], we
will go into higher-level programming APIs for GStreamer. You don't
need to know all the details from the previous parts to understand this, but
you will need to understand basic GStreamer concepts nevertheless. We will,
amongst others, discuss playbin and autopluggers.

Finally in [Appendices][appendix], you will find some random
information on integrating with GNOME, KDE, OS X or Windows, some
debugging help and general tips to improve and simplify GStreamer
programming.

[about]: application-development/introduction/index.md
[app-building]: application-development/basics/index.md
[advanced]: application-development/advanced/index.md
[highlevel]: application-development/highlevel/index.md
[appendix]: application-development/appendix/index.md
