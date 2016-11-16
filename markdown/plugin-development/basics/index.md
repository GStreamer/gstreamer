---
title: The Basics of Writing a Plugin
...

# Writing a Plugin

You are now ready to learn how to build a plugin. In this part of the
guide, you will learn how to apply basic GStreamer programming concepts
to write a simple plugin. The previous parts of the guide have contained
no explicit example code, perhaps making things a bit abstract and
difficult to understand. In contrast, this section will present both
applications and code by following the development of an example audio
filter plugin called “MyFilter”.

The example filter element will begin with a single input pad and a
single output pad. The filter will, at first, simply pass media and
event data from its sink pad to its source pad without modification. But
by the end of this part of the guide, you will learn to add some more
interesting functionality, including properties and signal handlers. And
after reading the next part of the guide, [Advanced Filter Concepts][advanced],
you will be able to add even more functionality to your plugins.

[advanced]: plugin-development/advanced/index.md
