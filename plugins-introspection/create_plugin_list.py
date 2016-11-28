#!/usr/bin/python3
#
# Copyright (C) 2016 Tim-Philipp Müller
#
# License: this script is made available under the following licenses:
# Simplified BSD License/FreeBSD License, MIT License, and/or LGPLv2.1+ license.
#
# Reads plugin introspection .xml files (one per module) and outputs a
# list of all plugins in markdown format. We may want to do something
# more fancy here in future, for now this is just to provide a replacement
# for the xltsproc-generated list in the www module
#
# XML files were generated like this (we'll want to automate that at some point):
#
# echo '<plugins module_name="gstreamer">' > core.xml; \
#   cat ~/gst/1.10/gstreamer/docs/plugins/inspect/plugin-* >> core.xml; \
#   echo '</plugins>' >> core.xml
#
# for m in base good ugly bad; do \
#   echo '<plugins module_name="gst-plugins-'$m'">' > $m.xml; \
#   cat ~/gst/1.10/gst-plugins-$m/docs/plugins/inspect/plugin-* >> $m.xml; \
#   echo '</plugins>' >> $m.xml; \
# done

import xml.etree.ElementTree as xmltree
import http.client as httpclient
import urllib.parse as urlparse
import pickle
import sys
import os

allow_offline = False
is_offline = False

modules = {}

all_elements = {}
all_plugins = {}

url_cache = {}
url_conn = None

# check if an url exists or not
def check_url(url):
  global url_conn

  if is_offline and allow_offline:
    return True

  if url in url_cache:
    return url_cache[url]

  r = urlparse.urlparse(url)

  # Reuse existing conn if possible
  is_gst_fdo = url.startswith('https://gstreamer.freedesktop.org')
  if is_gst_fdo and url_conn is not None:
    connection = url_conn
  else:
    connection = httpclient.HTTPSConnection(r.netloc)
    if is_gst_fdo:
      url_conn = connection

  connection.request('HEAD', r.path)
  response = connection.getresponse()
  response.read()
  exists = response.status < 400
  url_cache[url] = exists
  sys.stderr.write('Checking if {0} exists: {1}\n'.format(url,exists))
  return exists

# load url cache
def load_url_cache():
  global url_cache
  try:
    with open('cache/url_cache.pkl', 'rb') as f:
      url_cache = pickle.load(f)
  except:
    url_cache = {}

# save url cache
def save_url_cache():
  global url_cache
  if not os.path.exists('cache'):
    os.makedirs('cache')
  with open('cache/url_cache.pkl', 'wb') as f:
    pickle.dump(url_cache, f)

def parse_plugin_element(element_node):
  element = {}
  for detail in element_node:
    if detail.tag in ['pads']:
      pass
    elif detail.text:
      element[detail.tag] = detail.text

  return element

def parse_plugin_elements(elements_node):
  element_list = []

  for enode in elements_node:
    element = parse_plugin_element(enode)
    element_list.append(element)

  return element_list

def parse_plugin(plugin_node):
  plugin = {}

  for detail in plugin_node:
    if detail.tag == 'elements':
      plugin['elements'] = parse_plugin_elements(detail)
    elif detail.text:
      plugin[detail.tag] = detail.text

  return plugin

def parse_plugins(xml_fn):
  plugin_list = []

  tree = xmltree.parse(xml_fn)
  root = tree.getroot()
  for pnode in root:
    plugin = parse_plugin(pnode)
    plugin_list.append(plugin)

  return plugin_list

def process_plugins(short_name):
  module = {}
  elements = []
  plugins = parse_plugins(short_name + '.xml')
  module['plugins'] = plugins
  for p in plugins:
    p['module_name'] = short_name
    elist = p['elements']
    for e in elist:
      e['plugin_name'] = p['name']
      e['module_name'] = short_name
      elements.append(e)

      name = e['name']
      if name in all_elements:
        print('Element {0} already exists in element list! {1}'.format(name, all_elements[name]))
      else:
        all_elements[name] = e

    if p['name'] in all_plugins:
      print('Plugin {0} already exists in plugin list! {1}'.format(name, all_plugins[p['name']]))
    else:
      all_plugins[p['name']] = p

  module['elements'] = elements

  modules[short_name] = module

# __main__

load_url_cache()

if not check_url('https://www.google.com'):
  if not allow_offline:
    raise Exception('URL checking does not seem to work, are you online?')
  else:
    print ('URL checking does not seem to work, continuing in offline mode')
    is_offline = True

for m in ['core', 'base', 'good', 'bad', 'ugly']:
  process_plugins(m)

plugins_page = '''# List of Elements and Plugins

<!-- WARNING: This page is generated! Any modifications will be overwritten -->

Note: this list is not complete! It does not contain OS-specific plugins
for Android, Windows, macOS, iOS, or wrapper plugins (gst-libav, gst-omx),
nor gst-rtsp-server or gstreamer-vaapi elements.

There may be links to pages that don't exist, this means that the element or
plugin does not have documentation yet or the documentation is not hooked up
properly (help welcome!).

| Element | Description | Plugin  | Module |
|---------|-------------|---------|--------|
'''

plugin_names = []

element_links = []
plugin_links = []

element_names = []
for e in all_elements:
  element_names.append(e)

element_names.sort()
for e in element_names:
  element_name = e
  element_desc = all_elements[e]['description']
  plugin_name = all_elements[e]['plugin_name']
  module_nick = all_elements[e]['module_name']
  if module_nick == 'core':
    module_name = 'gstreamer'
  else:
    module_name = 'gst-plugins-' + module_nick

  # column 1: element name
  edoc_url = 'https://gstreamer.freedesktop.org/data/doc/gstreamer/head/' + \
    '{1}-plugins/html/{1}-plugins-{0}.html'.format(element_name, module_name)

  if check_url(edoc_url):
    element_links.append('[element-{0}]: {1}\n'.format(element_name, edoc_url))
    plugins_page += '|[{0}][element-{0}]'.format(element_name)
  else:
    plugins_page += '|{0}'.format(element_name)

  # column 2: element description

  element_desc.replace('\n', ' ')
  element_desc.replace('|', ' ')
  if len(element_desc) > 150:
    element_desc = '(too long)'
  plugins_page += '|{0}'.format(element_desc)

  # column 3: plugin name
  pdoc_url = 'https://gstreamer.freedesktop.org/data/doc/gstreamer/head/' + \
    '{1}-plugins/html/{1}-plugins-plugin-{0}.html'.format(plugin_name,module_name)

  if check_url(pdoc_url):
    plugins_page += '|[{0}][{0}]'.format(plugin_name)
  else:
    plugins_page += '|{0}'.format(plugin_name)

  if plugin_name not in plugin_names:
    plugin_names.append(plugin_name)
    if check_url(pdoc_url):
      plugin_links.append('[{0}]: {1}\n'.format(plugin_name,pdoc_url))

  # column 4: module name
  plugins_page += '|[{0}][{1}]'.format(module_name, module_nick)

  # end of row
  plugins_page += '|\n'

# Write module shortcuts list

plugins_page += '''

[core]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-plugins/html/
[base]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/
[good]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/
[ugly]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-ugly-plugins/html/
[bad]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-plugins/html/

'''

# Write element shortcuts list

element_links.sort()
for elink in element_links:
  plugins_page += elink

plugins_page += '\n'

# Write plugin shortcuts list

plugin_links.sort()
for plink in plugin_links:
  plugins_page += plink

# Output page to stdout
print(plugins_page)

save_url_cache()
