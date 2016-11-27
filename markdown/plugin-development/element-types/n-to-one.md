---
title: Writing a N-to-1 Element or Muxer
...

# Writing a N-to-1 Element or Muxer

N-to-1 elements have been previously mentioned and discussed in both
[Request and Sometimes pads][request-pads] and in
[Different scheduling modes][scheduling]. The main noteworthy
thing about N-to-1 elements is that each pad is push-based in its own
thread, and the N-to-1 element synchronizes those streams by
expected-timestamp-based logic. This means it lets all streams wait
except for the one that provides the earliest next-expected timestamp.
When that stream has passed one buffer, the next
earliest-expected-timestamp is calculated, and we start back where we
were, until all streams have reached EOS. There is a helper base class,
called `GstCollectPads`, that will help you to do this.

Note, however, that this helper class will only help you with grabbing a
buffer from each input and giving you the one with earliest timestamp.
If you need anything more difficult, such as "don't-grab-a-new-buffer
until a given timestamp" or something like that, you'll need to do this
yourself.

[request-pads]: plugin-development/advanced/request.md
[scheduling]: plugin-development/advanced/scheduling.md
