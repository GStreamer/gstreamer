---
title: Preface
...

# Preface

## What is GStreamer?

GStreamer is a framework for creating streaming media applications. The
fundamental design comes from the video pipeline at Oregon Graduate
Institute, as well as some ideas from DirectShow.

GStreamer's development framework makes it possible to write any type of
streaming multimedia application. The GStreamer framework is designed to
make it easy to write applications that handle audio or video or both.
It isn't restricted to audio and video, and can process any kind of data
flow. The pipeline design is made to have little overhead above what the
applied filters induce. This makes GStreamer a good framework for
designing even high-end audio applications which put high demands on
latency or performance.

One of the most obvious uses of GStreamer is using it to build a media
player. GStreamer already includes components for building a media
player that can support a very wide variety of formats, including MP3,
Ogg/Vorbis, MPEG-1/2, AVI, Quicktime, mod, and more. GStreamer, however,
is much more than just another media player. Its main advantages are
that the pluggable components can be mixed and matched into arbitrary
pipelines so that it's possible to write a full-fledged video or audio
editing application.

The framework is based on plugins that will provide the various codec
and other functionality. The plugins can be linked and arranged in a
pipeline. This pipeline defines the flow of the data.

The GStreamer core function is to provide a framework for plugins, data
flow, synchronization and media type handling/negotiation. It also
provides an API to write applications using the various plugins.

## Who Should Read This Guide?

This guide explains how to write new modules for GStreamer. The guide is
relevant to several groups of people:

  - Anyone who wants to add support for new ways of processing data in
    GStreamer. For example, a person in this group might want to create
    a new data format converter, a new visualization tool, or a new
    decoder or encoder.

  - Anyone who wants to add support for new input and output devices.
    For example, people in this group might want to add the ability to
    write to a new video output system or read data from a digital
    camera or special microphone.

  - Anyone who wants to extend GStreamer in any way. You need to have an
    understanding of how the plugin system works before you can
    understand the constraints that the plugin system places on the rest
    of the code. Also, you might be surprised after reading this at how
    much can be done with plugins.

This guide is not relevant to you if you only want to use the existing
functionality of GStreamer, or if you just want to use an application
that uses GStreamer. If you are only interested in using existing
plugins to write a new application - and there are quite a lot of
plugins already - you might want to check the *GStreamer Application
Development Manual*. If you are just trying to get help with a GStreamer
application, then you should check with the user manual for that
particular application.

## Preliminary Reading

