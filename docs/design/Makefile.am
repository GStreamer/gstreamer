
EXTRA_DIST = \
	draft-klass.txt \
	draft-metadata.txt \
	draft-push-pull.txt \
	draft-tagreading.txt \
	part-activation.txt \
	part-buffering.txt \
	part-bufferpool.txt \
	part-buffer.txt \
	part-caps.txt \
	part-clocks.txt \
	part-context.txt \
	part-controller.txt \
	part-conventions.txt \
	part-dynamic.txt \
	part-element-sink.txt \
	part-element-source.txt \
	part-element-transform.txt \
	part-events.txt \
	part-framestep.txt \
	part-gstbin.txt \
	part-gstbus.txt \
	part-gstelement.txt \
	part-gstghostpad.txt \
	part-gstobject.txt \
	part-gstpipeline.txt \
	part-latency.txt \
	part-live-source.txt \
	part-memory.txt \
	part-messages.txt \
	part-meta.txt \
	part-miniobject.txt \
	part-missing-plugins.txt \
	part-MT-refcounting.txt \
	part-negotiation.txt \
	part-overview.txt \
	part-preroll.txt \
	part-probes.txt \
	part-progress.txt \
	part-push-pull.txt \
	part-qos.txt \
	part-query.txt \
	part-relations.txt \
	part-scheduling.txt \
	part-seeking.txt \
	part-segments.txt \
	part-seqnums.txt \
	part-sparsestreams.txt \
	part-standards.txt \
	part-states.txt \
	part-stream-status.txt \
	part-streams.txt \
	part-synchronisation.txt \
	part-toc.txt \
	part-TODO.txt \
	part-tracing.txt \
	part-trickmodes.txt

CLEANFILES = index.html index.txt

html:
	if ! test -z `which asciidoc`; then \
	  echo >index.txt  "GStreamer design"; \
	  echo >>index.txt "================"; \
	  echo >>index.txt "The Gstreamer developers"; \
	  echo >>index.txt "Version $(PACKAGE_VERSION)"; \
	  echo >>index.txt ""; \
	  ( cd $(srcdir) && \
	  cat >>$(abs_builddir)/index.txt $(EXTRA_DIST) ); \
	  asciidoc -o index.html index.txt; \
	else \
	  echo "need asciidoc to generate html"; \
        fi;

upload:
	@echo nothing to upload

