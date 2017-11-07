---
short-description: Contributing to GStreamer
...

# How to Contribute to GStreamer

This document provides instructions and guidelines for submitting bug reports
, feature requests and patches to GStreamer. The following applies to all
these operations:

- Please use the [GNOME bugzilla][bugzilla] to perform any of the aforementioned
  operations. You will need to create a GNOME bugzilla account if you don't have
  one yet (yep, that's just how it is. Sorry for the inconvenience).

- Create a new bug if there is no bug report for this issue yet. Bugzilla will
  show you a list of existing and similar-looking issues when you file your
  bug. Please have a look at the list to see if anything looks like it matches.
  The GStreamer [bugs page][bugs] also has shortcuts for the major components
  and simple search functionality if you'd like to browse or search for
  existing bugs.

## How to File Bug Reports and Request for Enhancements

### Where to File Bug Reports and Feature Requests

After completing the common steps:

- If you are filing a feature request (i.e. anything that is not supposed to
  work already, that is anything not a bug), please set your bug's severity
  to *enhancement*. This won't affect the way we prioritise the issue, but
  it will make triaging easier for us.

- If your bug is about a specific plugin, element or utility library,
  please prefix the bug summary with `element-name:`, `plugin-name:` or `lib:`
  and keep the rest of the description as short and precise as possible.

  Examples:

   - `id3demux: fails to extract composer tags`
   - `tsdemux: does not detect audio stream`
   - `Internal flow error when playing matroska file`

  This makes sure developers looking through the list of open bugs or bug
  notification mails can quickly identify what your bug is about. If your text
  is too long and only contains fill words at the beginning, the important
  information will be cut off and not show up in the list view or mail client.

- If you don't know in which component the problem is, just select "don't know"
  and we'll move it to the right component once we have a better idea what the
  problem is.

- Please mention:

   - what version of GStreamer you are using
   - what operating system you are using (Windows, macOS, Linux)
   - if you're on Linux, please mention your distro and distro version
   - if this is on an embedded device please provide details

- Try to describe how the bug can be reproduced. If it is triggered by any
  specific file, try to make the file available somewhere for download and
  put the link into the bug report. The easier it is for us to reproduce
  the issue, the easier it is to fix it.

- If you experience a crash (that is: the application shuts down unexpectedly,
  usually with a segfault or bus error or memory access violation or such),
  please try to [obtain a stack trace][stack-trace]. If there are criticals
  or warnings printed right before the crash, run with the environment variable
  `G_DEBUG=fatal_warnings` set, then it will abort on the first warning, which
  should hopefully give an indication to where the problem is. You can then
  obtain a stack trace from where it aborts.

- If the application errors out, please provide a gst debug log. You can get
  one by setting the `GST_DEBUG=*:6` environment variable, combined with
  `GST_DEBUG_FILE=/tmp/dbg.log`. The resulting file might end up being very
  large, so it's advisable to compress it with `xz -9 /tmp/dbg.log` before
  sharing. You may also be asked to provide debug logs for specific debug
  categories rather than everything (`*:6`).

[stack-trace]: https://wiki.gnome.org/Community/GettingInTouch/Bugzilla/GettingTraces/Details

## How to Submit Patches

### Where to Submit Patches

After completing the common steps:

- Once you have created a bug you can attach your patch(es) to the bug report,
  see below for more details. You can add one attachment when you file the bug,
  but if you have multiple things to attach you will have to do that after the
  bug has been submitted.

- If your patch is for an enhancement (anything that is not supposed to work
  already, i.e. anything not a bug) or adds new API, please set your bug's
  severity to *enhancement*. This won't affect the way we prioritise your bug,
  but it does make triaging easier for us.

- If your patch is against a specific plugin or element or utility library,
  please prefix the bug summary with `element-name:`, `plugin-name:` or `lib:`
  and keep the rest of the description as short and precise as possible.

  Examples:

   - `id3demux: add support for WCOP frame`
   - `riff: add RGB16 support`
   - `playbin: detect if video-sink supports deinterlacing`
   - `tests: rtprtx unit test is racy`

  This makes sure developers looking through the list of open bugs or bug
  notification mails can quickly identify what your bug is about. If your text
  is too long and only contains fill words at the beginning, the important
  information will be cut off and not show up in the list view or mail client.

- Please create separate bugs for separate issues. There is no golden rule when
  something counts as a separate issue, please just use your best judgment. For
  example, if you have a change that needs to be done in each module, one bug
  for all the patches for the various modules is fine. If there is an issue
  that requires related fixes in multiple elements or libraries, please also
  feel free to put everything into one bug report. If you just happen to have
  multiple patches for us but they are not really related, please put them in
  separate bugs. The main question is if it makes sense to discuss and review
  these patches together or if they could just as well be handled completely
  separately.

- Please do not send patches to the gstreamer-devel mailing list. Patches
  submitted on the mailing list are most likely going to be ignored, overlooked,
  or you will get a brief reply asking you to put them into bugzilla. We do
  not use the mailing list for bug review.

- Please do not send pull requests to our github mirror. They will be closed
  automatically.

- Please also do not attach patches to already-existing bugs unless they
  really are directly relevant to the issue, i.e. do not attach patches to
  already-existing bugs that are only vaguely related to your issue.

[bugzilla]: https://bugzilla.gnome.org
[bugs]: https://gstreamer.freedesktop.org/bugs/
[open-bugs]: https://bugzilla.gnome.org/buglist.cgi?product=GStreamer&bug_status=UNCONFIRMED&bug_status=NEW&bug_status=ASSIGNED&bug_status=NEEDINFO&bug_status=REOPENED&form_name=query

### How to Prepare a Patch for Submission

If possible at all, you should prepare patches against a current git checkout,
ideally against the tip of the master branch, but in many cases patches against
a stable release will be acceptable as well if the plugin or code hasn't
changed much since then. If a patch was prepared against an old branch and
does not apply any longer to master you may be asked to provide an updated
patch.

If you have created a new plugin, please submit a patch that adds it to the
gst-plugins-bad module, including `configure.ac` and the various `Makefile.am`
modifications and all new files.

#### Patch Format

Please submit patches in `git format-patch` format, as attachment to a bug
in bugzilla.

The easiest way to create such patches is to create one or more local commits
for your changes in a local git repository. This can be a git clone checkout
of the module in question, or you could create a git repository in any
directory that has the source code, e.g. the directory created when unpacking
the source tarball (using `git init`, then `git add .` and
`git commit -m 'import tarball as initial revision'`).

Once you have a git repository with the original code in it, you can make
your modifications and create a local commit with e.g.

    git commit path/to/file1.[ch]

This will pop up an editor where you can create your commit message. It should
look something like:

    exampledemux: fix seeking without index in push mode

    Without an index we would refuse to seek in push mode. Make
    seeking without an index work by estimating the position
    to seek to. It might not be 100% accurate, but better than
    nothing.

    https://bugzilla.gnome.org/show_bug.cgi?id=987654

Then exit the editor, and you should have a commit.

It's best to run `git add` or `git commit` on specific directories or files
instead of using `git commit -a`, as it's too easy to accidentally contaminate
a patch with changes that belong into it with `git commit -a`, in particular
changes to the `common` submodule.

You can check your commit(s) with `git show` or `git log -p` or using a GUI
such as `gitg` or `gitk`.

Make sure the author is correctly set to your full name and e-mail address.

If you haven't used git before, it would be a good idea to tell it who you are:

    $ git config --global user.name "George S. Treamer"
    $ git config --global user.email "george.s.treamer@example.com"

You can make changes to the last commit using:

 - `git commit --amend` to fix up the commit message

 - `git commit --amend --author='John You <john@you.com>'` to fix up the author

 - `git add path/to/file1.[ch]; git commit --amend` to incorporate fixes
    made to the files since the last commit (i.e. what shows up in `git diff`).
    If you just want to add some of the changes, but not all of them you can
    use `git add -p file.c`, then it will ask you for each individual change
    whether you want to add it or leave it.

Once everything looks fine, create the patch file for the last commit with:

    git format-patch -1

If you have multiple commits, pass -2, -3, etc.

This should create one or more patch files named

    0001-exampledemux-do-this.patch
    0002-exampledemux-also-do-that.patch

in the current directory. Attach these files to a bug report in bugzilla.

Please make sure your patches are as terse and precise as possible. Do not
include 'clean-ups' or non-functional changes, since they distract from the
real changes and make things harder to review, and also lower the chances that
the patch will still apply cleanly to the lastest version in git. If you feel
there are things to clean up, please submit the clean-ups as a separate patch
that does not contain any functional changes.

Try to stick to the GStreamer indentation and coding style. There is a script
called [`gst-indent`][gst-indent] which you can run over your `.c` or `.cpp`
files if you want your code auto-indented before making the patch. The script
requires GNU indent to be installed already. Please do _not_ run `gst-indent` on
header files, our header file indentation is free-form. If you build GStreamer
from git, a local commit hook will be installed that checks if your commit
conforms to the required style (also using GNU indent).

[gst-indent]: http://cgit.freedesktop.org/gstreamer/gstreamer/tree/tools/gst-indent

### Writing Good Commit Messages

Please take the time to write good and concise commit messages.

The first line of each commit message should be a short and concise summary
of the commit. If the commit applies to a specific subsystem, library, plugin
or element, prefix the message with the name of the component, for example:

    oggdemux: fix granulepos query for the old theora bitstream

or

    docs: add new stream API

or

    tests: video: add unit test for converting RGB to XYZ colorspace

This should be a *summary* of the change and _not a description_ of the change.
Meaning: don't say *how* you did something but *what* you fixed, improved or
changed, what the most important practical *effect* of the change is. Example:

    qtdemux: fix crash when doing reverse playback in push mode (good)

instead of

    qtdemux: use signed integer to avoid counter underrun (bad)

The second line of the commit message should be empty.

The third and following lines should contain an extensive *description* and
*rationale* of the change made: what was changed, what was broken, how did it
get fixed, what bugs or issues does this fix? And most importantly: *why* was
something changed.

Trivial commits do not require a description, e.g. if you fix a memory leak
it's usually enough to just say that you fixed a leak. Maybe mention what was
leaked and perhaps also if it was an important leak or only happens in some
corner case error code path, but in any case there's no need to write a long
explanation why leaks are bad or why this needed fixing.

The important part is really what the reasoning behind the change is, since
that's what people want to know if they try to figure out twelve months later
why a line of code does what it does.

If the commit is related to any particular bugs in bugzilla, please add the
full bug URL at the end of the commit message.

We do not use `Signed-off by:` lines in GStreamer, please create patches
without those.

### After Submitting your Patch

Whenever you submit a new bug report, add a comment to an existing bug or add
an attachment to a bug, Bugzilla will send a notification e-mail to GStreamer
developers. This means that there is usually no need to advertise the fact that
you have done so in other forums such as on IRC or on the mailing list, unless
you have been asked to file a bug there, in which case it's nice to follow up
with the link to the bug.

Most of all, please be patient.

We try to review patches as quickly as possible, but there is such a high
turnaround of bugs, patches and feature requests that it is not always
possible to tend to them all as quickly as we'd like. This is especially
true for completely new plugins or new features.

If you haven't received any response at all for a while (say two weeks or so),
do feel free to ping developers by posting a quick follow-up comment on the
bug.

If you do not get a response, this is usually not a sign of people *ignoring*
the issue, but usually just means that it's fallen through the cracks or
people have been busy with other things.

### Tools

#### git-bz

FIXME: add link to docs / repo plus some examples
