# this snippet is to be included by both our docbook manuals
# and gtk-doc API references

# it adds an upload target to each of these dir's Makefiles

# each Makefile.am should define the following variables:
# - DOC: the base name of the documentation
#        (faq, manual, pwg, gstreamer, gstreamer-libs)
# - FORMATS: the formats in which DOC is output
#            (html ps pdf)

# if you want to use it, make sure your ..sh/config file contains the
# correct User entry for the Host entry for the DOC_SERVER

# these variables define the location of the online docs
DOC_SERVER=freedesktop.org
DOC_BASE=/home/projects/gstreamer/www/data/doc
DOC_URL=$(DOC_SERVER):$(DOC_BASE)


upload: $(FORMATS)
	@if test "x$$GST_PLUGINS_VERSION_NANO" = x0; then \
            export DOCVERSION=$(VERSION); \
        else export DOCVERSION=head; \
        fi; \
        export DIR=$(DOC_BASE)/gstreamer/$$DOCVERSION/$(DOC); \
	ssh $(DOC_SERVER) mkdir -p $$DIR; \
	if echo $(FORMATS) | grep html > /dev/null; then export SRC="$$SRC html"; fi; \
	if echo $(FORMATS) | grep ps > /dev/null; then export SRC="$$SRC $(DOC).ps"; fi; \
	if echo $(FORMATS) | grep pdf > /dev/null; then export SRC="$$SRC $(DOC).pdf"; fi; \
	echo Uploading $$SRC to $(DOC_SERVER):$$DIR; \
	rsync -rv -e ssh --delete $$SRC $(DOC_SERVER):$$DIR; \
	ssh $(DOC_SERVER) chmod -R g+w $$DIR; \
	echo Done
