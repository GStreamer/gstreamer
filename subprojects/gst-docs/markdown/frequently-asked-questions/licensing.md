---
short-description: Licensing your applications and plugins for use with GStreamer
...

<!-- FIXME: merge this page with the former sdk's legal-information.md
            and the legal section from the FAQ
-->

# Licensing your applications and plugins for use with GStreamer

This document is the result of many discussions both inside the
GStreamer community and with stakeholders outside the community. It
includes the results of discussions with lawyers, including official
representatives of the FSF, to help us ensure we cover the legal issues
as correctly as possible. This does not mean the FSF or anyone else
endorse the opinions in this page. The opinions only represent the rough
consensus of the GStreamer community. The advice contained in here is
meant as information and guidance for people developing free and open
source software using the GStreamer library, so they are aware of the
consequences of their choices. People developing proprietary software or
people distributing GStreamer might also find this document useful in
order to understand how GStreamer works in a licensing context.

This text is also meant to explain a little about our thinking in
regards to how to deal with the problem of software patents which is an
even bigger pain in the field of multimedia than other fields of
programming.

For more information on licensing you can check out our [legal
FAQ](frequently-asked-questions/legal.md)

## Licensing of code contributed to GStreamer itself

GStreamer is a plugin-based framework licensed under the LGPL. The
reason for this choice in licensing is to ensure that everyone can use
GStreamer to build applications using licenses of their choice.

To keep this policy viable, the GStreamer community has made a few
licensing rules for code to be included in GStreamer's core or
GStreamer's official modules, like our plugin packages. **We require
that all code going into our core packages is LGPL**. For the plugin
code, we require the **use of the LGPL for all plugins written from
scratch or linking to external libraries**. The only exception to this
is when plugins contain older code under the BSD and MIT license. They
can use those licenses instead and will still be considered for
inclusion, we do prefer that all new code written though is at least
dual licensed LGPL. We do not accept GPL code to be added to our plugins
modules, but we do accept LGPL-licensed plugins using an external GPL
library for some of our plugin modules. The reason we demand plugins be
licensed under the LGPL, even when they are using a GPL library, is that
other developers might want to use the plugin code as a template for
plugins linking to non-GPL libraries. We also accept dual licensed
plugins for inclusion as long as one of the licenses offered for dual
licensing is the LGPL.

We also do not allow plugins under any license into our core,base or
good packages if they have known patent issues associated with them.
This means that even a contributed LGPL/MIT licensed implementation of
something which there is a licensing body claiming fees for, those
plugins would need to go into our gst-plugins-ugly module.

All new plugins, regardless of licensing or patents tend to have to go
through a period in our incubation module, gst-plugins-bad before moving
to ugly, base or good.

## Licensing of applications using GStreamer

The licensing of GStreamer is no different from a lot of other libraries
out there like GTK+ or glibc: we use the [LGPL][LGPL]. What complicates things
with regards to GStreamer is its plugin-based design and the heavily
patented and proprietary nature of many multimedia codecs. While patents
on software are currently only allowed in a small minority of world
countries (the US and Australia being the most important of those), the
problem is that due to the central place the US hold in the world
economy and the computing industry, software patents are hard to ignore
wherever you are.

Due to this situation, many companies, including major GNU/Linux
distributions, get trapped in a situation where they either get bad
reviews due to lacking out-of-the-box media playback capabilities (and
attempts to educate the reviewers have met with little success so far),
or go against their own - and the free software movement's - wish to
avoid proprietary software. Due to competitive pressure, most choose to
add some support. Doing that through pure free software solutions would
have them risk heavy litigation and punishment from patent owners. So
when the decision is made to include support for patented codecs, it
leaves them the choice of either using special proprietary applications,
or try to integrate the support for these codecs through proprietary
plugins into the multimedia infrastructure provided by GStreamer. Faced
with one of these two evils the GStreamer community of course prefer the
second option.

The problem which arises is that most free software and open source
applications developed use the GPL as their license. While this is
generally a good thing, it creates a dilemma for people who want to put
together a distribution. The dilemma they face is that if they include
proprietary plugins in GStreamer to support patented formats in a way
that is legal for them, they do risk running afoul of the GPL license of
the applications. We have gotten some conflicting reports from lawyers
on whether this is actually a problem, but the official stance of the
FSF is that it is a problem. We view the FSF as an authority on this
matter, so we are inclined to follow their interpretation of the GPL
license.

So what does this mean for you as an application developer? Well, it
means **you have to make an active decision on whether you want your
application to be used together with proprietary plugins or not**. What
you decide here will also influence the chances of commercial
distributions and Unix vendors shipping your application. The GStreamer
community suggest you license your software using a license that will
allow non-free, patent implementing or non-GPL compatible plugins to be
bundled with GStreamer and your applications, in order to make sure that
as many vendors as possible go with GStreamer instead of less free
solutions. This in turn we hope and think will let GStreamer be a
vehicle for wider use of free formats like the
[Xiph.org](http://www.xiph.org/) formats.

If you do decide that you want to allow for non-free plugins to be used
with your application you have a variety of choices. One of the simplest
is using licenses like LGPL, MPL or BSD for your application instead of
the GPL. Or you can add a exceptions clause to your GPL license stating
that you except GStreamer plugins from the obligations of the GPL.

A good example of such a GPL exception clause would be, using the Totem
video player project as an example:

*The developers of the Totem video player hereby grants permission for
non-GPL compatible GStreamer plugins to be used and distributed together
with GStreamer and Totem. This permission is above and beyond the
permissions granted by the GPL license by which Totem is covered. If you
modify this code, you may extend this exception to your version of the
code, but you are not obligated to do so. If you do not wish to do so,
delete this exception statement from your version.*

Our suggestion among these choices is to use the LGPL license, as it is
what resembles the GPL most and it makes it a good licensing fit with
the major GNU/Linux desktop projects like GNOME and KDE. It also allows
you to share code more openly with projects that have compatible
licenses. As you might deduce, pure GPL licensed code without the
above-mentioned clause is not re-usable in your application under a GPL
plus exception clause unless you get the author of the pure GPL code to
allow a relicensing to GPL plus exception clause. By choosing the LGPL,
there is no need for an exception clause and thus code can be shared
freely between your application and other LGPL using projects.

We have above outlined the practical reasons for why the GStreamer
community suggest you allow non-free plugins to be used with your
applications. We feel that in the multimedia arena, the free software
community is still not strong enough to set the agenda and that blocking
non-free plugins to be used in our infrastructure hurts us more than it
hurts the patent owners and their ilk.

This view is not shared by everyone. The [Free Software
Foundation](http://www.fsf.org) urges you to use an unmodified GPL for
your applications, so as to push back against the temptation to use
non-free plug-ins. They say that since not everyone else has the
strength to reject them because they are unethical, they ask your help
to give them a legal reason to do so.

[LGPL]: http://www.fsf.org/licenses/lgpl.html
