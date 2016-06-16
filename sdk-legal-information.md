---
short-description: Patents, Licenses and legal F.A.Q.
...

# Legal information

# Installer, default installation

The installer (Microsoft Windows and MacOSX) and the default
installation (GNU/Linux) contain and install the minimal default
installation. At install time or later, the downloading of optional
components is also possible, but read on for certain legal cautions you
might want to take. All downloads are from the
[gstreamer.freedesktop.org](http://gstreamer.freedesktop.org) website.

# Licensing of SDK

GStreamer SDK minimal default installation only contains packages which
are licensed under the [GNU LGPL license
v2.1](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). This
license gives you the Freedom to use, modify, make copies of the
software either in the original or in a modified form, provided that the
software you redistribute is licensed under the same licensing terms.
This only extends to the software itself and modified versions of it,
but you are free to link the LGPL software as a library used by other
software under whichever license. In other words, it is a weak copyleft
license.

Therefore, it is possible to use the SDK to build applications that are
then distributed under a different license, including a proprietary one,
provided that reverse engineering is not prohibited for debugging
modifications purposes. Only the pieces of the SDK that are under the
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

# Optional packages

There are two types of optional packages (GPL and Patented), which are
under a different license or have other issues concerning patentability
(or both).

### GPL code

Part of the optional packages are under the GNU GPL
[v2](http://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or
[v3](http://www.gnu.org/licenses/gpl-3.0.html). This means that you
cannot link the GPL software in a program unless the same program is
also under the GPL, but you are invited to seek competent advice on how
this works in your precise case and design choices. GPL is called
“strong copyleft” because the condition to distributed under the same
license has the largest possible scope and extends to all derivative
works.

### Patents

Certain software, and in particular software that implements
multimedia standard formats such as MP3, MPEG 2 video and audio, h.264,
MPEG 4 audio and video, AC3, etc, can have patent issues. In certain
countries patents are granted on software and even software-only
solution are by and large considered patentable and are patented (such
as in the United States). In certain others, patents on pure software
solutions are formally prohibited, but granted (this is the case in many
European countries), and in others again are neither allowed nor granted.

It is up to you to make sure that in the countries where the SDK is
used, products are made using it and product are distributed, a license
from the applicable patent holders is required or not. Receiving the SDK
– or links to other downloadable software – does not provide any license
expressed or implied over these patents, except in very limited
conditions where the license so provides. No representation is made.

In certain cases, the optional packages are distributed only as source
code. It is up to the receiver to make sure that in the applicable
circumstances compiling the same code for a given platform or
distributing the object code is not an act that infringes one or more
patents.

# Software is as-is

All software and the entire GStreamer SDK is provided as-is, without any
warranty whatsoever. The individual licenses have particular language
disclaiming liability: we invite you to read all of them. Should you
need a warranty on the fact that software works as intended or have any
kind of indemnification, you have the option to subscribe a software
maintenance agreement with a company or entity that is in that business.
Fluendo and Collabora, as well as some other companies, provide software
maintenance agreements under certain conditions, you are invited to
contact them in order to receive further details and discuss of the
commercial terms.

# Data protection

This website might use cookies and HTTP logs for statistical analysis
and on an aggregate basis only.

# Frequently Asked Questions

#### What licenses are there?

The SDK containst software under various licenses. See above.

#### How does this relate to the packaging system?

The packaging is only a more convenient way to install software and
decide what's good for you. GStreamer is meant to be modular, making use
of different modules, or plugins, that perform different activities.

We provide some of them by default. Others are provided as an additional
download, should you elect to do so. You could do the same by finding
and downloading the same packages for your own platform. So it is
entirely up to you to decide what to do.

Also, we note that SDK elements are divided into different packages,
roughly following the licensing conditions attached to the same. For
instance, the codecs-gpl package contains GPL licensed codecs. All the
packages installed by default, conversely, are licensed under the LGPL
or a more liberal license. This division is provided only for ease of
reference, but we cannot guarantee that our selection is 100% correct,
so it is up to the user to verify the actual licensing conditions before
distributing works that utilize the SDK.

#### Can I / must I distribute the SDK along with my application?

You surely can. All software is Free/Open Source software, and can be
distributed freely. You are not **required** to distribute it. Only,
be reminded that one of the conditions for you to use software under
certain licenses to make a work containing such software, is that you
also distribute the complete source code of the original code (or of
the modified code, if you have modified it). There are alternative
ways to comply with this obligation, some of them do not require any
actual distribution of source code, but since the SDK contains the
entire source code, you might want to include it (or the directories
containing the source code) with your application as a safe way to
comply with this requirement of the license.

#### What happens when I modify the GStreamer SDK's source code?

You are invited to do so, as the licenses (unless you are dealing with
proprietary bits, but in that case you will not find the corresponding
source code) so permit. Be reminded though that in that case you need
to also provide the complete corresponding source code (and to
preserve the same license, of course). You might also consider to push
your modifications upstream, so that they are merged into the main
branch of development if they are worth it and will be maintained by
the GStreamer project and not by you individually. We invite you not
to fork the code, if at all possible.  he Cerbero build system has a
"bundle-source" command that can help you create a source bundle
containing all of the complete corresponding machine readable source
code that you are required to provide.

#### How does licensing relate to software patents? What about software patents in general?

This is a tricky question. We believe software patents should not exist,
so that by distributing and using software on a general purpose machine
you would not violate any of them. But the inconvenient truth is that
they do exist.

Software patents are widely available in the USA. Despite they are
formally prohibited in the European Union, they indeed are granted by
the thousand by the European Patent Office, and also some national
patent offices follow the same path. In other countries they are not
available.

Since patent protection is a national state-granted monopoly,
distributing software that violates patents in a given country could be
entirely safe if done in another country. Fair use exceptions also
exist. So we cannot advice you whether the software we provide would be
considered violating patents in your country or in any other country,
but that can be said for virtually all kinds of sofware. Only, since we
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

In all cases, since most of the software we include in the SDK is under
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
