fig_pngs= \
	  bin-element.png \
	  connected-elements.png \
	  filter-element-multi.png \
	  filter-element.png \
	  hello-world.png \
	  mime-world.png \
	  queue.png \
	  sink-element.png \
	  src-element.png \
	  state-diagram.png \
	  thread.png

fig_epss= \
	  bin-element.eps \
	  connected-elements.eps \
	  filter-element-multi.eps \
	  filter-element.eps \
	  hello-world.eps \
	  mime-world.eps \
	  queue.eps \
	  sink-element.eps \
	  src-element.eps \
	  state-diagram.eps \
	  thread.eps

png_images: $(fig_pngs)
eps_images: $(fig_epss)

%.png : ../fig/%.fig
	fig2dev -L png -s 16 $< $@

%.eps : ../fig/%.fig
	fig2dev -L eps -s 16 $< $@


all-local: png_images eps_images

