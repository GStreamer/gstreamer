# GStreamer Legal Issues

<!-- FIXME: this entire section seems out of date and a bit weird. No one
     ever asks questions like that any more. Should be made more relevant -->

This part of the FAQ is based on a series of questions we asked the FSF
to understand how the GPL works and how patents affects the GPL. These
questions were answered by the [FSF lawyers](http://www.fsf.org/), so we
view them as the final interpretation on how the GPL and LGPL interact
with patents in our opinion. This consultancy was paid for by
[Fluendo](http://www.fluendo.com/) in order to obtain clear and quotable
answers. These answers were certified by the FSF lawyer team and
verified by FSF lawyer and law professor Eben Moglen.

> Can someone distribute the combination of:
>  - GStreamer, the LGPL library
>  - MyPlayer, a GPL playback application
>  - The binary-only Sorenson decoder
>
> together in one distribution/operating system? If not, what needs to be
> changed to make this possible?

This would be a problem, because the GStreamer and MyPlayer
licenses would forbid it. In order to link GStreamer to MyPlayer, you
need to use section 3 of the LGPL to convert GStreamer to GPL. The GPL
version of GStreamer forbids linking to the Sorenson decoder. Anyway,
the MyPlayer GPL license forbids this.

If the authors of MyPlayer want to permit this, we have an exception for
them: the controlled interface exception from the FAQ. The idea of this
is that you can't get around the GPL just by including a LGPL bit in the
middle.

Note: MyPlayer is a completely fictituous application at the time of
writing.

> Suppose Apple wants to write a binary-only proprietary plugin for GStreamer
>
> .. to decode Sorenson video, which will be shipped stand-alone,
> not part of a package like in the question above. Can Apple distribute
> this binary-only plugin?

Yes, modulo certain reverse engineering requirements in section 6
of the LGPL.

> If a program released under the GPL uses a library that is LGPL, and
> this library can dlopen plug-ins at runtime, what are the requirements
> for the license of the plug-in?

You may not distribute the plug-in with the GPL application.
Distributing the plug-in alone, with the knowledge that it will be used
primarily by GPL software is a bit of an edge case. We will not advise
you that it would be safe to do so, but we also will not advise you that
it would be absolutely forbidden.

> Can someone in a country that does not have software patents
> distribute code covered by US patents under the GPL to people in, for
> example, Norway? If he/she visits the US, can he/she be arrested?

Yes, he can. No, there are no criminal penalties for patent
infringement in the US.

> Can someone from the US distribute software covered by US patents
> under the GPL to people in Norway? To people in the US?

This might infringe some patents, but the GPL would not forbid it
absent some actual restriction, such as a court judgement or agreement.
The US government is empowered to refuse importation of patent
infringing devices, including software.

> There are a lot of GPL- or LGPL-licensed libraries that handle media
> codecs which have patents. Take mad, an mp3 decoding library, as an
> example. It is licensed under the GPL. In countries where patents are
> valid, does this invalidate the GPL license for this project?

The mere existence of a patent which might read on the program
does not change anything. However, if a court judgement or other
agreement prevents you from distributing libmad under GPL terms, you can
not distribute it at all.

The GPL and LGPL say (sections 7 and 11): “If you cannot distribute so
as to satisfy simultaneously your obligations under this License and any
other pertinent obligations, then as a consequence you may not
distribute the Library at all.”

> So let's say there is a court judgement. Does this mean that the GPL
> license is invalid for the project everywhere, or only in the
> countries where it conflicts with the applicable patents?

The GPL operates on a per-action, not per-program basis. That is,
if you are in a country which has software patents, and a court tells
you that you cannot distribute (say) libmad in source code form, then
you cannot distribute libmad at all. This doesn't affect anyone else.

> Patented decoding can be implemented in GStreamer either by having a
> binary-only plugin do the decoding, or by writing a plugin (with any
> applicable license) that links to a binary-only library. Does this
> affect the licensing issues involved in regards to GPL/LGPL?

No.

> Is it correct that you cannot distribute the GPL mad library to decode
> mp3's, *even* in the case where you have obtained a valid license for
> decoding mp3?

The only GPL-compatible patent licenses are those which are open
to all parties posessing copies of GPL software which practices the
teachings of the patent.

If you take a license which doesn't allow others to distribute original
or modified versions of libmad practicing the same patent claims as the
version you distribute, then you may not distribute at all.
