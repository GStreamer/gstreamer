# Troubleshooting GStreamer

## Some application is telling me that I am missing a plug-in. What do I do ?

Well, start by checking if you really are missing the plug-in.

``` 
gst-inspect-1.0 (plug-in)
  
```

and replace (plug-in) with the plug-in you think is missing. If this
doesn't return any result, then you either don't have it or your
registry cannot find it.

If you're not sure either way, then chances are good that you don't have
it. You should get the plug-in and run gst-register to register it. How
to get the plug-in depends on your distribution.

  - if you run GStreamer using packages for your distribution, you
    should check what packages are available for your distribution and
    see if any of the available packages contains the plug-in.

  - if you run GStreamer from a source install, there's a good chance
    the plug-in didn't get built because you are missing an external
    library. When you ran configure, you should have gotten output of
    what plug-ins are going to be built. You can re-run configure to see
    if it's there. If it isn't, there is a good reason why it is not
    getting built. The most likely is that you're missing the library
    you need for it. Check the README file in gst-plugins to see what
    library you need. Make sure to remember to re-run configure after
    installing the supporting library \!

  - if you run GStreamer from git, the same logic applies as for a
    source install. Go over the reasons why the plug-in didn't get
    configured for build. Check output of config.log for a clue as to
    why it doesn't get built if you're sure you have the library needed
    installed in a sane place.

## I get an error that says something like (process:26626):
GLib-GObject-WARNING \*\*: specified instance size for type
\`DVDReadSrc' is smaller than the parent type's \`GstElement' instance
size What's wrong ?

If you run GStreamer from git uninstalled, it means that
something changed in the core that requires a recompilation in the
plugins. Recompile the plugins by doing "make clean && make".

If you run GStreamer installed, it probably means that you run the
plugins against a different (incompatible) version than they were
compiled against, which ususally means that you run multiple
installations of GStreamer. Remove the old ones and - if needed -
recompile again to ensure that it is using the right version.

Note that we strongly recommend using Debian or RPM packages, since you
will not get such issues if you use provided packages.

## The GStreamer application I used stops with a segmentation fault. What can I do ?

There are two things you can do. If you compiled GStreamer with
specific optimization compilation flags, you should try recompiling
GStreamer, the application and the plug-ins without any optimization
flags. This allows you to verify if the problem is due to optimization
or due to bad code. Second, it will also allow you to provide a
reasonable backtrace in case the segmentation fault still occurs.

The second thing you can do is look at the backtrace to get an idea of
where things are going wrong, or give us an idea of what is going wrong.
To provide a backtrace, you should

1.  run the application in gdb by starting it with
    
    ``` 
        gdb (gst-application)
      
    ```
    
    (If the application is in a source tree instead of installed on the
    system, you might want to put "libtool" before "gdb")

2.  Pass on the command line arguments to the application by typing
    
    ``` 
        set args (the arguments to the application)
      
    ```
    
    at the (gdb) prompt

3.  Type "run" at the (gdb) prompt and wait for the application to
    segfault. The application will run a lot slower, however.

4.  After the segfault, type "bt" to get a backtrace. This is a stack of
    function calls detailing the path from main () to where the code is
    currently at.

5.  If the application you're trying to debug contains threads, it is
    also useful to do
    
    ``` 
        info threads
      
    ```
    
    and get backtraces of all of the threads involved, by switching to a
    different thread using "thread (number)" and then again requesting a
    backtrace using "bt".

6.  If you can't or don't want to work out the problem yourself, a copy
    and paste of all this information should be included in your [bug
    report](#using-bugs-where).

## On my system there is no gst-register command.

Since GStreamer version 0.10 this is not needed anymore. The
registry will be rebuilt automatically. If you suspect the registry is
broken, just delete the `registry.*.xml` files under
`$HOME/.gstreamer-1.X/` and run

``` 
  gst-inspect-1.0
```

to rebuild the registry.
