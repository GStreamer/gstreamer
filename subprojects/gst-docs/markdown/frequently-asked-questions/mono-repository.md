# GStreamer mono repository FAQ

## Executive Summary: What is all this monorepo talk?

The GStreamer multimedia framework is a set of libraries and plugins split into a number of distinct modules which are released independently and which have so far been developed in separate git repositories in [freedesktop.org GitLab](https://gitlab.freedesktop.org/gstreamer/).

In addition to these separate git repositories there was a `gst-build` meta-repository that would use the Meson build systems's subproject feature to download each individual module and then build everything in one go. It would also provide a development environment that made it easy to work on GStreamer and use or test versions other than the system-installed GStreamer version.

All of these modules have now (as of 28 September 2021) been merged into a single git repository ("Mono repository" or "monorepo") which should simplify development workflows and continuous integration, especially where changes need to be made to multiple modules at once.

This mono repository merge will primarily affect GStreamer developers and contributors and anyone who has workflows based on the GStreamer git repositories.

The Rust bindings and Rust plugins modules have not been merged into the mono repository at this time because they follow a different release cycle.

The mono repository lives in the existing GStreamer core git repository in the new `"main"` branch and all future development will happen on this `"main"` branch.

## I'm a contributor - what should I do with my pending Merge Requests in Gitlab?

Since it's not possible to just move Merge Requests into another module with the push of a button, and we can't create new merge requests (MRs) in your name in gitlab, we wrote a little script that you can run yourself to move existing MRs from other modules to the mono repo in the main gstreamer module.

The script is located in [the gstreamer repository] and you can start moving your pending MRs simply by calling it ([`scripts/move_mrs_to_monorepo.py`][move_mrs_to_monorepo]) and following the instructions. If you already have a checkout, you may need to switch to the `main` branch.

[the gstreamer repository]: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main
[move_mrs_to_monorepo]: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/scripts/move_mrs_to_monorepo.py

With this script you'll be able to move any open MRs of yours in the various GStreamer modules to the gstreamer mono repository.

The script will keep the discussion history and link to the previous MRs.

The script will also check that there is no existing MR already to avoid duplication.

Before you can run the script, you first need to go to your profile page in GitLab to generate a gitlab access token:

https://gitlab.freedesktop.org/-/profile/personal_access_tokens

On this page you'll need to give a **token name**, grant access
to (API, read_user, read_api) and then press the *create personal access token* button.

You should see a value in the *token name* edit box. The name is just for you, you can call it anything you like (e.g. "scripts").

![](images/faq-monorepo-gitlab-personal-access-token-dialog.png)


When you have a token such as "edEFoqK3tATMj-XD6pY_" you should be able to access the gitlab API and run the script:

```
GITLAB_API_TOKEN=edEFoqK3tATMj-XD6pY_ ./scripts/move_mrs_to_monorepo.py
```

In case you have several merge requests, you can run it with just one merge request first, e.g. if the initial merge request you want to move for testing purposes is

https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/-/merge_requests/1277

you can run:

```
GITLAB_API_TOKEN=zytXYboB5yi3uggRpBM6 ./scripts/move_mrs_to_monorepo.py -mr https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/-/merge_requests/1277

```

When you are ready, you can also run the same script without any parameters to browse and move all your Merge Requests:

```
GITLAB_API_TOKEN=zytXYboB5yi3uggRpBM6 ./scripts/move_mrs_to_monorepo.py
```

Don't worry - the script will prompt you for input along the way before it does anything.



### Must I use the script? Can't I just open a new MR?

The script will move existing discussions and comments. This is particularly useful for MRs that have been reviewed already and have open discussion items. This makes sure we don't accidentally merge something even though there were outstanding issues, which we wouldn't know if you just filed a new MR and closed the old Merge Request.

If the existing Merge Request has not had any review comments yet or is something simple, there's no problem just opening a new MR against the mono repo and closing the old one instead of course.

### What about existing MRs in the GStreamer repository?

Any existing MRs against the GStreamer core repository will have been made against the git `"master"` branch.

You do not need to use any scripts to update these Merge Requests, simply rebase them on top of the `"main"` branch.

You can do this via the GitLab user interface by editing the issue and then changing the target branch.

## I'm a contributor - I have a branch in gst-plugins-XX or one of the other modules that I have not proposed upstream yet, how can I get it rebased onto the `gstreamer` repository?

We provide a [scripts/rebase-branch-from-old-module.py](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/scripts/rebase-branch-from-old-module.py) script in the `gstreamer` repository that you should use to rebase branches from the old GStreamer module repositories onto the main `gstreamer` repository.

This script will only modify your local gstreamer mono repository checkout and not upload anything to GitLab or create any Merge Requests of course. You don't even need a GitLab account to run it. You can use the script as following:

```
./scripts/rebase-branch-from-old-module.py https://gitlab.freedesktop.org/user/gst-plugins-bad my_wip_dev_branch
```

## I use or distribute the release tarballs - how will this affect me?

We will continue to release the various GStreamer modules individually as tarballs, so if you only consume tarballs the move to a mono repository should not affect you at all.

## I use or distribute the release tarballs but would rather not bother with all those separate module tarballs - is the monorepo going to do anything for me?

In future (>= v1.19.3) you will be able to simply download a mono repo tarball for the [release tag](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tags) in gitlab which will then contain all modules same as the monorepo in git.

## I use gst-build - what should I use now?

In short, the `gstreamer` repository has now become what `gst-build` used to be, with the small difference that the code for all the main GStreamer modules is now already in the repository in the subprojects folder, and doesn't need to be downloaded via Meson as part of the build process.

Just `git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git` and work from there. It should be very similar to what you are used to from gst-build.

`gst-build` is dead, long live `gstreamer`!

## I use cerbero - how will this affect me?

If you use cerbero this won't affect you.

Cerbero has been updated to build the latest and greatest GStreamer from the mono repository now.

If you used the `"master"` branch of cerbero to build GStreamer from git master you will need to switch to the `"main"` branch now. All future cerbero development will continue on the `"main"` branch, the `"master"` branch is frozen on the 1.19.2 release and may be removed in future.

Stable branches of cerbero will continue to build stable versions of GStreamer just like before.

## If GStreamer is now gst-build, where has the GStreamer core library code gone?

It's still there, it just moved to the `subprojects/gstreamer/` directory.

## Does that mean all the commit hashes have changed? How can we verify the history?

All the modules have been imported as-is, with history and commit hashes unchanged.

## Does that mean we will be able to easily git bisect across modules now?

Why yes, yes it does. At least going forward. git bisecting into pre-monorepo history will not work.

## Have all modules been moved into the mono repository?

No, the Rust bindings and Rust plugins modules stay separate for now since they follow a difference release cycle.

Cerbero (our package builder for Windows, Android, macOS and iOS) also stays in its own repository.

## Where should I submit new Merge Requests now?

Please submit all new Merge Requests against the [gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer) module in gitlab on top of the `main` branch.

## Where should I submit new issues and feature requests now?

Please submit all new issues and feature requests against the [gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer) module in gitlab (after checking for existing issues first ideally).

## I have some open issues in GitLab - is there a script to move those as well?

No, issues will be moved either by a GStreamer developer over the next couple of weeks and months, or you can also move your own issues yourself if you like and if you believe that they still apply to the latest version of GStreamer.

There should be a `Move issue` button at the bottom right if it's your own issue or you are a GStreamer developer.

Please move issues to the `gstreamer/gstreamer` module.

Before you move an issue, please make sure that the issue summary line has an appropriate prefix that makes it easy for others to see what component of GStreamer this issue is about (e.g. the plugin name or helper library) without looking at tags or the issue description. Edit the issue to adjust the summary line if needed before moving it.

## What will happen to all the open issues and Merge Requests in GitLab in the other modules?

Over the next couple of weeks and months we will make a concerted effort to go over and triage all open issues and those merge requests which haven't been moved by their authors.

Issues that look still relevant and useful will over time be moved over to the [gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer) module in gitlab, and GStreamer developers and maintainers should feel free to do so using the `Move issue` button in Gitlab when they come across issues.

## Why don't you just mass-move all open issues?

There are several thousand open issues in GitLab, and several hundred open Merge Requests.

Mass-moving all those issues and MRs would just lead to a couple of thousand e-mail notifications that would get deleted immediately without anyone ever taking a look a those issues.

The mono repository migration provides a unique opportunity to systematically review all open issues and Merge Requests and weed out anything that looks no longer relevant or useful.

We may still decide to automatically move any remaining open issues at some point in future of course.

## What will happen to the existing git repositories for the other modules?

All existing git repositories in GitLab will be retained of course.

We may close them for new issues and Merge Requests however, to ensure that all new issues and Merge Requests are filed against the mono repository.

We may also archive those modules at some point in future once we have triaged any remaining open issues and Merge Requests. This would just affect the visibility of the module in GitLab however: Issue, MR and git repository URLs and branches and tags and such would continue to work as before in that case.

## What will happen to the existing "master" branches in all the modules?

For the time being all "master" branches are retained, but frozen on the 1.19.2 release.

If you have scripts or workflows that use the "master" branch, you should either update them to use the new mono repository or pin them to the 1.19.2 tag instead.

We may remove the "master" branch at some point in future.

## I have workflows that build GStreamer modules from git but don't want to use the gst-build-style metabuild setup - what should I do now?

You can still build each GStreamer module individually by changing into the `subprojects/$gst_module` directory and then running Meson with that directory as the source directory.

## I want to cherry-pick a patch from the upstream mono repository into my <= 1.18 repository:

As the subtree changed in the monorepo organization (ie subprojects/gst-pluigns-xx), you can give a try to the following git command to resolve the path conflict:

```
$ git cherry-pick <commit> ... --strategy=subtree
```

## I have another question related to the mono repository - where is the best place to ask or get help?

Best to just pop into our [Matrix room][matrix] or start a discussion on [Discourse][discourse].

You can also file an issue in GitLab if you have a question that you think might be worth adding to this FAQ.

[matrix]: https://matrix.to/#/#gstreamer:gstreamer.org
[discourse]: https://discourse.gstreamer.org/

- - -

*This FAQ has been prepared by Thibault Saunier and Tim-Philipp Müller with contributions from Mathieu Duponchelle.*

*License: [CC BY-SA 4.0](http://creativecommons.org/licenses/by-sa/4.0/)*
