---
short-description: Contributing to GStreamer
...

# How to Contribute to GStreamer

This document provides instructions and guidelines for submitting issues,
feature requests and patches to GStreamer. The following applies to all
these operations:

- Please use [Freedesktop.org Gitlab][gitlab] to perform any of the aforementioned
  operations. You will need to create a Freedekstop.org Gitlab account if you
  don't have one yet (yep, that's just how it is. Sorry for the inconvenience).

## How to File Issues and Request for Enhancements

### Where to File Issues and Feature Requests

- Create a new issue if there is no report for this problem yet.
  The GStreamer [bugs page][bugs] also has shortcuts for the major components
  and simple search functionality if you'd like to browse or search for
  existing issues.

- If you are filing a feature request (i.e. anything that is not supposed to
  work already, that is anything not an issue), please add the *Enhancement* label.
  Feel free to add any other appropriate already existing labels.  Please don't
  create new labels just for your issue. This won't affect the way we prioritise
  the issue, but it will make triaging easier for us.

- If your issue is about a specific plugin, element or utility library,
  please prefix the issue summary with `element-name:`, `plugin-name:` or `lib:`
  and keep the rest of the description as short and precise as possible.

  Examples:

   - `id3demux: fails to extract composer tags`
   - `tsdemux: does not detect audio stream`
   - `Internal flow error when playing matroska file`

  This makes sure developers looking through the list of open issues or issue
  notification mails can quickly identify what your issue is about. If your text
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

- Try to describe how the issue can be reproduced. If it is triggered by any
  specific file, try to make the file available somewhere for download and
  put the link into the issue. The easier it is for us to reproduce
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

- Once you have created an issue you can submit a merge request, see below for
  more details.

- If your change is for an enhancement (anything that is not supposed to work
  already, i.e. anything not a bug) or adds new API, please add the
  *Enhancement* label. This won't affect the way we prioritise your issue,
  but it does make triaging easier for us.

- If your merge request is against a specific plugin or element or utility library,
  please prefix the merge request summary with `element-name:`, `plugin-name:`
  or `lib:` and keep the rest of the description as short and precise as possible.

  Examples:

   - `id3demux: add support for WCOP frame`
   - `riff: add RGB16 support`
   - `playbin: detect if video-sink supports deinterlacing`
   - `tests: rtprtx unit test is racy`

  This makes sure developers looking through the list of open merge requests or
  notification mails can quickly identify what your change is about. If your text
  is too long and only contains fill words at the beginning, the important
  information will be cut off and not show up in the list view or mail client.

- Please enable the "Allow commits from members who can merge to the target branch"
  checkbox when submitting merge requests as otherwise maintainers might
  request you to rebase your Merge Request multiple times even if no manual
  changes are required.

