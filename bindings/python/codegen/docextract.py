# -*- Mode: Python; py-indent-offset: 4 -*-
'''Simple module for extracting GNOME style doc comments from C
sources, so I can use them for other purposes.'''

import sys, os, string, re

# Used to tell if the "Since: ..." portion of the gtkdoc function description
# should be omitted.  This is useful for some C++ modules such as gstreamermm
# that wrap C API which is still unstable and including this information would
# not be useful.
# This variable is modified from docextract_to_xml based on the --no-since
# option being specified.
no_since = False

__all__ = ['extract']

class GtkDoc:
    def __init__(self):
        self.name = None
        self.block_type = '' # The block type ('function', 'signal', 'property')
        self.params = []
        self.annotations = []
        self.description = ''
        self.ret = ('', []) # (return, annotations)
    def set_name(self, name):
        self.name = name
    def set_type(self, block_type):
        self.block_type = block_type
    def get_type(self):
        return self.block_type
    def add_param(self, name, description, annotations=[]):
        if name == '...':
            name = 'Varargs'
        self.params.append((name, description, annotations))
    def append_to_last_param(self, extra):
        self.params[-1] = (self.params[-1][0], self.params[-1][1] + extra,
            self.params[-1][2])
    def append_to_named_param(self, name, extra):
        for i in range(len(self.params)):
            if self.params[i][0] == name:
                self.params[i] = (name, self.params[i][1] + extra,
                    self.params[i][2])
                return
        # fall through to adding extra parameter ...
        self.add_param(name, extra)
    def add_annotation(self, annotation):
        self.annotations.append(annotation)
    def get_annotations(self):
        return self.annotations
    def append_to_description(self, extra):
        self.description = self.description + extra
    def get_description(self):
        return self.description
    def add_return(self, first_line, annotations=[]):
        self.ret = (first_line, annotations)
    def append_to_return(self, extra):
        self.ret = (self.ret[0] + extra, self.ret[1])

comment_start_pattern = re.compile(r'^\s*/\*\*\s')
comment_end_pattern = re.compile(r'^\s*\*+/')
comment_line_lead_pattern = re.compile(r'^\s*\*\s*')
comment_empty_line_pattern = re.compile(r'^\s*\**\s*$')
function_name_pattern = re.compile(r'^([a-z]\w*)\s*:?(\s*\(.*\)\s*){0,2}\s*$')
signal_name_pattern = re.compile(r'^([A-Z]\w+::[a-z0-9-]+)\s*:?(\s*\(.*\)\s*){0,2}\s*$')
property_name_pattern = re.compile(r'^([A-Z]\w+:[a-z0-9-]+)\s*:?(\s*\(.*\)\s*){0,2}\s*$')
return_pattern = re.compile(r'^@?(returns:|return\s+value:)(.*\n?)$', re.IGNORECASE)
deprecated_pattern = re.compile(r'^(deprecated\s*:\s*.*\n?)$', re.IGNORECASE)
rename_to_pattern = re.compile(r'^(rename\s+to)\s*:\s*(.*\n?)$', re.IGNORECASE)
param_pattern = re.compile(r'^@(\S+)\s*:(.*\n?)$')
# Used to extract the annotations in the parameter and return descriptions
# extracted using above [param|return]_pattern patterns.
annotations_pattern = re.compile(r'^(?:(\s*\(.*\)\s*)*:)')
# Used to construct the annotation lists.
annotation_lead_pattern = re.compile(r'^\s*\(\s*(.*?)\s*\)\s*')

# These patterns determine the identifier of the current comment block.  They
# are grouped in a list for easy determination of block identifiers (in
# skip_to_identifier).  The function_name_pattern should be tested for last
# because it always matches signal and property identifiers.
identifier_patterns = [ signal_name_pattern, property_name_pattern, function_name_pattern ]

# This pattern is to match return sections that forget to have a colon (':')
# after the initial 'Return' phrase.  It is not included by default in the list
# of final sections below because a lot of function descriptions begin with
# 'Returns ...' and the process_description() function would stop right at that
# first line, thinking it is a return section.
no_colon_return_pattern = re.compile(r'^@?(returns|return\s+value)\s*(.*\n?)$', re.IGNORECASE)
since_pattern = re.compile(r'^(since\s*:\s*.*\n?)$', re.IGNORECASE)

