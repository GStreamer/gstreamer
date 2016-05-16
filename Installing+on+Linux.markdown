#  GStreamer SDK documentation : Installing on Linux 

This page last changed on Jun 12, 2013 by slomo.

# Prerequisites

To develop applications using the GStreamer SDK on Linux you will need
one of the following supported distributions:

  - Ubuntu Precise Pangolin (12.04)
  - Ubuntu Quantal Quetzal (12.10)
  - Ubuntu Raring Ringtail (13.04)
  - Debian Squeeze (6.0)
  - Debian Wheezy (7.0)
  - Fedora 17
  - Fedora 18

Other distributions or version might work, but they have not been
tested. If you want to try, do it at your own risk\!

All the commands given in this section are intended to be typed in from
a terminal.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>Make sure you have superuser (root) access rights to install the GStreamer SDK.</p></td>
</tr>
</tbody>
</table>

# Download and install the SDK

The GStreamer SDK provides a set of binary packages for supported Linux
distributions. Detailed instructions on how to install the packages for
each supported distribution can be found below. Optionally, you can
download the source code and build the SDK yourself, which should then
work on any platform.

These packages will install the SDK at `/opt/gstreamer-sdk`. If you
build the SDK yourself, you can install it anywhere you want.

![](images/icons/grey_arrow_down.gif)Ubuntu (Click here to expand)

In order to install the SDK on **Ubuntu**, it is required that the
public repository where the SDK resides is added to the apt sources
list.

To do so, download the appropriate definition file for your
distribution:

  - Ubuntu Precise Pangolin (12.04)
    [i386](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/precise/i386/gstreamer-sdk.list) [amd64](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/precise/amd64/gstreamer-sdk.list)

  - Ubuntu Quantal Quetzal (12.10)
    [i386](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/quantal/i386/gstreamer-sdk.list) [amd64](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/quantal/amd64/gstreamer-sdk.list)

  - Ubuntu Raring Ringtail (13.04)
    [i386](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/raring/i386/gstreamer-sdk.list) [amd64](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/ubuntu/raring/amd64/gstreamer-sdk.list)

And copy it to the `/etc/apt/sources.list.d/` directory on your system:

``` theme: Default; brush: plain; gutter: false
sudo cp gstreamer-sdk.list /etc/apt/sources.list.d/
```

After adding the repositories, the GPG key of the apt repository has to
be added and the apt repository list needs to be refreshed. This can be
done by
running:

``` theme: Default; brush: plain; gutter: false
wget -q -O - http://www.freedesktop.org/software/gstreamer-sdk/sdk.gpg | sudo apt-key add -
sudo apt-get update
```

Now that the new repositories are available, install the SDK with the
following command:

``` theme: Default; brush: plain; gutter: false
sudo apt-get install gstreamer-sdk-dev
```

Enter the superuser/root password when prompted.

![](images/icons/grey_arrow_down.gif)Debian (Click here to expand)

In order to install the SDK on **Debian**, it is required that the
public repository where the SDK resides is added to the apt sources
list.

To do so, download the appropriate definition file for your
distribution:

  - Debian Squeeze (6.0)
    [i386](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/debian/squeeze/i386/gstreamer-sdk.list)
    [amd64](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/debian/squeeze/amd64/gstreamer-sdk.list)

  - Debian Wheezy (7.0)
    [i386](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/debian/wheezy/i386/gstreamer-sdk.list)
    [amd64](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/debian/wheezy/amd64/gstreamer-sdk.list)

And copy it to the `/etc/apt/sources.list.d/` directory on your system:

``` theme: Default; brush: plain; gutter: false
su -c 'cp gstreamer-sdk.list /etc/apt/sources.list.d/'
```

After adding the repositories, the GPG key of the apt repository has to
be added and the apt repository list needs to be refreshed. This can be
done by
running:

``` theme: Default; brush: plain; gutter: false
su -c 'wget -q -O - http://www.freedesktop.org/software/gstreamer-sdk/sdk.gpg | apt-key add -'
su -c 'apt-get update'
```

Now that the new repositories are available, install the SDK with the
following command:

``` theme: Default; brush: plain; gutter: false
su -c 'apt-get install gstreamer-sdk-dev'
```

Enter the superuser/root password when prompted.

![](images/icons/grey_arrow_down.gif)Fedora (Click here to expand)

In order to install the SDK on **Fedora**, it is required that the
public repository where the SDK resides is added to the yum sources
list.

