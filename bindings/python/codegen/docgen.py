#!/usr/bin/env python
import getopt
import os
import re
import sys

import definitions
import defsparser
import docextract
import override


class Node:

    def __init__(self, name, interfaces=[]):
        self.name = name
        self.interfaces = interfaces
        self.subclasses = []

    def add_child(self, node):
        self.subclasses.append(node)


def build_object_tree(parser):
    # reorder objects so that parent classes come first ...
    objects = parser.objects[:]
    pos = 0
    while pos < len(objects):
        parent = objects[pos].parent
        for i in range(pos+1, len(objects)):
            if objects[i].c_name == parent:
                objects.insert(i+1, objects[pos])
                del objects[pos]
                break
        else:
            pos = pos + 1

    root = Node(None)
    nodes = {None: root}
    for obj_def in objects:
        parent_name = obj_def.parent
        if parent_name == 'GObject':
            parent_name = None
        parent_node = nodes[parent_name]
        node = Node(obj_def.c_name, obj_def.implements)
        parent_node.add_child(node)
        nodes[node.name] = node

    if parser.interfaces:
        interfaces = Node('gobject.GInterface')
        root.add_child(interfaces)
        nodes[interfaces.name] = interfaces
        for obj_def in parser.interfaces:
            node = Node(obj_def.c_name)
            interfaces.add_child(node)
            nodes[node.name] = node

    if parser.boxes:
        boxed = Node('gobject.GBoxed')
        root.add_child(boxed)
        nodes[boxed.name] = boxed
        for obj_def in parser.boxes:
            node = Node(obj_def.c_name)
            boxed.add_child(node)
            nodes[node.name] = node

    if parser.pointers:
        pointers = Node('gobject.GPointer')
        root.add_child(pointers)
        nodes[pointers.name] = pointers
        for obj_def in parser.pointers:
            node = Node(obj_def.c_name)
            pointers.add_child(node)
            nodes[node.name] = node

    return root