# These patterns normally will be encountered after the description.  Knowing
# the order of their appearance is difficult so this list is used to test when
# one begins and the other ends when processing the rest of the sections after
# the description.
final_section_patterns = [ return_pattern, since_pattern, deprecated_pattern, rename_to_pattern ]

def parse_file(fp, doc_dict):
    line = fp.readline()
    while line:
        cur_doc = GtkDoc()
        line = skip_to_comment_block(fp, line)
        line = skip_to_identifier(fp, line, cur_doc)
        # See if the identifier is found (stored in the current GtkDoc by
        # skip_to_identifier).  If so, continue reading the rest of the comment
        # block.
        if cur_doc.name:
            line = process_params(fp, line, cur_doc)
            line = process_description(fp, line, cur_doc)
            line = process_final_sections(fp, line, cur_doc)
            # Add the current doc block to the dictionary of doc blocks.
            doc_dict[cur_doc.name] = cur_doc

# Given a list of annotations as string of the form 
# '(annotation1) (annotation2) ...' return a list of annotations of the form
# [ (name1, value1), (name2, value2) ... ].  Not all annotations have values so
# the values in the list of tuples could be empty ('').
def get_annotation_list(annotations):
    annotation_list = []
    while annotations:
        match = annotation_lead_pattern.match(annotations)
        if match:
            annotation_contents = match.group(1)
            name, split, value = annotation_contents.strip().partition(' ')
            annotation_list.append((name, value))
            # Remove first occurrence to continue processing.
            annotations = annotation_lead_pattern.sub('', annotations)
        else:
            break
    return annotation_list

# Given a currently read line, test that line and continue reading until the
# beginning of a comment block is found or eof is reached.  Return the last
# read line.
def skip_to_comment_block(fp, line):
    while line:
        if comment_start_pattern.match(line):
            break
        line = fp.readline()
    return line

# Given the current line in a comment block, continue skipping lines until a
# non-blank line in the comment block is found or until the end of the block
# (or eof) is reached.  Returns the line where reading stopped.
def skip_to_nonblank(fp, line):
    while line:
        if not comment_empty_line_pattern.match(line):
            break
        line = fp.readline()
        # Stop processing if eof or end of comment block is reached.
        if not line or comment_end_pattern.match(line):
            break
    return line

# Given the first line of a comment block (the '/**'), see if the next
# non-blank line is the identifier of the comment block.  Stop processing if
# the end of the block or eof is reached.  Store the identifier (if there is
# one) and its type ('function', 'signal' or 'property') in the given GtkDoc.
# Return the line where the identifier is found or the line that stops the
# processing (if eof or the end of the comment block is found first).
def skip_to_identifier(fp, line, cur_doc):
    # Skip the initial comment block line ('/**') if not eof.
    if line: line = fp.readline()

    # Now skip empty lines.
    line = skip_to_nonblank(fp, line)

    # See if the first non-blank line is the identifier.
    if line and not comment_end_pattern.match(line):
        # Remove the initial ' * ' in comment block line and see if there is an
        # identifier.
        line = comment_line_lead_pattern.sub('', line)
        for pattern in identifier_patterns:
            match = pattern.match(line)
            if match:
                # Set the GtkDoc name.
                cur_doc.set_name(match.group(1))
                # Get annotations and add them to the GtkDoc.
                annotations = get_annotation_list(match.group(2))
                for annotation in annotations:
                    cur_doc.add_annotation(annotation)
                # Set the GtkDoc type.
                if pattern == signal_name_pattern:
                    cur_doc.set_type('signal')
                elif pattern == property_name_pattern:
                    cur_doc.set_type('property')
                elif pattern == function_name_pattern:
                    cur_doc.set_type('function')
                return line
    return line

