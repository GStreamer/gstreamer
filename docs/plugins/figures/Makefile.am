SOURCES=camerabin.dot
IMAGES =camerabin.png

EXTRA_DIST=$(SOURCES)

.PHONY: update

.dot.png:
	dot -Tpng $< -o$@

update: $(IMAGES)

