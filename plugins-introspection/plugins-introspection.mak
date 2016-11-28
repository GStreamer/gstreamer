update-plugin-list:
	@cd plugins-introspection; \
	./create_plugin_list.py > ../markdown/plugins.md ; \
	cd ..

CHECKOUT_DIR = $(top_builddir)/plugins-introspection/cache

checkoutdir:
	@mkdir -p $(CHECKOUT_DIR) || { \
	  echo "Failed to create $(CHECKOUT_DIR)"; \
	  exit 1; \
	}

update-xml: checkoutdir
	@for m in gstreamer gst-plugins-base gst-plugins-good gst-plugins-ugly gst-plugins-bad; do \
	  moduledir=$(CHECKOUT_DIR)/$$m; \
	  branch=$(BRANCH); \
	  if [ -d $$moduledir ] ; then \
	    cd $$moduledir; \
	    echo "$$m checkout: cleaning"; \
	    git fetch --tags; \
	    git clean --dry-run -f -d -x || { \
	      echo "Cleaning $$m checkout failed"; exit 1; \
	    } ; \
	    echo "$$m checkout: updating..."; \
	    git checkout -q $$branch && git pull -q || { \
	      echo "Updating $$m checkout failed"; exit 1; \
	    } ; \
	  else \
	    echo "$$m checkout: creating (cloning)"; \
	    git clone -b $$branch https://anongit.freedesktop.org/git/gstreamer/$$m.git $$moduledir || { \
	      echo "git clone failed"; exit 1; \
	    } ; \
	  cd ../../ ; \
	  fi; \
	done
	@cd $(CHECKOUT_DIR)/gstreamer; \
	  echo '<plugins module_name="gstreamer">' > ../../core.xml; \
	  git ls-files docs/plugins/inspect/ | sort | xargs cat >> ../../core.xml; \
	  echo '</plugins>' >> ../../core.xml
	@for m in base good ugly bad; do \
	  cd $(CHECKOUT_DIR)/gst-plugins-$$m; \
	  echo '<plugins module_name="gst-plugins-'$$m'">' > ../../$$m.xml; \
	  git ls-files docs/plugins/inspect/ | sort | xargs cat >> ../../$$m.xml; \
	  echo '</plugins>' >> ../../$$m.xml; \
	done


foo:
	@for m in base good ugly bad; do \
	  echo '<plugins module_name="gst-plugins-'$$m'">' > plugins-introspection/$$m.xml; \
	  cat $(CHECKOUT_DIR)/gst-plugins-$$m/docs/plugins/inspect/plugin-* >> plugins-introspection/$$m.xml; \
	  echo '</plugins>' >> plugins-introspection/$$m.xml; \
	done

clean-url-cache:
	@rm -f plugins-introspection/cache/url_cache.pkl