- Make liberal use of the reference syntax available to help cross-linking
  different issues and merge requests. e.g. `#100` references issue 100 in the
  current module. `!100` references merge request 100 in the current project.
  A complete list is available from [gitlab's documentation][special-md-references].

- Please create separate merge requests for separate issues and for different
  modules. There is no golden rule when something counts as a separate issue,
  please just use your best judgment.  If a merge request is related to another
  merge request in another module please mention that in the description using
  a gitlab reference as outlined above.  For example, if you have a change that
  needs to be done in each module, one issue with one merge request per module
  is fine. If there is an issue that requires related fixes in multiple elements
  or libraries, please also feel free to put everything into one issue. If you
  just happen to have multiple patches for us but they are not really related,
  please put them in separate issues and merge requests. The main question is
  if it makes sense to discuss and review these patches together or if they
  could just as well be handled completely separately.

- Please do not send patches to the gstreamer-devel mailing list. Patches
  submitted on the mailing list are most likely going to be ignored, overlooked,
  or you will get a brief reply asking you to put them into gitlab. We do
  not use the mailing list for patch review.

- Please do not send pull requests to our github mirror. They will be closed
  automatically.

- Please do not attach patches to existing bugs on [GNOME Bugzilla][bugzilla]
  If you want to reopen an already closed bug, let one of the developers know
  and we will look into that on a case-by-case basis.

- Please do not attach patches to issues.


[special-md-references]: https://docs.gitlab.com/ee/user/markdown.html#special-gitlab-references
[bugzilla]: https://bugzilla.gnome.org/
[bugs]: https://gstreamer.freedesktop.org/bugs/
[gitlab]: https://gitlab.freedesktop.org/gstreamer

### How to Prepare a Merge Request for Submission

If possible at all, you should prepare a merge request against a current git
checkout, ideally against the tip of the master branch.  The gitlab mrege request
UI will contain information about whether the merge request can be applied to the
current code. If a merge request was prepared against an old commit and
does not apply any longer to master you may be asked to provide an updated
branch to merge.

If you have created a new plugin, please submit a merge request that adds it to
the gst-plugins-bad module, including `configure.ac`, the various `Makefile.am`
modifications, `meson.build` modifications, and all new files.

#### Patch Format

The easiest way to create a merge request is to create one or more local commits
for your changes in a branch in a local git repository. This should be a git
clone checkout of your fork of the module in question.  To fork a module go to
the module in question (e.g.
[https://gitlab.freedesktop.org/gstreamer/gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer))
and hit the fork button.  A new repository will be created in your user namespace
and should accessible as
[https://gitlab.freedesktop.org/$USERNAME/gstreamer](https://gitlab.freedesktop.org/$USERNAME/gstreamer).
You should clone this repository with valid ssh credentials to be able to
automatically push code to your fork.

Once you have a git repository with the original code in it, you should create a
branch for your change. e.g. to create a branch and checkout:

    git checkout -b topic-branch

Then you can make your modifications and create a local commit with e.g.

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

Once everything looks fine, push your branch to your local fork e.g. using

    git push origin topic-branch

This push will display a link to be able create a merge request from your branch.
Click this link and fill out the details of the merge request.  You can also
create a merge request from an existing branch. See the
[gitlab documentation][create-mr] for more details.

Please make sure your commits are as terse and precise as possible. Do not
include 'clean-ups' or non-functional changes, since they distract from the
real changes and make things harder to review, and also lower the chances that
the patch will still apply cleanly to the latest version in git. If you feel
there are things to clean up, please submit the clean-ups as a separate patch
that does not contain any functional changes.

Try to stick to the GStreamer indentation and coding style. There is a script
called [`gst-indent`][gst-indent] which you can run over your `.c` or `.cpp`
files if you want your code auto-indented before making the patch. The script
requires GNU indent to be installed already. Please do _not_ run `gst-indent` on
header files, our header file indentation is free-form. If you build GStreamer
from git, a local commit hook will be installed that checks if your commit
conforms to the required style (also using GNU indent).

[gst-indent]: https://gitlab.freedesktop.org/gstreamer/gstreamer/tree/master/tools/gst-indent
[create-mr]: https://docs.gitlab.com/ee/gitlab-basics/add-merge-request.html

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

If the commit is related to any particular issues in gitlab, please add the
full issue URL at the end of the commit message.

    https://gitlab.freedesktop.org/gstreamer/gstreamer/issues/123

We do not use `Signed-off by:` lines in GStreamer, please create commits
without those.

### After Submitting your Patch

Whenever you submit a new merge request, add a comment to an existing issue or
merge request, Gitlab will send a notification e-mail to GStreamer
developers. This means that there is usually no need to advertise the fact that
you have done so in other forums such as on IRC or on the mailing list, unless
you have been asked to file an issue there, in which case it's nice to follow up
with the link to the issue.

Most of all, please be patient.

We try to review patches as quickly as possible, but there is such a high
turnaround of issues, merge requests and feature requests that it is not always
possible to tend to them all as quickly as we'd like. This is especially
true for completely new plugins or new features.

If you haven't received any response at all for a while (say two weeks or so),
do feel free to ping developers by posting a quick follow-up comment on the
issue or merge request.

If you do not get a response, this is usually not a sign of people *ignoring*
the issue, but usually just means that it's fallen through the cracks or
people have been busy with other things.
