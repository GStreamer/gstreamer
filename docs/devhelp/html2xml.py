#!/usr/bin/python2
import formatter
import htmllib
import os
import os.path
import sgmllib
import string
import sys

base_dict = {
  'glib'           : 'http://developer.gnome.org/doc/API/glib/',
  'gdk'            : 'http://developer.gnome.org/doc/API/gdk/',
  'gtk'            : 'http://developer.gnome.org/doc/API/gtk/',
  'gdk-pixbuf'     : 'http://developer.gnome.org/doc/API/gdk-pixbuf/',
  'glib-2.0'       : 'http://developer.gnome.org/doc/API/2.0/glib/',
  'gobject-2.0'    : 'http://developer.gnome.org/doc/API/2.0/gobject/',
  'atk'            : 'http://developer.gnome.org/doc/API/2.0/atk/',
  'pango'          : 'http://developer.gnome.org/doc/API/2.0/pango/',
  'gdk-2.0'        : 'http://developer.gnome.org/doc/API/2.0/gdk/',
  'gtk-2.0'        : 'http://developer.gnome.org/doc/API/2.0/gtk/',
  'gdk-pixbuf-2.0' : 'http://developer.gnome.org/doc/API/2.0/gdk-pixbuf/',
  'gnome-vfs-2.0'  : 'http://developer.gnome.org/doc/API/2.0/gnome-vfs/',
  'libxml-2'       : 'http://xmlsoft.org/html/',
  'libgnome'       : 'http://developer.gnome.org/doc/API/2.0/libgnome/',
  'libgnomeui'     : 'http://developer.gnome.org/doc/API/2.0/libgnomeui/',  
}

def does_dict_have_keys (dict, keys):
    for key in keys:
	if not dict.has_key (key):
	    return 0
    if len(dict) != len(keys):
	return 0
    return 1

class BookParser (sgmllib.SGMLParser):
    def __init__ (self):
	sgmllib.SGMLParser.__init__ (self)
	self.a = self.parents = []
	self.dict = {}
	self.last = self.link = ""
	self.is_a = self.level = 0
	self.first = 1

    def unknown_starttag (self, tag, attrs):
	if tag == 'a':
	    self.is_a = 1
	    for attr in attrs:
		if attr[0] == "href":
		    self.link = attr[1]
		    break
		
	if tag in ['dd', 'ul']:
	    self.parents.append (self.last)
	    self.level = self.level + 1
	
    def unknown_endtag (self, tag):
	if tag == 'a':
	    self.is_a = 0
	    
	if tag in ['dd', 'ul']:
	    self.level = self.level - 1
	    self.parents.pop()
	
    def handle_data (self, data):
	data = string.strip (data)
	if not data or data in [ ">", "<" ]:
	    return
	
	if self.first:
	    self.dict['name'] = data
	    self.first = 0
	    return
	    
	if data == self.dict['name'] or data in [ "Next Page", "Previous Page", "Home", "Next"]:
	    return
	
	if len (self.parents) == 0:
	    dict = self.dict
	elif len (self.parents) == 1:
	    dict = self.dict[self.parents[0]]
	elif len (self.parents) == 2:
	    dict = self.dict[self.parents[0]][self.parents[1]]
	elif len (self.parents) == 3:
	    dict = self.dict[self.parents[0]][self.parents[1]][self.parents[2]]
	else:
	    dict = None
	    
	if self.is_a:
	    if dict == None:
		return
	    
	    if not dict.has_key (data):
		dict[data] = {}		    
	    if not dict.has_key ('order'):
		dict['order'] = []
	    dict['order'].append (data)
	    dict[data]['link'] = self.link
	    
	    self.last = data

class FunctionParser (htmllib.HTMLParser):
    SKIP_DATA = [ "Next Page", "Previous Page", "Home", "Next", "Up", "<", ">", "Figure 1", "[1]"]
    is_a = 0
    a = []
    link = ""
    def anchor_bgn (self, href, name, type):
        self.is_a = 1
        self.link = href

    def anchor_end (self):
        self.is_a = 0

    def handle_data (self, data):
        data = string.strip (data)
        if data in self.SKIP_DATA:
            return

        if not '#' in self.link:
            return

        if self.link[:2] == "..":
            return
            
        if self.is_a and self.link:
            self.a.append ((data, self.link))