class DocWriter:

    def __init__(self):
        self._fp = None
        # parse the defs file
        self.parser = defsparser.DefsParser(())
        self.overrides = override.Overrides()
        self.classmap = {}
        self.docs = {}

    def add_sourcedirs(self, source_dirs):
        self.docs = docextract.extract(source_dirs, self.docs)

    def add_tmpldirs(self, tmpl_dirs):
        self.docs = docextract.extract_tmpl(tmpl_dirs, self.docs)

    def add_docs(self, defs_file, overrides_file, module_name):
        '''parse information about a given defs file'''
        self.parser.filename = defs_file
        self.parser.startParsing(defs_file)
        if overrides_file:
            self.overrides.handle_file(overrides_file)

        for obj in (self.parser.objects + self.parser.interfaces +
                    self.parser.boxes + self.parser.pointers):
            if not obj.c_name in self.classmap:
                self.classmap[obj.c_name] = '%s.%s' % (
                    module_name, obj.name)

    def pyname(self, name):
        return self.classmap.get(name, name)

    def _compare(self, obja, objb):
        return cmp(self.pyname(obja.c_name), self.pyname(objb.c_name))

    def output_docs(self, output_prefix):
        files = {}

        # class hierarchy
        hierarchy = build_object_tree(self.parser)
        filename = self.create_filename('hierarchy', output_prefix)
        self._fp = open(filename, 'w')
        self.write_full_hierarchy(hierarchy)
        self._fp.close()

        obj_defs = (self.parser.objects + self.parser.interfaces +
                    self.parser.boxes + self.parser.pointers)
        obj_defs.sort(self._compare)

        for obj_def in obj_defs:
            filename = self.create_filename(obj_def.c_name, output_prefix)
            self._fp = open(filename, 'w')
            if isinstance(obj_def, definitions.ObjectDef):
                self.output_object_docs(obj_def)
            elif isinstance(obj_def, definitions.InterfaceDef):
                self.output_interface_docs(obj_def)
            elif isinstance(obj_def, definitions.BoxedDef):
                self.output_boxed_docs(obj_def)
            elif isinstance(obj_def, definitions.PointerDef):
                self.output_boxed_docs(obj_def)
            self._fp.close()
            files[os.path.basename(filename)] = obj_def

        if not files:
            return

        output_filename = self.create_toc_filename(output_prefix)
        self._fp = open(output_filename, 'w')
        self.output_toc(files)
        self._fp.close()

    def output_object_docs(self, obj_def):
        self.write_class_header(obj_def.c_name)

        self.write_heading('Synopsis')
        self.write_synopsis(obj_def)
        self.close_section()

        # construct the inheritence hierarchy ...
        ancestry = [(obj_def.c_name, obj_def.implements)]
        try:
            parent = obj_def.parent
            while parent != None:
                if parent == 'GObject':
                    ancestry.append(('GObject', []))
                    parent = None
                else:
                    parent_def = self.parser.find_object(parent)
                    ancestry.append((parent_def.c_name, parent_def.implements))
                    parent = parent_def.parent
        except ValueError:
            pass
        ancestry.reverse()
        self.write_heading('Ancestry')
        self.write_hierarchy(obj_def.c_name, ancestry)
        self.close_section()

        constructor = self.parser.find_constructor(obj_def, self.overrides)
        if constructor:
            self.write_heading('Constructor')
            self.write_constructor(constructor,
                                   self.docs.get(constructor.c_name, None))
            self.close_section()

        methods = self.parser.find_methods(obj_def)
        methods = filter(lambda meth, self=self:
                         not self.overrides.is_ignored(meth.c_name), methods)
        if methods:
            self.write_heading('Methods')
            for method in methods:
                self.write_method(method, self.docs.get(method.c_name, None))
            self.close_section()

        self.write_class_footer(obj_def.c_name)

    def get_methods_for_object(self, obj_def):
        methods = []
        for method in self.parser.find_methods(obj_def):
            if not self.overrides.is_ignored(method.c_name):
                methods.append(method)
        return methods

    def output_interface_docs(self, int_def):
        self.write_class_header(int_def.c_name)

        self.write_heading('Synopsis')
        self.write_synopsis(int_def)
        self.close_section()

        methods = self.get_methods_for_object(int_def)
        if methods:
            self.write_heading('Methods')
            for method in methods:
                self.write_method(method, self.docs.get(method.c_name, None))
            self.close_section()
        self.write_class_footer(int_def.c_name)

    def output_boxed_docs(self, box_def):
        self.write_class_header(box_def.c_name)

        self.write_heading('Synopsis')
        self.write_synopsis(box_def)
        self.close_section()

        constructor = self.parser.find_constructor(box_def, self.overrides)
        if constructor:
            self.write_heading('Constructor')
            self.write_constructor(constructor,
                                   self.docs.get(constructor.c_name, None))
            self.close_section()

        methods = self.get_methods_for_object(box_def)
        if methods:
            self.write_heading('Methods')
            for method in methods:
                self.write_method(method, self.docs.get(method.c_name, None))
            self.close_section()

        self.write_class_footer(box_def.c_name)

    def output_toc(self, files):
        self._fp.write('TOC\n\n')
        for filename in sorted(files):
            obj_def = files[filename]
            self._fp.write(obj_def.c_name + ' - ' + filename + '\n')

    # override the following to create a more complex output format

    def create_filename(self, obj_name, output_prefix):
        '''Create output filename for this particular object'''
        return output_prefix + '-' + obj_name.lower() + '.txt'

    def create_toc_filename(self, output_prefix):
        return self.create_filename(self, 'docs', output_prefix)

    def write_full_hierarchy(self, hierarchy):

        def handle_node(node, indent=''):
            for child in node.subclasses:
                self._fp.write(indent + node.name)
                if node.interfaces:
                    self._fp.write(' (implements ')
                    self._fp.write(', '.join(node.interfaces))
                    self._fp.write(')\n')
                else:
                    self._fp.write('\n')
                handle_node(child, indent + '  ')
        handle_node(hierarchy)

    def serialize_params(self, func_def):
        params = []
        for param in func_def.params:
            params.append(param[1])
        return ', '.join(params)

    # these need to handle default args ...

    def create_constructor_prototype(self, func_def):
        return '%s(%s)' % (func_def.is_constructor_of,
                           self.serialize_params(func_def))

    def create_function_prototype(self, func_def):
        return '%s(%s)' % (func_def.name,
                           self.serialize_params(func_def))

    def create_method_prototype(self, meth_def):
        return '%s.%s(%s)' % (meth_def.of_object,
                              meth_def.name,
                              self.serialize_params(meth_def))

    def write_class_header(self, obj_name):
        self._fp.write('Class %s\n' % obj_name)
        self._fp.write('======%s\n\n' % ('=' * len(obj_name)))

    def write_class_footer(self, obj_name):
        pass

    def write_heading(self, text):
        self._fp.write('\n' + text + '\n' + ('-' * len(text)) + '\n')

    def close_section(self):
        pass

    def write_synopsis(self, obj_def):
        self._fp.write('class %s' % obj_def.c_name)
        if isinstance(obj_def, definitions.ObjectDef):
            bases = []
            if obj_def.parent:
                bases.append(obj_def.parent)
            bases = bases = obj_def.implements
            if bases:
                self._fp.write('(%s)' % ', '.join(bases, ))
        self._fp.write(':\n')

        constructor = self.parser.find_constructor(obj_def, self.overrides)
        if constructor:
            prototype = self.create_constructor_prototype(constructor)
            self._fp.write('    def %s\n' % prototype)

        for method in self.get_methods_for_object(obj_def):
            prototype = self.create_method_prototype(method)
            self._fp.write('    def %s\n' % prototype)

    def write_hierarchy(self, obj_name, ancestry):
        indent = ''
        for name, interfaces in ancestry:
            self._fp.write(indent + '+-- ' + name)
            if interfaces:
                self._fp.write(' (implements ')
                self._fp.write(', '.join(interfaces))
                self._fp.write(')\n')
            else:
                self._fp.write('\n')
            indent = indent + '  '
        self._fp.write('\n')

    def write_constructor(self, func_def, func_doc):
        prototype = self.create_constructor_prototype(func_def)
        self._fp.write(prototype + '\n\n')
        for type, name, dflt, null in func_def.params:
            self.write_parameter(name, func_doc)
        self.write_return_value(func_def, func_doc)
        if func_doc and func_doc.description:
            self._fp.write(func_doc.description)
        self._fp.write('\n\n\n')

    def write_method(self, meth_def, func_doc):
        prototype = self.create_method_prototype(meth_def)
        self._fp.write(prototype + '\n\n')
        for type, name, dflt, null in meth_def.params:
            self.write_parameter(name, func_doc)
        self.write_return_value(meth_def, func_doc)
        if func_doc and func_doc.description:
            self._fp.write('\n')
            self._fp.write(func_doc.description)
        self._fp.write('\n\n')

    def write_parameter(self, param_name, func_doc):
        if func_doc:
            descr = func_doc.get_param_description(param_name)
        else:
            descr = 'a ' + type
        self._fp.write('  ' + param_name + ': ' + descr + '\n')

    def write_return_value(self, meth_def, func_doc):
        if meth_def.ret and meth_def.ret != 'none':
            if func_doc and func_doc.ret:
                descr = func_doc.ret
            else:
                descr = 'a ' + meth_def.ret
            self._fp.write('  Returns: ' + descr + '\n')