This guide assumes that you are somewhat familiar with the basic
workings of GStreamer. For a gentle introduction to programming concepts
in GStreamer, you may wish to read the *GStreamer Application
Development Manual* first. Also check out the other documentation
available on the [GStreamer web
site](http://gstreamer.freedesktop.org/documentation/).

In order to understand this manual, you will need to have a basic
understanding of the C language. Since GStreamer adheres to the GObject
programming model, this guide also assumes that you understand the
basics of [GObject](https://docs.gtk.org/gobject/concepts.html)
programming. You may also want to have a look at Eric Harlow's book
*Developing Linux Applications with GTK+ and GDK*.

## Structure of This Guide

To help you navigate through this guide, it is divided into several
large parts. Each part addresses a particular broad topic concerning
GStreamer plugin development. The parts of this guide are laid out in
the following order:

  - [Building a Plugin][building] - Introduction to the
    structure of a plugin, using an example audio filter for
    illustration.

    This part covers all the basic steps you generally need to perform
    to build a plugin, such as registering the element with GStreamer
    and setting up the basics so it can receive data from and send data
    to neighbour elements. The discussion begins by giving examples of
    generating the basic structures and registering an element in
    [Constructing the Boilerplate][boilerplate]. Then,
    you will learn how to write the code to get a basic filter plugin
    working in [Specifying the pads][pads], [The chain function][chainfunc]
    and [What are states?][states].

    After that, we will show some of the GObject concepts on how to make
    an element configurable for applications and how to do
    application-element interaction in [Adding
    Properties][properties] and [Signals][signals]. Next, you will learn to
    build a quick test application to test all that you've just learned
    in [Building a Test Application][testapp]. We
    will just touch upon basics here. For full-blown application
    development, you should look at [the Application Development
    Manual](application-development/index.md).

  - [Advanced Filter Concepts][advanced] - Information on
    advanced features of GStreamer plugin development.

    After learning about the basic steps, you should be able to create a
    functional audio or video filter plugin with some nice features.
    However, GStreamer offers more for plugin writers. This part of the
    guide includes chapters on more advanced topics, such as scheduling,
    media type definitions in GStreamer, clocks, interfaces and tagging.
    Since these features are purpose-specific, you can read them in any
    order, most of them don't require knowledge from other sections.

    The first chapter, named [Different scheduling
    modes][scheduling], will explain some of the basics of
    element scheduling. It is not very in-depth, but is mostly some sort
    of an introduction on why other things work as they do. Read this
    chapter if you're interested in GStreamer internals. Next, we will
    apply this knowledge and discuss another type of data transmission
    than what you learned in [The chain function][chainfunc]: [Different
    scheduling modes][scheduling]. Loop-based elements will give you
    more control over input rate. This is useful when writing, for
    example, muxers or demuxers.

    Next, we will discuss media identification in GStreamer in [Media Types
    and Properties][media-types]. You will learn how to
    define new media types and get to know a list of standard media
    types defined in GStreamer.

    In the next chapter, you will learn the concept of request- and
    sometimes-pads, which are pads that are created dynamically, either
    because the application asked for it (request) or because the media
    stream requires it (sometimes). This will be in [Request and
    Sometimes pads][request-pads].

    The next chapter, [Clocking][clocks], will
    explain the concept of clocks in GStreamer. You need this
    information when you want to know how elements should achieve
    audio/video synchronization.

    The next few chapters will discuss advanced ways of doing
    application-element interaction. Previously, we learned on the
    GObject-ways of doing this in [Adding Properties][properties] and
    [Signals][signals]. We will discuss dynamic
    parameters, which are a way of defining element behaviour over time
    in advance, in [Supporting Dynamic Parameters][dynamic-params].
    Next, you will learn about interfaces in [Interfaces][interfaces].
    Interfaces are very target- specific ways of application-element
    interaction, based on GObject's GInterface. Lastly, you will learn about
    how metadata is handled in GStreamer in [Tagging (Metadata and
    Streaminfo)][tagging].

    The last chapter, [Events: Seeking, Navigation and More][events], will
    discuss the concept of events in GStreamer. Events are another way of
    doing application-element interaction. They take care of seeking, for
    example. They are also yet another way in which elements
    interact with each other, such as letting each other know about
    media stream discontinuities, forwarding tags inside a pipeline and
    so on.

  - [Creating special element types][element-types] - Explanation of
    writing other plugin types.

    Because the first two parts of the guide use an audio filter as an
    example, the concepts introduced apply to filter plugins. But many
    of the concepts apply equally to other plugin types, including
    sources, sinks, and autopluggers. This part of the guide presents
    the issues that arise when working on these more specialized plugin
    types. The chapter starts with a special focus on elements that can
    be written using a base-class ([Pre-made base classes][base-classes]),
    and later also goes into writing special types of elements in [Writing a
    Demuxer or Parser][one-to-n], [Writing a N-to-1 Element or Muxer][n-to-one]
    and [Writing a Manager][manager].

  - [Appendices][appendix] - Further information for plugin developers.

    The appendices contain some information that stubbornly refuses to
    fit cleanly in other sections of the guide. Most of this section is
    not yet finished.

The remainder of this introductory part of the guide presents a short
overview of the basic concepts involved in GStreamer plugin development.
Topics covered include [Elements and Plugins][intro-elements],
[Pads][intro-pads], [GstMiniObject, Buffers and Events][intro-miniobjects]
and [Media types and Properties][intro-mediatypes]. If you are already
familiar with this information, you can use this short overview to
refresh your memory, or you can skip to [Building a Plugin][building].

As you can see, there's a lot to learn, so let's get started\!

  - Creating compound and complex elements by extending from a GstBin.
    This will allow you to create plugins that have other plugins
    embedded in them.

  - Adding new media types to the registry along with typedetect
    functions. This will allow your plugin to operate on a completely
    new media type.

[building]: plugin-development/basics/index.md
[boilerplate]: plugin-development/basics/boiler.md
[pads]: plugin-development/basics/pads.md
[chainfunc]: plugin-development/basics/chainfn.md
[states]: plugin-development/basics/states.md
[properties]: plugin-development/basics/args.md
[signals]: plugin-development/basics/signals.md
[testapp]: plugin-development/basics/testapp.md
[advanced]: plugin-development/advanced/index.md
[scheduling]: plugin-development/advanced/scheduling.md
[media-types]: plugin-development/advanced/media-types.md
[request-pads]: plugin-development/advanced/request.md
[clocks]: plugin-development/advanced/clock.md
[dynamic-params]: plugin-development/advanced/dparams.md
[interfaces]: plugin-development/advanced/interfaces.md
[tagging]: plugin-development/advanced/tagging.md
[events]: plugin-development/advanced/events.md
[element-types]: plugin-development/element-types/index.md
[base-classes]: plugin-development/element-types/base-classes.md
[one-to-n]: plugin-development/element-types/one-to-n.md
[n-to-one]: plugin-development/element-types/n-to-one.md
[manager]: plugin-development/element-types/manager.md
[appendix]: plugin-development/appendix/index.md
[intro-elements]: plugin-development/introduction/basics.md#elements-and-plugins
[intro-pads]: plugin-development/introduction/basics.md#pads
[intro-miniobjects]: plugin-development/introduction/basics.md#gstminiobject-buffers-and-events
[intro-mediatypes]: plugin-development/introduction/basics.md#media-types-and-properties