def parse_file_for_functions (fd):
    try:
	p = FunctionParser (formatter.NullFormatter ())
	p.feed (fd.read ())
	p.close ()
    except KeyboardInterrupt:
	raise SystemExit
    return p.a

def output_chapter (fd, main_dict, level=1):
    if not main_dict.has_key ('order'):
        return
    
    for sub in main_dict['order']:
        dict = main_dict[sub]
            
        if not does_dict_have_keys (dict, ['link']):
            fd.write (' '*(level*4))
            fd.write ('<sub name="%s" link="%s">\n' % (sub, dict['link']))
            output_chapter (fd, dict, level+1)
            fd.write (' '*(level*4))
            fd.write ('</sub>\n')
        else:
            fd.write (' '*(level*4))            
            fd.write ('<sub name="%s" link="%s"/>\n' % (sub, dict['link']))    
    
def get_functions (dir):
    function_list = []
    
    for file in os.listdir (dirname):
        if file[-5:] != ".html":
            continue
        
        fd = open (dirname + os.sep + file)
        functions = parse_file_for_functions (fd)

        for function in functions:
            if not function in function_list:
                function_list.append (function)
    return function_list

def get_index_filename (dir):
    dir = os.path.abspath (dir)

    filename = dir + os.sep + 'index.html'
    if os.path.exists (filename):
        return filename

    filename = dir + os.sep + 'book1.html'
    if os.path.exists (filename):
        return filename
    else:
        raise SystemExit, "Can't find an index file"

def output_xml (fd, filename, bookname, version):
    global base_dict
    
    templ = \
    '<book title="%(title)s"\n'   \
    '      name="%(name)s"\n%(version)s'     \
    '      base="%(base)s"\n'     \
    '      link="%(link)s">\n\n'

	
    link = os.path.basename (filename)

    input_fd = open (filename)
    p = BookParser ()
    p.feed (input_fd.read ())
    p.close ()

    fd.write ('<?xml version="1.0"?>\n')
    
    full = book_full_name (bookname, version)
    if base_dict.has_key (full):
	base = base_dict[full]
    else:
	base = ""

    if version:
	version =     '      version="%s"\n' % version
    
    fd.write (templ % { 'title'   : p.dict['name'],
                        'name'    : bookname,
		        'version' : version,
		        'base'    : base,
		        'link'    : link })
    
    fd.write ('<chapters>\n')
    output_chapter (fd, p.dict)
    fd.write ('</chapters>\n\n')
    
    fd.flush ()
    
def output_functions (fd, dirname):
    functions = get_functions (dirname)
    functions.sort ()
    
    fd.write ('<functions>\n')
    for name, link in functions:
        fd.write ('    <function name="%s" link="%s"/>\n' % (name, link))
    fd.write ('</functions>\n\n')    

def dirname_to_bookname (dirname):
    dirname = os.path.basename (dirname)
    if string.find (dirname, '-') == -1:
	return (dirname, "")
    else:
	pos = string.rfind (dirname, '-')
	return (dirname[:pos], dirname[pos+1:])

def book_full_name (book, version):
    if version:
	return "%s-%s" % (book, version)
    else:
	return book
    
if __name__ == "__main__":
    if len (sys.argv) != 2 and \
       len (sys.argv) != 3:
	print 'Usage: %s dirname [output]' % sys.argv[0]
	raise SystemExit
    
    index_filename = get_index_filename (sys.argv[1])
    dirname = os.path.abspath (sys.argv[1])
    book, version = dirname_to_bookname (sys.argv[1])
    if len (sys.argv) != 2:
	if sys.argv[2] == "-s":
	    fd = sys.stdout
	else:
	    output_filename = sys.argv[2]
	    fd = open (output_filename, 'w')
    else:
	output_filename = book_full_name (book, version) + '.devhelp'
	fd = open (output_filename, 'w')
	
    output_xml (fd, index_filename, book, version)
    output_functions (fd, dirname)
    fd.write ('</book>\n')
    fd.close ()
            