# Given a currently read line (presumably the identifier line), read the next
# lines, testing to see if the lines are part of parameter descriptions.  If
# so, store the parameter descriptions in the given doc block.  Stop on eof and
# return the last line that stops the processing.
def process_params(fp, line, cur_doc):
    # Skip the identifier line if not eof.  Also skip any blank lines in the
    # comment block.  Return if eof or the end of the comment block are
    # encountered.
    if line: line = fp.readline()
    line = skip_to_nonblank(fp, line)
    if not line or comment_end_pattern.match(line):
        return line

    # Remove initial ' * ' in first non-empty comment block line.
    line = comment_line_lead_pattern.sub('', line)

    # Now process possible parameters as long as no eof or the end of the
    # param section is not reached (which could be triggered by anything that
    # doesn't match a '@param:..." line, even the end of the comment block).
    match = param_pattern.match(line)
    while line and match:
        description = match.group(2)

        # First extract the annotations from the description and save them.
        annotations = []
        annotation_match = annotations_pattern.match(description)
        if annotation_match:
            annotations = get_annotation_list(annotation_match.group(1))
            # Remove the annotations from the description
            description = annotations_pattern.sub('', description)

        # Default to appending lines to current parameter.
        append_func = cur_doc.append_to_last_param

        # See if the return has been included as part of the parameter
        # section and make sure that lines are added to the GtkDoc return if
        # so.
        if match.group(1).lower() == "returns":
            cur_doc.add_return(description, annotations)
            append_func = cur_doc.append_to_return
        # If not, just add it as a regular parameter.
        else:
            cur_doc.add_param(match.group(1), description, annotations)

        # Now read lines and append them until next parameter, beginning of
        # description (an empty line), the end of the comment block or eof.
        line = fp.readline()
        while line:
            # Stop processing if end of comment block or a blank comment line
            # is encountered.
            if comment_empty_line_pattern.match(line) or \
                    comment_end_pattern.match(line):
                break

            # Remove initial ' * ' in comment block line.
            line = comment_line_lead_pattern.sub('', line)

            # Break from current param processing if a new one is
            # encountered.
            if param_pattern.match(line): break;

            # Otherwise, just append the current line and get the next line.
            append_func(line)
            line = fp.readline()

        # Re-evaluate match for while condition
        match = param_pattern.match(line)

    # End by returning the current line.
    return line

# Having processed parameters, read the following lines into the description of
# the current doc block until the end of the comment block, the end of file or
# a return section is encountered.
def process_description(fp, line, cur_doc):
    # First skip empty lines returning on eof or end of comment block.
    line = skip_to_nonblank(fp, line)
    if not line or comment_end_pattern.match(line):
        return line

    # Remove initial ' * ' in non-empty comment block line.
    line = comment_line_lead_pattern.sub('', line)

    # Also remove possible 'Description:' prefix.
    if line[:12] == 'Description:': line = line[12:]

    # Used to tell if the previous line was blank and a return section
    # uncommonly marked with 'Returns ...' instead of 'Returns: ...'  has
    # started (assume it is non-empty to begin with).
    prev_line = 'non-empty'

    # Now read lines until a new section (like a return or a since section) is
    # encountered.
    while line:
        # See if the description section has ended (if the line begins with
        # 'Returns ...' and the previous line was empty -- this loop replaces
        # empty lines with a newline).
        if no_colon_return_pattern.match(line) and prev_line == '\n':
            return line
        # Or if one of the patterns of the final sections match
        for pattern in final_section_patterns:
            if pattern.match(line):
                return line

        # If not, append lines to description in the doc comment block.
        cur_doc.append_to_description(line)

        prev_line = line
        line = fp.readline()

        # Stop processing on eof or at the end of comment block.
        if not line or comment_end_pattern.match(line):
            return line

        # Remove initial ' * ' in line so that the text can be appended to the
        # description of the comment block and make sure that if the line is
        # empty it be interpreted as a newline.
        line = comment_line_lead_pattern.sub('', line)
        if not line: line = '\n'

