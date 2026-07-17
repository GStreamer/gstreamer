# GStreamer Developer policy

The GStreamer project has several kinds of special status beyond being a guest
named after the [GitLab user roles](https://docs.gitlab.com/ee/user/permissions.html).

* [**CI-OK**](https://gitlab.freedesktop.org/groups/gstreamer/ci-ok/-/group_members)
  users have permission to manually trigger continuous integration (CI) jobs.

* **Reporters** can report issues and submit merge requests. Additionally they
  can edit tags in issues (a.k.a "Work Items"). This role is useful for
  contributors helping with triaging.

* **Developers** have direct read-write access to the repository, enabling them
  to commit changes, using the Marge bot exclusively. They can also assign Marge
  to pull requests reviewed by others if authors ask developers to do so. They
  also have permission to add labels to Merge Requests, in particular labels such
  as `Merge in 5 days` and similar.

New GStreamer Developers will be selected by the set of existing GStreamer
Developers/Maintainers/Owners

The list of current GStreamer Developers and Maintainers can be consulted on the
[GStreamer GitLab](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/project_members).

## Choosing Developers

A candidate for GStreamer Developer should initially be nominated by another
developer using a private issue in the
[gstreamer-project](https://gitlab.freedesktop.org/gstreamer/gstreamer-project/-/issues/new)
project, in accordance with the criteria below. If an objection is raised, the
developers should discuss the matter and try to come to consensus. If there are
objections from more than one person the application will be deferred and
revisited in 6 months time. In the mean time a feedback message can be prepared
and delivered to the applicant.

Once someone is successfully nominated for GStreamer Developer status, the
GitLab role of the new developer will be updated accordingly and the nomination
issue will be closed.

New developers will usually aquire their status with respect to certain areas in
which they have worked and have proven their experience, and will usually be
awarded their new status in order to be able to take on a more active role in
those specific areas (such as reviewing/merging other people's MRs).

GStreamer is a wide and complex project, no one is expected to know everything.
When contributing to areas that are less well understood, we expect Developers
to make stronger efforts to get their code reviewed.

## Criteria for Developers

A GStreamer Developer should be a person who has shown particularly good
judgment, understanding of project policies, collaboration skills, and
understanding of the code. Developers are expected to ensure that the merge-requests
they review follow project policies, don't introduce unnecessary maintenance
burdens and to do their best to check for bugs or other problems with the
merge-request.

The conditions for a contributor to be nominated Developer are the following:

- Make substantial contributions over a long period and show a certain
  understanding of the GStreamer development culture along with a certain
  technical skill level.
- Commitment to the project that goes "above and beyond" what is required for their everyday work.
- Interact with more than one project peer.
- Show good judgment and effective collaboration.

Potential Developers are expected to go the extra mile and contribute to the
maintenance effort, which means for example handling third party bug reports and
MRs, not just reviewing the MRs of one's colleagues. GStreamer developers should
be people who have demonstrated more than just contributions made in the
course of their work.

Significant contributors to testing, bug management, web site content,
documentation, project infrastructure and other non-code areas may also be
nominated.

All developer nominations require the support of at least three developers,
with people affiliated to at least 2 different companies.

## Removing Developer Status

The list of developers and maintainers will be reviewed regularly to ensure
it is up-to-date.

As such developer/maintainer status should be removed from members who are no
longer sufficiently active in the project and have not pushed a single commit in
the project in the last 24 months. In such situation their gitlab role would be
downgraded to Reporter. Returning contributors can be re-nominated for the Developer
role again when they comply with the guidelines listed in the previous section.

People come and go and move on to other things, so this is just routine project
hygiene and in no way a commentary on their expertise, judgment or appreciation
for previous work done.