CLASS_HEADER_TEMPLATE = """<refentry id="%(entryid)s">
  <refmeta>
    <refentrytitle>%(name)s</refentrytitle>
    <manvolnum>3</manvolnum>
    <refmiscinfo>%(miscinfo)s</refmiscinfo>
  </refmeta>

  <refnamediv>
    <refname>%(name)s</refname><refpurpose></refpurpose>
  </refnamediv>

"""
VARIABLE_TEMPLATE = """<varlistentry>
      <term><parameter>%(parameter)s</parameter>&nbsp;:</term>
      <listitem><simpara>%(description)s</simpara></listitem>
    </varlistentry>
"""

DOCBOOK_HEADER = """<?xml version="1.0" standalone="no"?>
<!DOCTYPE synopsis PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
    "http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd">
"""


class DocbookDocWriter(DocWriter):

    def __init__(self):
        DocWriter.__init__(self)

        self._function_pat = re.compile(r'(\w+)\s*\(\)')
        self._parameter_pat = re.compile(r'\@(\w+)')
        self._constant_pat = re.compile(r'\%(-?\w+)')
        self._symbol_pat = re.compile(r'#([\w-]+)')

        self._transtable = ['-'] * 256
        # make string -> reference translation func
        for digit in '0123456789':
            self._transtable[ord(digit)] = digit

        for letter in 'abcdefghijklmnopqrstuvwxyz':
            self._transtable[ord(letter)] = letter
            self._transtable[ord(letter.upper())] = letter
        self._transtable = ''.join(self._transtable)

    def create_filename(self, obj_name, output_prefix):
        '''Create output filename for this particular object'''
        stem = output_prefix + '-' + obj_name.lower()
        return stem + '.xml'

    def create_toc_filename(self, output_prefix):
        return self.create_filename('classes', output_prefix)

    def make_class_ref(self, obj_name):
        return 'class-' + obj_name.translate(self._transtable)

    def make_method_ref(self, meth_def):
        return 'method-%s--%s' % (
            meth_def.of_object.translate(self._transtable),
            meth_def.name.translate(self._transtable))

    def _format_function(self, match):
        info = self.parser.c_name.get(match.group(1), None)
        if info:
            if isinstance(info, defsparser.FunctionDef):
                return self._format_funcdef(info)
            if isinstance(info, defsparser.MethodDef):
                return self._format_method(info)

        # fall through through
        return '<function>%s()</function>' % (match.group(1), )

    def _format_funcdef(self, info):
        if info.is_constructor_of is not None:
            # should have a link here
            return '<methodname>%s()</methodname>' % (
                self.pyname(info.is_constructor_of), )
        else:
            return '<function>%s()</function>' % (info.name, )

    def _format_param(self, match):
        return '<parameter>%s</parameterdliteral>' % (match.group(1), )

    def _format_const(self, match):
        return '<literal>%s</literal>' % (match.group(1), )

    def _format_method(self, info):
        return ('<link linkend="%s">'
                '<methodname>%s.%s</methodname>'
                '</link>') % (self.make_method_ref(info),
                              self.pyname(info.of_object),
                              info.name)

    def _format_object(self, info):
        return ('<link linkend="%s">'
                '<classname>%s</classname>'
                '</link>') % (self.make_class_ref(info.c_name),
                              self.pyname(info.c_name))

    def _format_symbol(self, match):
        info = self.parser.c_name.get(match.group(1), None)
        if info:
            if isinstance(info, defsparser.FunctionDef):
                return self._format_funcdef(info)
            elif isinstance(info, defsparser.MethodDef):
                return self._format_method(info)
            elif isinstance(info, (defsparser.ObjectDef,
                                   defsparser.InterfaceDef,
                                   defsparser.BoxedDef,
                                   defsparser.PointerDef)):
                return self._format_object(info)

        # fall through through
        return '<literal>%s</literal>' % (match.group(1), )

    def reformat_text(self, text, singleline=0):
        # replace special strings ...
        text = self._function_pat.sub(self._format_function, text)
        text = self._parameter_pat.sub(self._format_param, text)
        text = self._constant_pat.sub(self._format_const, text)
        text = self._symbol_pat.sub(self._format_symbol, text)

        # don't bother with <para> expansion for single line text.
        if singleline:
            return text

        lines = text.strip().split('\n')
        for index in range(len(lines)):
            if lines[index].strip() == '':
                lines[index] = '</para>\n<para>'
                continue
        return '<para>%s</para>' % ('\n'.join(lines), )

    # write out hierarchy

    def write_full_hierarchy(self, hierarchy):

        def handle_node(node, indent=''):
            if node.name:
                self._fp.write('%s<link linkend="%s">%s</link>' %
                         (indent, self.make_class_ref(node.name),
                          self.pyname(node.name)))
                if node.interfaces:
                    self._fp.write(' (implements ')
                    for i in range(len(node.interfaces)):
                        self._fp.write('<link linkend="%s">%s</link>' %
                                 (self.make_class_ref(node.interfaces[i]),
                                  self.pyname(node.interfaces[i])))
                        if i != len(node.interfaces) - 1:
                            self._fp.write(', ')
                    self._fp.write(')\n')
                else:
                    self._fp.write('\n')

                indent = indent + '  '
            node.subclasses.sort(lambda a, b:
                                 cmp(self.pyname(a.name), self.pyname(b.name)))
            for child in node.subclasses:
                handle_node(child, indent)

        self._fp.write(DOCBOOK_HEADER)
        self._fp.write('<synopsis>')
        handle_node(hierarchy)
        self._fp.write('</synopsis>\n')

    # these need to handle default args ...

    def create_constructor_prototype(self, func_def):
        xml = ['<constructorsynopsis language="python">\n']
        xml.append('    <methodname>__init__</methodname>\n')
        for type, name, dflt, null in func_def.params:
            xml.append('    <methodparam><parameter>')
            xml.append(name)
            xml.append('</parameter>')
            if dflt:
                xml.append('<initializer>')
                xml.append(dflt)
                xml.append('</initializer>')
            xml.append('</methodparam>\n')
        if not func_def.params:
            xml.append('    <methodparam></methodparam>')
        xml.append('  </constructorsynopsis>')
        return ''.join(xml)

    def create_function_prototype(self, func_def):
        xml = ['<funcsynopsis language="python">\n    <funcprototype>\n']
        xml.append('      <funcdef><function>')
        xml.append(func_def.name)
        xml.append('</function></funcdef>\n')
        for type, name, dflt, null in func_def.params:
            xml.append('      <paramdef><parameter>')
            xml.append(name)
            xml.append('</parameter>')
            if dflt:
                xml.append('<initializer>')
                xml.append(dflt)
                xml.append('</initializer>')
            xml.append('</paramdef>\n')
        if not func_def.params:
            xml.append('      <paramdef></paramdef')
        xml.append('    </funcprototype>\n  </funcsynopsis>')
        return ''.join(xml)

    def create_method_prototype(self, meth_def, addlink=0):
        xml = ['<methodsynopsis language="python">\n']
        xml.append('    <methodname>')
        if addlink:
            xml.append('<link linkend="%s">' % self.make_method_ref(meth_def))
        xml.append(self.pyname(meth_def.name))
        if addlink:
            xml.append('</link>')
        xml.append('</methodname>\n')
        for type, name, dflt, null in meth_def.params:
            xml.append('    <methodparam><parameter>')
            xml.append(name)
            xml.append('</parameter>')
            if dflt:
                xml.append('<initializer>')
                xml.append(dflt)
                xml.append('</initializer>')
            xml.append('</methodparam>\n')
        if not meth_def.params:
            xml.append('    <methodparam></methodparam>')
        xml.append('  </methodsynopsis>')
        return ''.join(xml)

    def write_class_header(self, obj_name):
        self._fp.write(DOCBOOK_HEADER)
        self._fp.write(CLASS_HEADER_TEMPLATE % dict(
            entryid=self.make_class_ref(obj_name),
            name=self.pyname(obj_name),
            miscinfo="PyGTK Docs"))

    def write_class_footer(self, obj_name):
        self._fp.write('</refentry>\n')

    def write_heading(self, text):
        self._fp.write('  <refsect1>\n')
        self._fp.write('    <title>' + text + '</title>\n\n')

    def close_section(self):
        self._fp.write('  </refsect1>\n')

    def write_synopsis(self, obj_def):
        self._fp.write('<classsynopsis language="python">\n')
        self._fp.write('  <ooclass><classname>%s</classname></ooclass>\n'
                 % self.pyname(obj_def.c_name))
        if isinstance(obj_def, definitions.ObjectDef):
            if obj_def.parent:
                self._fp.write('  <ooclass><classname><link linkend="%s">%s'
                         '</link></classname></ooclass>\n'
                         % (self.make_class_ref(obj_def.parent),
                            self.pyname(obj_def.parent)))
            for base in obj_def.implements:
                self._fp.write('  <ooclass><classname><link linkend="%s">%s'
                         '</link></classname></ooclass>\n'
                         % (self.make_class_ref(base), self.pyname(base)))
        elif isinstance(obj_def, definitions.InterfaceDef):
            self._fp.write('  <ooclass><classname>gobject.GInterface'
                     '</classname></ooclass>\n')
        elif isinstance(obj_def, definitions.BoxedDef):
            self._fp.write('  <ooclass><classname>gobject.GBoxed'
                     '</classname></ooclass>\n')
        elif isinstance(obj_def, definitions.PointerDef):
            self._fp.write('  <ooclass><classname>gobject.GPointer'
                     '</classname></ooclass>\n')

        constructor = self.parser.find_constructor(obj_def, self.overrides)
        if constructor:
            self._fp.write(
                '%s\n' % self.create_constructor_prototype(constructor))
        for method in self.get_methods_for_object(obj_def):
            self._fp.write(
                '%s\n' % self.create_method_prototype(method, addlink=1))
        self._fp.write('</classsynopsis>\n\n')

    def write_hierarchy(self, obj_name, ancestry):
        self._fp.write('<synopsis>')
        indent = ''
        for name, interfaces in ancestry:
            self._fp.write(
                '%s+-- <link linkend="%s">%s</link>' %
                (indent, self.make_class_ref(name), self.pyname(name)))
            if interfaces:
                self._fp.write(' (implements ')
                for i in range(len(interfaces)):
                    self._fp.write('<link linkend="%s">%s</link>' %
                             (self.make_class_ref(interfaces[i]),
                              self.pyname(interfaces[i])))
                    if i != len(interfaces) - 1:
                        self._fp.write(', ')
                self._fp.write(')\n')
            else:
                self._fp.write('\n')
            indent = indent + '  '
        self._fp.write('</synopsis>\n\n')

    def write_params(self, params, ret, func_doc):
        if not params and (not ret or ret == 'none'):
            return
        self._fp.write('  <variablelist>\n')
        for type, name, dflt, null in params:
            if func_doc:
                descr = func_doc.get_param_description(name).strip()
            else:
                descr = 'a ' + type
            self._fp.write(VARIABLE_TEMPLATE % dict(
                parameter=name,
                description=self.reformat_text(descr, singleline=1)))
        if ret and ret != 'none':
            if func_doc and func_doc.ret:
                descr = func_doc.ret.strip()
            else:
                descr = 'a ' + ret
            self._fp.write(VARIABLE_TEMPLATE % dict(
                parameter='Returns',
                description=self.reformat_text(descr, singleline=1)))
        self._fp.write('  </variablelist>\n')

    def write_constructor(self, func_def, func_doc):
        prototype = self.create_constructor_prototype(func_def)
        self._fp.write('<programlisting>%s</programlisting>\n' % prototype)
        self.write_params(func_def.params, func_def.ret, func_doc)

        if func_doc and func_doc.description:
            self._fp.write(self.reformat_text(func_doc.description))
        self._fp.write('\n\n\n')

    def write_method(self, meth_def, func_doc):
        self._fp.write('  <refsect2 id="%s">\n' % (
            self.make_method_ref(meth_def), ))
        self._fp.write('    <title>%s.%s</title>\n\n' % (
            self.pyname(meth_def.of_object),
            meth_def.name))
        prototype = self.create_method_prototype(meth_def)
        self._fp.write('<programlisting>%s</programlisting>\n' % prototype)
        self.write_params(meth_def.params, meth_def.ret, func_doc)
        if func_doc and func_doc.description:
            self._fp.write(self.reformat_text(func_doc.description))
        self._fp.write('  </refsect2>\n\n\n')

    def output_toc(self, files, fp=sys.stdout):
        self._fp.write(DOCBOOK_HEADER)

        #self._fp.write('<reference id="class-reference">\n')
        #self._fp.write('  <title>Class Documentation</title>\n')
        #for filename, obj_def in files:
        #    self._fp.write('&' +
        #                   obj_def.c_name.translate(self._transtable) + ';\n')
        #self._fp.write('</reference>\n')

        self._fp.write('<reference id="class-reference" '
                       'xmlns:xi="http://www.w3.org/2001/XInclude">\n')
        self._fp.write('  <title>Class Reference</title>\n')
        for filename in sorted(files):
            self._fp.write('  <xi:include href="%s"/>\n' % filename)
        self._fp.write('</reference>\n')


def main(args):
    try:
        opts, args = getopt.getopt(args[1:], "d:s:o:",
                                   ["defs-file=", "override=", "source-dir=",
                                    "output-prefix="])
    except getopt.error, e:
        sys.stderr.write('docgen.py: %s\n' % e)
        sys.stderr.write(
            'usage: docgen.py -d file.defs [-s /src/dir] [-o output-prefix]\n')
        return 1

    defs_file = None
    overrides_file = None
    source_dirs = []
    output_prefix = 'docs'
    for opt, arg in opts:
        if opt in ('-d', '--defs-file'):
            defs_file = arg
        if opt in ('--override', ):
            overrides_file = arg
        elif opt in ('-s', '--source-dir'):
            source_dirs.append(arg)
        elif opt in ('-o', '--output-prefix'):
            output_prefix = arg
    if len(args) != 0 or not defs_file:
        sys.stderr.write(
            'usage: docgen.py -d file.defs [-s /src/dir] [-o output-prefix]\n')
        return 1

    d = DocbookDocWriter()
    d.add_sourcedirs(source_dirs)
    d.add_docs(defs_file, overrides_file, 'gio')
    d.output_docs(output_prefix)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