To do so, download the appropriate definition file for your
distribution:

  - [Fedora 17](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/fedora/gstreamer-sdk.repo)

  - [Fedora 18](http://www.freedesktop.org/software/gstreamer-sdk/data/packages/fedora/gstreamer-sdk.repo)

And copy it to the `/etc/yum.repos.d/` directory on your system:

``` theme: Default; brush: plain; gutter: false
su -c 'cp gstreamer-sdk.repo /etc/yum.repos.d/'
```

After adding the repositories, the yum repository list needs to be
refreshed. This can be done by running:

``` theme: Default; brush: plain; gutter: false
su -c 'yum update'
```

Now that the new repositories are available, install the SDK with the
following command:

``` theme: Default; brush: plain; gutter: false
su -c 'yum install gstreamer-sdk-devel'
```

Enter the superuser/root password when prompted.

# Configure your development environment

When building applications using GStreamer, the compiler must be able to
locate its libraries. However, in order to prevent possible collisions
with the GStreamer installed in the system, the GStreamer SDK is
installed in a non-standard location `/opt/gstreamer-sdk`. The shell
script `gst-sdk-shell` sets the required environment variables for
building applications with the GStreamer SDK:

``` theme: Default; brush: bash; gutter: false
/opt/gstreamer-sdk/bin/gst-sdk-shell
```

The only other “development environment” that is required is
the `gcc` compiler and a text editor. In order to compile code that
requires the GStreamer SDK and uses the GStreamer core library, remember
to add this string to your `gcc` command:

``` theme: Default; brush: plain; gutter: false
`pkg-config --cflags --libs gstreamer-0.10`
```

If you're using other GStreamer libraries, e.g. the video library, you
have to add additional packages after gstreamer-0.10 in the above string
(gstreamer-video-0.10 for the video library, for example).

If your application is built with the help of libtool, e.g. when using
automake/autoconf as a build system, you have to run
the `configure `script from inside the `gst-sdk-shell` environment.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>You have also the option to embed the SDK's path into your binaries so they do not need to be executed from within the <code>gst-sdk-shell</code>. To do so, add these options to gcc:</p>
<div class="code panel" style="border-width: 1px;">
<div class="codeContent panelContent">
<pre class="theme: Default; brush: plain; gutter: false" style="font-size:12px;"><code>-Wl,-rpath=/opt/gstreamer-sdk/lib `pkg-config --cflags --libs gstreamer-0.10`</code></pre>
</div>
</div>
<p>In case you are using libtool, it will automatically add the <code>-Wl </code>and<code> -rpath</code> options and you do not need to worry about it.</p></td>
</tr>
</tbody>
</table>

### Getting the tutorial's source code

The source code for the tutorials can be copied and pasted from the
tutorial pages into a text file, but, for convenience, it is also
available in a GIT repository and distributed with the SDK.

The GIT repository can be cloned with:

``` theme: Default; brush: plain; gutter: false
git clone git://anongit.freedesktop.org/gstreamer-sdk/gst-sdk-tutorials
```

Or you can locate the source code in
`/opt/gstreamer-sdk/share/gst-sdk/tutorials`, and copy it to a working
folder of your choice.

### Building the tutorials

You need to enter the GStreamer SDK shell in order for the compiler to
use the right libraries (and avoid conflicts with the system libraries).
Run `/opt/gstreamer-sdk/bin/gst-sdk-shell` to enter this shell.

Then go to the folder where you copied/cloned the tutorials and
write:

``` theme: Default; brush: plain; gutter: false
gcc basic-tutorial-1.c -o basic-tutorial-1 `pkg-config --cflags --libs gstreamer-0.10`
```

Using the file name of the tutorial you are interested in
(`basic-tutorial-1` in this example).

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p><strong>Depending on the GStreamer libraries you need to use, you will have to add more packages to the <code class="western">pkg-config </code>command, besides <code class="western">gstreamer-0.10</code></strong></p>
<p>At the bottom of each tutorial's source code you will find the command for that specific tutorial, including the required libraries, in the required order.</p>
<p>When developing your own applications, the GStreamer documentation will tell you what library a function belongs to.</p></td>
</tr>
</tbody>
</table>

### Running the tutorials

To run the tutorials, simply execute the desired tutorial (**from within
the `gst-sdk-shell`**):

``` theme: Default; brush: cpp; gutter: false
./basic-tutorial-1
```

### Deploying your application

Your application built with the GStreamer SDK must be able locate the
GStreamer libraries when deployed in the target machine. You have at
least a couple of options:

If you want to install a shared SDK, you can put your application
in `/opt/gstreamer-sdk` (next to the SDK) and create a `.desktop` file
in `/usr/share/applications` pointing to it. For this to work without
any problems you must make sure that your application is built with
the `-Wl,-rpath=/opt/gstreamer-sdk/lib` parameter (this is done
automatically by libtool, if you use it).

Or, you can deploy a wrapper script (similar to `gst-sdk-shell`), which
sets the necessary environment variables and then calls your application
and create a `.desktop` file in `/usr/share/applications` pointing to
the wrapper script. This is the most usual approach, doesn't require the
use of the `-Wl,-rpath` parameters and is more flexible. Take a look
at `gst-sdk-shell` to see what this script needs to do. It is also
possible to create a custom wrapper script with
the `gensdkshell` command of the Cerbero build system, if you built
the SDK yourself as explained above.

Document generated by Confluence on Oct 08, 2015 10:27

