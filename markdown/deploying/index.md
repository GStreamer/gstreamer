---
short-description: Deploy GStreamer with your application
...

# Deploying your application

Once the development of your application is finished, you will need to
deploy it to the target machine, usually in the form of a package or
installer. You have several options here, and, even though this subject
is not really in the scope of this documentation, we will give some
hints to try to help you.

## Multiplatform vs. single-platform packaging system

The first choice you need to make is whether you want to deploy your
application to more than one platform. If yes, then you have the choice
to use a different packaging system for each platform, or use one that
can deliver to all platforms simultaneously. This table summarizes the
pros and cons of each option.

<table>
<colgroup>
<col width="33%" />
<col width="33%" />
<col width="33%" />
</colgroup>
<thead>
<tr class="header">
<th> </th>
<th>Pros</th>
<th>Cons</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><strong>Multiplatform packaging system</strong></p>
<p>The same system is used to package your application for all platforms</p></td>
<td><ul>
<li><p>You only need to develop your packaging system once, and it works for all supported platforms.</p></li>
</ul></td>
<td><ul>
<li>On some platforms, the packaging system might impose artificial restrictions inherited from the other platforms.</li>
</ul></td>
</tr>
<tr class="even">
<td><p><strong>Single-platform packaging system</strong></p>
<p>Your application is packaged using a different system on each platform.</p></td>
<td><ul>
<li><p>You can make use of all the advantages each packaging system can offer.</p>
</li>
</ul></td>
<td><ul>
<li><p>You need to develop a new packaging system for each supported platform.</p></li>
</ul></td>
</tr>
</tbody>
</table>

GStreamer itself supports many different platforms (Linux, iOS, Android, Mac
OS X, Windows, etc) and has been built using a multiplatform packaging
system named **Cerbero**, which is available for you to use, should you
choose to go down this route.

## Shared vs. private GStreamer deployment

You can install GStreamer in the target machine in the same way
you installed it in your development machine, you can deploy it
privately, or you can even customize it before deploying. Here you have
a few options:

<table>
<colgroup>
<col width="33%" />
<col width="33%" />
<col width="33%" />
</colgroup>
<thead>
<tr class="header">
<th></th>
<th>Pros</th>
<th>Cons</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p><strong>Shared GStreamer</strong></p>
<p>GStreamer is installed independently of your application, as a prerequisite, in a common place in the target computer (<code>C:\Program Files</code>, for example). You application uses an environment variable to locate it.</p></td>
<td><ul>
<li>If more than one application in the target computer uses GStreamer, it is installed only once and shared, reducing disk usage.</li>
</ul></td>
<td><ul>
<li>Tampering or corruption of the shared GStreamer installation can make your application fail.</li>
</ul></td>
</tr>
<tr class="even">
<td><p><strong>Private GStreamer with dynamic linking</strong></p>
<p>Your application deploys GStreamer to a private folder.</p></td>
<td><ul>
<li>Your GStreamer is independent of other applications, so it does not get corrupted if other applications mess with their installations.</li>
</ul></td>
<td><ul>
<li>If multiple applications in the target computer use GStreamer, it won’t be shared, consuming more disk space.</li>
</ul></td>
</tr>
<tr class="odd">
<td><p><strong>Private GStreamer with static linking</strong></p>
<p>Your application links statically against GStreamer, so it effectively becomes part of your application binary.</p></td>
<td><ul>
<li>Your GStreamer is independent of other applications, so it does not get corrupted if other applications mess with their installations.</li>
<li>Deployment for ordinary users is easier as you have fewer files.</li>
<li>This is your only choice on iOS.</li>
</ul></td>
<td><ul>
<li>If multiple applications in the target computer use GStreamer, it won’t be shared, consuming more disk space.</li>
<li>You need to provide the required files for your users to re-link your application against a modified GStreamer as required by the license.</li>
</ul></td>
</tr>
</tbody>
</table>

The following pages give further directions for some of the above
options.

  - Platform-specific packaging methods:
      - For [Mac OS X](deploying/mac-osx.md)
      - For [Windows](deploying/windows.md)
  - [Multiplatform deployment using
    Cerbero](deploying/multiplatform-using-cerbero.md)
