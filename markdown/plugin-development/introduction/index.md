---
title: Introduction
...

# Introduction

GStreamer is an extremely powerful and versatile framework for creating
streaming media applications. Many of the virtues of the GStreamer
framework come from its modularity: GStreamer can seamlessly incorporate
new plugin modules. But because modularity and power often come at a
cost of greater complexity (consider, for example,
[CORBA](http://www.omg.org/)), writing new plugins is not always easy.

This guide is intended to help you understand the GStreamer framework
 so you can develop new plugins to extend the existing
functionality. The guide addresses most issues by following the
development of an example plugin - an audio filter plugin - written in
C. However, the later parts of the guide also present some issues
involved in writing other types of plugins, and the end of the guide
describes some of the Python bindings for GStreamer.