# Given the line that ended the description (the first line of one of the final
# sections) process the final sections ('Returns:', 'Since:', etc.) until the
# end of the comment block or eof.  Return the line that ends the processing.
def process_final_sections(fp, line, cur_doc):
    while line and not comment_end_pattern.match(line):
        # Remove leading ' * ' from current non-empty comment line.
        line = comment_line_lead_pattern.sub('', line)
        # Temporarily append the no colon return pattern to the final section
        # patterns now that the description has been processed.  It will be
        # removed after the for loop below executes so that future descriptions
        # that begin with 'Returns ...' are not interpreted as a return
        # section.
        final_section_patterns.append(no_colon_return_pattern)
        for pattern in final_section_patterns:
            match = pattern.match(line)
            if match:
                if pattern == return_pattern or \
                        pattern == no_colon_return_pattern:
                    # Dealing with a 'Returns:' so first extract the
                    # annotations from the description and save them.
                    description = match.group(2)
                    annotations = []
                    annotation_match = \
                            annotations_pattern.match(description)
                    if annotation_match:
                        annotations = \
                                get_annotation_list(annotation_match.group(1))
                        # Remove the annotations from the description
                        description = annotations_pattern.sub('', description)

                    # Now add the return.
                    cur_doc.add_return(description, annotations)
                    # In case more lines need to be appended.
                    append_func = cur_doc.append_to_return
                elif pattern == rename_to_pattern:
                    # Dealing with a 'Rename to:' section (GObjectIntrospection
                    # annotation) so no further lines will be appended but this
                    # single one (and only to the annotations).
                    append_func = None
                    cur_doc.add_annotation((match.group(1),
                            match.group(2)))
                else:
                    # For all others ('Since:' and 'Deprecated:') just append
                    # the line to the description for now.
                    # But if --no-since is specified, don't append it.
                    if no_since and pattern == since_pattern:
                        pass
                    else:
                        cur_doc.append_to_description(line)

                    # In case more lines need to be appended.
                    append_func = cur_doc.append_to_description

                # Stop final section pattern matching for loop since a match
                # has already been found.
                break

        # Remove the no colon return pattern (which was temporarily added in
        # the just executed loop) from the list of final section patterns.
        final_section_patterns.pop()

        line = fp.readline()

        # Now continue appending lines to current section until a new one is
        # found or an eof or the end of the comment block is encountered.
        finished = False
        while not finished and line and \
                not comment_end_pattern.match(line):
            # Remove leading ' * ' from line and make sure that if it is empty,
            # it be interpreted as a newline.
            line = comment_line_lead_pattern.sub('', line)
            if not line: line = '\n'

            for pattern in final_section_patterns:
                if pattern.match(line):
                    finished = True
                    break

            # Break out of loop if a new section is found (determined in above
            # inner loop).
            if finished: break

            # Now it's safe to append line.
            if append_func: append_func(line)

            # Get the next line to continue processing.
            line = fp.readline()

    return line

def parse_dir(dir, doc_dict):
    for file in os.listdir(dir):
        if file in ('.', '..'): continue
        path = os.path.join(dir, file)
        if os.path.isdir(path):
            parse_dir(path, doc_dict)
        if len(file) > 2 and file[-2:] == '.c':
            sys.stderr.write("Processing " + path + '\n')
            parse_file(open(path, 'r'), doc_dict)

def extract(dirs, doc_dict=None):
    if not doc_dict: doc_dict = {}
    for dir in dirs:
        parse_dir(dir, doc_dict)
    return doc_dict

tmpl_section_pattern = re.compile(r'^<!-- ##### (\w+) (\w+) ##### -->$')
def parse_tmpl(fp, doc_dict):
    cur_doc = None

    line = fp.readline()
    while line:
        match = tmpl_section_pattern.match(line)
        if match:
            cur_doc = None  # new input shouldn't affect the old doc dict
            sect_type = match.group(1)
            sect_name = match.group(2)

            if sect_type == 'FUNCTION':
                cur_doc = doc_dict.get(sect_name)
                if not cur_doc:
                    cur_doc = GtkDoc()
                    cur_doc.set_name(sect_name)
                    doc_dict[sect_name] = cur_doc
        elif line == '<!-- # Unused Parameters # -->\n':
            cur_doc = None # don't worry about unused params.
        elif cur_doc:
            if line[:10] == '@Returns: ':
                if string.strip(line[10:]):
                    cur_doc.append_to_return(line[10:])
            elif line[0] == '@':
                pos = string.find(line, ':')
                if pos >= 0:
                    cur_doc.append_to_named_param(line[1:pos], line[pos+1:])
                else:
                    cur_doc.append_to_description(line)
            else:
                cur_doc.append_to_description(line)

        line = fp.readline()

def extract_tmpl(dirs, doc_dict=None):
    if not doc_dict: doc_dict = {}
    for dir in dirs:
        for file in os.listdir(dir):
            if file in ('.', '..'): continue
            path = os.path.join(dir, file)
            if os.path.isdir(path):
                continue
            if len(file) > 2 and file[-2:] == '.sgml':
                parse_tmpl(open(path, 'r'), doc_dict)
    return doc_dict
