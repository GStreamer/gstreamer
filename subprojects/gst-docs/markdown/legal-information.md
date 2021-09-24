---
short-description: Patents, Licenses and legal F.A.Q.
...

# Legal information

## Installer, default installation

The installer (Microsoft Windows and MacOSX) and the default
installation (GNU/Linux) contain and install the minimal default
installation. At install time or later, the downloading of optional
components is also possible, but read on for certain legal cautions you
might want to take. All downloads are from the
[gstreamer.freedesktop.org](http://gstreamer.freedesktop.org) website.

## Licensing of GStreamer

GStreamer minimal default installation only contains packages which
are licensed under the [GNU LGPL license
v2.1](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). This
license gives you the Freedom to use, modify, make copies of the
software either in the original or in a modified form, provided that the
software you redistribute is licensed under the same licensing terms.
This only extends to the software itself and modified versions of it,
but you are free to link the LGPL software as a library used by other
software under whichever license. In other words, it is a weak copyleft
license.

Therefore, it is possible to use GStreamer to build applications that are
then distributed under a different license, including a proprietary one,
provided that reverse engineering is not prohibited for debugging
modifications purposes. Only the pieces of GStreamer that are under the
LGPL need to be kept under the LGPL, and the corresponding source code
must be distributed along with the application (or an irrevocable offer
to do so for at least three years from distribution). Please consult
section 6 of the
[LGPL](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) for
further details as to what the corresponding source code must contain.

Some portions of the minimal default installation may be under
different licenses, which are both more liberal than the LGPL (they are
less strict conditions for granting the license) and compatible with the
LGPL. This is advised locally.

## Optional packages

There are two types of optional packages (GPL and Patented), which are
under a different license or have other issues concerning patentability
(or both).

#### GPL code

Part of the optional packages are under the GNU GPL
[v2](http://www.gnu.org/licenses/old-licenses/gpl-2.0.html). This means that
you cannot link the GPL software in a program unless the same program is
also under the GPL, but you are invited to seek competent advice on how
this works in your precise case and design choices. GPL is called
“strong copyleft” because the condition to distributed under the same
license has the largest possible scope and extends to all derivative
works.

#### Patents

Certain software, and in particular software that implements
multimedia standard formats such as MP3, MPEG 2 video and audio, h.264,
MPEG 4 audio and video, AC3, etc, can have patent issues. In certain
countries patents are granted on software and even software-only
solution are by and large considered patentable and are patented (such
as in the United States). In certain others, patents on pure software
solutions are formally prohibited, but granted (this is the case in many
European countries), and in others again are neither allowed nor granted.

It is up to you to make sure that in the countries where GStreamer is
used, products are made using it and product are distributed, a license
from the applicable patent holders is required or not. Receiving GStreamer
– or links to other downloadable software – does not provide any license
expressed or implied over these patents, except in very limited
conditions where the license so provides. No representation is made.

In certain cases, the optional packages are distributed only as source
code. It is up to the receiver to make sure that in the applicable
circumstances compiling the same code for a given platform or
distributing the object code is not an act that infringes one or more
patents.

## Software is as-is

All software and the entire GStreamer binaries are provided as-is, without any
warranty whatsoever. The individual licenses have particular language
disclaiming liability: we invite you to read all of them. Should you
need a warranty on the fact that software works as intended or have any
kind of indemnification, you have the option to subscribe a software
maintenance agreement with a company or entity that is in that business.


## Licensing of code contributed to GStreamer itself

GStreamer is a plugin-based framework licensed under the LGPL. The
reason for this choice in licensing is to ensure that everyone can use
GStreamer to build applications using licenses of their choice.

To keep this policy viable, the GStreamer community has made a few licensing
rules for code to be included in GStreamer's core or GStreamer's
official modules, like our plugin packages.

**We require that all code going into our core packages is LGPL.**

For the plugin code, we require the <B>use of the LGPL for all plugins
written from scratch or linking to external libraries</B>.  The only
exception to this is when plugins contain older code under the BSD and
MIT license.  They can use those licenses instead and will still be
considered for inclusion, we do prefer that all new code written
though is at least dual licensed LGPL. We do not accept GPL code to be
added to our plugins modules, but we do accept LGPL-licensed plugins
using an external GPL library for some of our plugin modules.  The
reason we demand plugins be licensed under the LGPL, even when they
are using a GPL library, is that other developers might want to use
the plugin code as a template for plugins linking to non-GPL
libraries. We also accept dual licensed plugins for inclusion as long
as one of the licenses offered for dual licensing is the LGPL.

We also do not allow plugins under any license into our core,base
or good packages if they have known patent issues associated with
them. This means that even a contributed LGPL/MIT licensed
implementation of something which there is a licensing body claiming
fees for, those plugins would need to go into our gst-plugins-ugly
module.

All new plugins, regardless of licensing or patents
tend to have to go through a period in our incubation module,
gst-plugins-bad before moving to ugly, base or good.

## Frequently Asked Questions

#### What licenses are there?

GStreamer binaries contain software under various licenses. See above.

#### How does this relate to the packaging system?

The packaging is only a more convenient way to install software and
decide what's good for you. GStreamer is meant to be modular, making use
of different modules, or plugins, that perform different activities.

We provide some of them by default. Others are provided as an additional
download, should you elect to do so. You could do the same by finding
and downloading the same packages for your own platform. So it is
entirely up to you to decide what to do.

Also, we note that GStreamer elements are divided into different packages,
roughly following the licensing conditions attached to the same. For
instance, the codecs-gpl package contains GPL licensed codecs. All the
packages installed by default, conversely, are licensed under the LGPL
or a more liberal license. This division is provided only for ease of
reference, but we cannot guarantee that our selection is 100% correct,
so it is up to the user to verify the actual licensing conditions before
distributing works that utilize GStreamer.

#### Can I / must I distribute GStreamer along with my application?

You surely can. All software is Free/Open Source software, and can be
distributed freely. You are not **required** to distribute it. Only,
be reminded that one of the conditions for you to use software under
certain licenses to make a work containing such software, is that you
also distribute the complete source code of the original code (or of
the modified code, if you have modified it). There are alternative
ways to comply with this obligation, some of them do not require any
actual distribution of source code, but since GStreamer contains the
entire source code, you might want to include it (or the directories
containing the source code) with your application as a safe way to
comply with this requirement of the license.

#### What happens when I modify the GStreamer's source code?

You are invited to do so, as the licenses (unless you are dealing with
proprietary bits, but in that case you will not find the corresponding
source code) so permit. Be reminded though that in that case you need
to also provide the complete corresponding source code (and to
preserve the same license, of course). You might also consider to push
your modifications upstream, so that they are merged into the main
branch of development if they are worth it and will be maintained by
the GStreamer project and not by you individually. We invite you not
to fork the code, if at all possible. The Cerbero build system has a
"bundle-source" command that can help you create a source bundle
containing all of the complete corresponding machine readable source
code that you are required to provide.

#### How does licensing relate to software patents? What about software patents in general?

This is a tricky question. We believe software patents should not exist,
so that by distributing and using software on a general purpose machine
you would not violate any of them. But the inconvenient truth is that
they do exist.

Software patents are widely available in the USA. Even though they are
formally prohibited in the European Union, they indeed are granted by
the thousand by the European Patent Office, and also some national
patent offices follow the same path. In other countries they are not
available.

Since patent protection is a national state-granted monopoly,
distributing software that violates patents in a given country could be
entirely safe if done in another country. Fair use exceptions also
exist. So we cannot advise you whether the software we provide would be
considered violating patents in your country or in any other country,
but that can be said for virtually all kinds of software. Only, since we
deal with audio-video standards, and these standards are by and large
designed to use certain patented technologies, it is common wisdom that
the pieces of software that implement these standards are sensitive in
this respect.

This is why GStreamer has taken a modular approach, so that you can use
a Free plugins or a proprietary, patent royalty bearing, plugin for a
given standard.

#### What about static vs. dynamic linking and copyleft?

We cannot provide one single answer to that question. Since copyright in
software works as copyright in literature, static linking means
basically that the programmer includes bits of code of the original
library in the bytecode at compile time. This amounts to make derivative
code of the library without conceivable exceptions, so you need a
permission from the copyright holders of the library to do this.

A widespread line of thinking says that dynamic linking is conversely
not relevant to the copyleft effect, since the mingling of code in a
larger work is done at runtime. However, another equally authoritative
line of thought says that only certain type of dynamic linking is not
copyright relevant.  Therefore, using a library that is specifically
designed to be loaded into a particular kind of software, even through
API,  requires permission by the copyright holder of the library when
the two pieces are distributed together.

In all cases, since most of the software we include in GStreamer is under
the LGPL, this permission is granted once for all, subject to compliance
with the conditions set out by it. Therefore, the problem only arises
when you want to use GPL libraries to make non-GPL applications, and you
need to audit your software in that case to make sure that what you do
is not an infringement. This is why we have put the GPL libraries in a
separate set of optional components, so you have a clearer view of what
is safely clear for use, and what might need better investigation on a
case-by-case basis.

Please be reminded that even for LGPL, the recipient of the software
must be in a position to replace the current library with a modified
one, and to that effect some conditions apply, among which that for
static linking you must also provide the complete toolchain required to
relink the library (“any data and utility programs needed for
reproducing the executable from it”, except the “major components”) and
that the license of the conditions of the resulting program must allow
decompilation to debug modifications to the library.

## Licensing applications under the GNU GPL using GStreamer

The licensing of GStreamer is no different from a lot of other libraries out
there like GTK+ or glibc:
we use the [LGPL](http://www.fsf.org/licenses/lgpl.html).

What complicates things with regards to GStreamer is its plugin-based design
and the heavily patented and proprietary nature of many multimedia codecs.
While patents on software are currently only allowed in a small minority of
world countries (the US and Australia being the most important of those), the
problem is that due to the central place the US hold in the world economy and
the computing industry, software patents are hard to ignore wherever you are.

Due to this situation, many companies, including major GNU/Linux distributions,
get trapped in a situation where they either get bad reviews due to lacking
out-of-the-box media playback capabilities (and attempts to educate the
reviewers have met with little success so far), or go against their
own - and the free software movement's - wish to avoid proprietary software.

Due to competitive pressure, most choose to add some support. Doing that
through pure free software solutions would have them risk heavy litigation and
punishment from patent owners. So when the decision is made to include support
for patented codecs, it leaves them the choice of either using special
proprietary applications, or try to integrate the support for these codecs
through proprietary plugins into the multimedia infrastructure provided by
GStreamer. Faced with one of these two evils the GStreamer community of
course prefer the second option.

The problem which arises is that most free software and open source
applications developed use the GPL as their license. While this is generally a
good thing, it creates a dilemma for people who want to put together a
distribution.  The dilemma they face is that if they include proprietary
plugins in GStreamer to support patented formats in a way that is legal for
them, they do risk running afoul of the GPL license of the applications. We
have gotten some conflicting reports from lawyers on whether this is actually a
problem, but the official stance of the FSF is that it is a problem.
We view the FSF as an authority on this matter, so we are inclined to follow
their interpretation of the GPL license.

So what does this mean for you as an application developer? Well, it
means **you have to make an active decision on whether you want your
application to be used together with proprietary plugins or
not**. What you decide here will also influence the chances of
commercial distributions and Unix vendors shipping your
application. The GStreamer community suggest you license your software
using a license that will allow non-free, patent implementing or
non-GPL compatible plugins to be bundled with GStreamer and your
applications, in order to make sure that as many vendors as possible
go with GStreamer instead of less free solutions.  This in turn we
hope and think will let GStreamer be a vehicle for wider use of free
formats like the [Xiph.org](http://www.xiph.org/) formats.

If you do decide that you want to allow for non-free plugins to be used with
your application you have a variety of choices. One of the simplest is using
licenses like LGPL, MPL or BSD for your application instead of the GPL.
Or you can add a exceptions clause to your GPL license stating that you except
GStreamer plugins from the obligations of the GPL.

A good example of such a GPL exception clause would be, using the Totem
video player project as an example:

*The developers of the Totem video player hereby grants permission for
non-GPL compatible GStreamer plugins to be used and distributed together
with GStreamer and Totem. This permission is above and beyond the permissions
granted by the GPL license by which Totem is covered. If you modify this code,
you may extend this exception to your version of the code, but you are
not obligated to do so. If you do not wish to do so, delete this exception
statement from your version.*

Our suggestion among these choices is to use the LGPL license, as it is what
resembles the GPL most and it makes it a good licensing fit with the major
GNU/Linux desktop projects like GNOME and KDE.  It also allows you to share
code more openly with projects that have compatible licenses. As you might
deduce, pure GPL licensed code without the above-mentioned clause is not
re-usable in your application under a GPL plus exception clause unless you
get the author of the pure GPL code to allow a relicensing to GPL plus
exception clause. By choosing the LGPL, there is no need for an exception
clause and thus code can be shared freely between your application and
other LGPL using projects.

We have above outlined the practical reasons for why the GStreamer community
suggest you allow non-free plugins to be used with your applications. We feel
that in the multimedia arena, the free software community is still not strong
enough to set the agenda and that blocking non-free plugins to be used in our
infrastructure hurts us more than it hurts the patent owners and their ilk.

This view is not shared by everyone.
The [Free Software Foundation](http://www.fsf.org) urges you to use
an unmodified GPL for your applications, so as to push back against the
temptation to use non-free plug-ins. They say that since not everyone else has
the strength to reject them because they are unethical, they ask your help to
give them a legal reason to do so.
