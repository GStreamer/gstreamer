#!/usr/bin/perl
#
# gapi2xml.pl : Generates an XML representation of GObject based APIs.
#
# Author: Mike Kestner <mkestner@speakeasy.net>
#
# Copyright (c) 2001-2003 Mike Kestner
# Copyright (c) 2003-2009 Novell, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of version 2 of the GNU General Public
# License as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
##############################################################

$parser_version = 2;
$debug=$ENV{'GAPI_DEBUG'};

use XML::LibXML;

if (!$ARGV[2]) {
	die "Usage: gapi_pp.pl <srcdir> | gapi2xml.pl <namespace> <outfile> <libname>\n";
}

$ns = $ARGV[0];
$libname = $ARGV[2];

# Used by name mangling sub
%num2txt = ('1', "One", '2', "Two", '3', "Three", '4', "Four", '5', "Five",
	    '6', "Six", '7', "Seven", '8', "Eight", '9', "Nine", '0', "Zero");

##############################################################
# Check if the filename provided exists.  We parse existing files into
# a tree and append the namespace to the root node.  If the file doesn't 
# exist, we create a doc tree and root node to work with.
##############################################################

if (-e $ARGV[1]) {
	#parse existing file and get root node.
	$doc = XML::LibXML->new->parse_file($ARGV[1]);
	$root = $doc->getDocumentElement();
	if ($root->getAttribute ('parser_version') != $parser_version) {
		die "The version of the file does not match the version of the parser";
	}
} else {
	$doc = XML::LibXML::Document->new();
	$root = $doc->createElement('api');
	$root->setAttribute('parser_version', $parser_version);
	$doc->setDocumentElement($root);
	$warning_node = XML::LibXML::Comment->new ("\n\n        This file was automatically generated.\n        Please DO NOT MODIFY THIS FILE, modify .metadata files instead.\n\n");
	$root->appendChild($warning_node);
}

$ns_elem = $doc->createElement('namespace');
$ns_elem->setAttribute('name', $ns);
$ns_elem->setAttribute('library', $libname);
$root->appendChild($ns_elem);

##############################################################
# First we parse the input for typedefs, structs, enums, and class_init funcs
# and put them into temporary hashes.
##############################################################

while ($line = <STDIN>) {
	if ($line =~ /typedef\s+(struct\s+\w+\s+)\*+(\w+);/) {
		$ptrs{$2} = $1;
	} elsif ($line =~ /typedef\s+(struct\s+\w+)\s+(\w+);/) {
		next if ($2 =~ /Private$/);
		# fixme: siiigh
		$2 = "GdkDrawable" if ($1 eq "_GdkDrawable");
		$types{$2} = $1;
	} elsif ($line =~ /typedef\s+struct/) {
		$sdef = $line;
		while ($line = <STDIN>) {
			$sdef .= $line;
			last if ($line =~ /^(deprecated)?}/);
		}
		$sdef =~ s!/\*.*?(\*/|\n)!!g;
		$sdef =~ s/\n\s*//g;
		$types{$1} = $sdef if ($sdef =~ /.*\}\s*(\w+);/);
	} elsif ($line =~ /typedef\s+(unsigned\s+\w+)\s+(\**)(\w+);/) {
		$types{$3} = $1 . $2;
	} elsif ($line =~ /typedef\s+(\w+)\s+(\**)(\w+);/) {
		$types{$3} = $1 . $2;
	} elsif ($line =~ /typedef\s+enum\s+(\w+)\s+(\w+);/) {
		$etypes{$1} = $2;
	} elsif ($line =~ /^((deprecated)?typedef\s+)?\benum\b/) {
		$edef = $line;
		while ($line = <STDIN>) {
			$edef .= $line;
			last if ($line =~ /^(deprecated)?}\s*(\w+)?;/);
		}
		$edef =~ s/\n\s*//g;
		$edef =~ s|/\*.*?\*/||g;
		if ($edef =~ /typedef.*}\s*(\w+);/) {
			$ename = $1;
		} elsif ($edef =~ /^(deprecated)?enum\s+(\w+)\s*{/) {
			$ename = $2;
		} else {
			print "Unexpected enum format\n$edef";
			next;
		}
		$edefs{$ename} = $edef;
	} elsif ($line =~ /typedef\s+\w+\s*\**\s*\(\*\s*(\w+)\)\s*\(/) {
		$fname = $1;
		$fdef = "";
		while ($line !~ /;/) {
			$fdef .= $line;
			$line = <STDIN>;
		}
		$fdef .= $line;
		$fdef =~ s/\n\s+//g;
		$fpdefs{$fname} = $fdef;
	} elsif ($line =~ /^(private|deprecated)?struct\s+(\w+)/) {
		next if ($line =~ /;/);
		$sname = $2;
		$sdef = $line;
		while ($line = <STDIN>) {
			$sdef .= $line;
			last if ($line =~ /^(deprecated)?}/);
		}
		$sdef =~ s!/\*[^<].*?(\*/|\n)!!g;
		$sdef =~ s/\n\s*//g;
		$sdefs{$sname} = $sdef if (!exists ($sdefs{$sname}));
	} elsif ($line =~ /^(\w+)_(class|base)_init\b/) {
		$class = StudlyCaps($1);
		$pedef = $line;
		while ($line = <STDIN>) {
			$pedef .= $line;
			last if ($line =~ /^(deprecated)?}/);
		}
		$pedefs{lc($class)} = $pedef;
	} elsif ($line =~ /^(\w+)_get_type\b/) {
		$class = StudlyCaps($1);
		$pedef = $line;
		while ($line = <STDIN>) {
			$pedef .= $line;
			if ($line =~ /g_boxed_type_register_static/) {
				$boxdef = $line;
				while ($line !~ /;/) {
					$boxdef .= ($line = <STDIN>);
				}
				$boxdef =~ s/\n\s*//g;
				$boxdef =~ /\(\"(\w+)\"/;
				my $boxtype = $1;
				$boxtype =~ s/($ns)Type(\w+)/$ns$2/;
				$boxdefs{$boxtype} = $boxdef;
			} elsif ($line =~ /g_(enum|flags)_register_static/) {
				$pedef =~ /^(\w+_get_type)/;
				$enum_gtype{$class} = $1;
			}
			last if ($line =~ /^(deprecated)?}/);
		}
		$typefuncs{lc($class)} = $pedef;
	} elsif ($line =~ /^G_DEFINE_TYPE_WITH_CODE\s*\(\s*(\w+)/) {
		$typefuncs{lc($1)} = $line;
	} elsif ($line =~ /^(deprecated)?(const|G_CONST_RETURN)?\s*(struct\s+)?\w+\s*\**(\s*(const|G_CONST_RETURN)\s*\**)?\s*(\w+)\s*\(/) {
		$fname = $6;
		$fdef = "";
		while ($line !~ /;/) {
			$fdef .= $line;
			$line = <STDIN>;
		}
		$fdef .= $line;
		$fdef =~ s/\n\s*//g;
		if ($fdef !~ /^_/) {
			$fdefs{$fname} = $fdef;
		}
	} elsif ($line =~ /CHECK_(\w*)CAST/) {
		$cast_macro = $line;
		while ($line =~ /\\$/) {
			$line = <STDIN>;
			$cast_macro .= $line;
		}
		$cast_macro =~ s/\\\n\s*//g;
		$cast_macro =~ s/\s+/ /g;
		if ($cast_macro =~ /G_TYPE_CHECK_(\w+)_CAST.*,\s*(\w+),\s*(\w+)\)/) {
			if ($1 eq "INSTANCE") {
				$objects{$2} = $3 . $objects{$2};
			} else {
				$objects{$2} .= ":$3";
			}
		} elsif ($cast_macro =~ /G_TYPE_CHECK_(\w+)_CAST.*,\s*([a-zA-Z0-9]+)_(\w+)_get_type\s*\(\),\s*(\w+)\)/) {
			$typename = uc ("$2_type_$3");
			if ($1 eq "INSTANCE") {
				$objects{$typename} = $4 . $objects{$typename};
			} else {
				$objects{$typename} .= ":$4";
			}
		} elsif ($cast_macro =~ /GTK_CHECK_CAST.*,\s*(\w+),\s*(\w+)/) {
			$objects{$1} = $2 . $objects{$1};
		} elsif ($cast_macro =~ /GTK_CHECK_CLASS_CAST.*,\s*(\w+),\s*(\w+)/) {
			$objects{$1} .= ":$2";
		} elsif ($cast_macro =~ /GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST.*,\s*(\w+),\s*(\w+)/) {
			$objects{$1} = $2 . $objects{$1};
		}
	} elsif ($line =~ /INSTANCE_GET_INTERFACE.*,\s*(\w+),\s*(\w+)/) {
		$ifaces{$1} = $2;
	} elsif ($line =~ /^BUILTIN\s*\{\s*\"(\w+)\".*GTK_TYPE_BOXED/) {
		$boxdefs{$1} = $line;
	} elsif ($line =~ /^BUILTIN\s*\{\s*\"(\w+)\".*GTK_TYPE_(ENUM|FLAGS)/) {
		# ignoring these for now.
	} elsif ($line =~ /^(deprecated)?\#define/) {
		my $test_ns = uc ($ns);
		if ($line =~ /^deprecated\#define\s+(\w+)\s+\"(.*)\"/) {
			$defines{"deprecated$1"} = $2;
		} elsif ($line =~ /\#define\s+(\w+)\s+\"(.*)\"/) {
			$defines{$1} = $2;
		}
	} elsif ($line !~ /\/\*/) {
		print $line;
	}
}

##############################################################
# Produce the enum definitions.
##############################################################
%enums = ();

foreach $cname (sort(keys(%edefs))) {
	$ecnt++;
	$def = $edefs{$cname};
	$cname = $etypes{$cname} if (exists($etypes{$cname}));
	$enums{lc($cname)} = $cname;
	$enum_elem = addNameElem($ns_elem, 'enum', $cname, $ns);
	if ($def =~ /^deprecated/) {
		$enum_elem->setAttribute("deprecated", "1");
		$def =~ s/deprecated//g;
	}
	if ($enum_gtype{$cname}) {
		$enum_elem->setAttribute("gtype", $enum_gtype{$cname});
	}
	if ($def =~ /<</) {
		$enum_elem->setAttribute('type', "flags");
	} else {
		$enum_elem->setAttribute('type', "enum");
	}
	$def =~ /\{(.*\S)\s*\}/;
	@vals = split(/,\s*/, $1);
	$vals[0] =~ s/^\s+//;
	@nameandval = split(/=/, $vals[0]);
	@v0 = split(/_/, $nameandval[0]);
	if (@vals > 1) {
		$done = 0;
		for ($idx = 0, $regex = ""; $idx < @v0; $idx++) {
			$regex .= ($v0[$idx] . "_");
			foreach $val (@vals) {
				$done = 1 if ($val !~ /$regex/);
			}
			last if $done;
		}
		$common = join("_", @v0[0..$idx-1]);
	} else {
		$common = join("_", @v0[0..$#v0-1]);
	}
	
	foreach $val (@vals) {
		$val =~ s/=\s*\(\s*(.*\S)\s*\)\s*/= \1/;
		if ($val =~ /$common\_?(\w+)\s*=\s*(.*)$/) {
			$name = $1;
			$enumval = $2;
			if ($enumval =~ /^(\d+|0x[0-9A-Fa-f]+)u?\s*<<\s*(\d+)$/) {
				$enumval = "$1 << $2";
			} elsif ($enumval =~ /^$common\_?(\w+)$/) {
				$enumval = StudlyCaps(lc($1))
			}
		} elsif ($val =~ /$common\_?(\w+)/) {
			$name = $1; $enumval = "";
		} else {
			die "Unexpected enum value: $val for common value $common\n";
		}

		$val_elem = addNameElem($enum_elem, 'member');
		$val_elem->setAttribute('cname', "$common\_$name");
		$val_elem->setAttribute('name', StudlyCaps(lc($name)));
		if ($enumval) {
			$val_elem->setAttribute('value', $enumval);
		}
	}
}

##############################################################
# Parse the callbacks.
##############################################################

foreach $cbname (sort(keys(%fpdefs))) {
	next if ($cbname =~ /^_/);
	$cbcnt++;
	$fdef = $cb = $fpdefs{$cbname};
	$cb_elem = addNameElem($ns_elem, 'callback', $cbname, $ns);
	$cb =~ /typedef\s+(.*)\(.*\).*\((.*)\);/;
	$ret = $1; $params = $2;
	addReturnElem($cb_elem, $ret);
	if ($params && ($params ne "void")) {
		addParamsElem($cb_elem, split(/,/, $params));
	}
}

##############################################################
# Parse the interfaces list.   
##############################################################

foreach $type (sort(keys(%ifaces))) {

	$iface = $ifaces{$type};
	($inst, $dontcare) = split(/:/, delete $objects{$type});
	$initfunc = $pedefs{lc($inst)};
	$ifacetype = delete $types{$iface};
	delete $types{$inst};
	
	$ifacecnt++;
	$iface_el = addNameElem($ns_elem, 'interface', $inst, $ns);

	$elem_table{lc($inst)} = $iface_el;

	$classdef = $sdefs{$1} if ($ifacetype =~ /struct\s+(\w+)/);
	my @signal_vms;
	if ($initfunc) {
		@signal_vms = parseInitFunc($iface_el, $initfunc, $classdef);
	} else {
		warn "Don't have an init func for $inst.\n" if $debug;
		# my @signal_vms;
	}

	addClassElem ($iface_el, $classdef, @signal_vms) if ($classdef);
}


##############################################################
# Parse the classes by walking the objects list.   
##############################################################

foreach $type (sort(keys(%objects))) {
	($inst, $class) = split(/:/, $objects{$type});
	$class = $inst . "Class" if (!$class);
	$initfunc = $pedefs{lc($inst)};
	$typefunc = $typefuncs{lc($inst)};
	$insttype = delete $types{$inst};
	$classtype = delete $types{$class};

	$instdef = $classdef = "";
	$instdef = $sdefs{$1} if ($insttype =~ /struct\s+(\w+)/);
	$classdef = $sdefs{$1} if ($classtype =~ /struct\s+(\w+)/);
	$classdef =~ s/deprecated//g;
	$instdef =~ s/\s+(\*+)([^\/])/\1 \2/g;
	warn "Strange Class $inst\n" if (!$instdef && $debug);

	$classcnt++;
	$obj_el = addNameElem($ns_elem, 'object', $inst, $ns);

	$elem_table{lc($inst)} = $obj_el;

	# Check if the object is deprecated
	if ($instdef =~ /^deprecatedstruct/) {
		$obj_el->setAttribute("deprecated", "1");
		$instdef =~ s/deprecated//g;
	}
	
	# Extract parent and fields from the struct
	if ($instdef =~ /^struct/) {
		$instdef =~ /\{(.*)\}/;
		$fieldstr = $1;
		$fieldstr =~ s|/\*[^<].*?\*/||g;
		@fields = split(/;/, $fieldstr);
		addFieldElems($obj_el, 'private', @fields);
		$obj_el->setAttribute('parent', $obj_el->firstChild->getAttribute('type'));
		$obj_el->removeChild($obj_el->firstChild);
	} elsif ($instdef =~ /privatestruct/) {
		# just get the parent for private structs
		$instdef =~ /\{\s*(\w+)/;
		$obj_el->setAttribute('parent', "$1");
	}

	# Get the props from the class_init func.
	if ($initfunc) {
		@signal_vms = parseInitFunc($obj_el, $initfunc, $classdef);
	} else {
		warn "Don't have an init func for $inst.\n" if $debug;
	}

	addClassElem ($obj_el, $classdef, @signal_vms) if ($classdef);

	# Get the interfaces from the class_init func.
	if ($typefunc) {
		if ($typefunc =~ /G_DEFINE_TYPE_WITH_CODE/) {
			parseTypeFuncMacro($obj_el, $typefunc);
		} else {
			parseTypeFunc($obj_el, $typefunc);
		}
	} else {
		warn "Don't have a GetType func for $inst.\n" if $debug;
	}

}

##############################################################
# Parse the remaining types.
##############################################################

foreach $key (sort (keys (%types))) {

	$lasttype = $type = $key;
	while ($type && ($types{$type} !~ /struct/)) {
		$lasttype = $type;
		$type = $types{$type};
	}

	if ($types{$type} =~ /struct\s+(\w+)/) {
		$type = $1;
		if (exists($sdefs{$type})) {
			$def = $sdefs{$type};
		} else {
			$def = "privatestruct";
		}
	} elsif ($types{$type} =~ /struct/ && $type =~ /^$ns/) {
		$def = $types{$type};
	} else {
		$elem = addNameElem($ns_elem, 'alias', $key, $ns);
		$elem->setAttribute('type', $lasttype);
		warn "alias $key to $lasttype\n" if $debug;
		next;
	}

	# fixme: hack
	if ($key eq "GdkBitmap") {
		$struct_el = addNameElem($ns_elem, 'object', $key, $ns);
	} elsif (exists($boxdefs{$key})) {
		$struct_el = addNameElem($ns_elem, 'boxed', $key, $ns);
	} else {
		$struct_el = addNameElem($ns_elem, 'struct', $key, $ns);
	}

	if ($def =~ /^deprecated/) {
		$struct_el->setAttribute("deprecated", "1");
		$def =~ s/deprecated//g;
	}

	$elem_table{lc($key)} = $struct_el;

	$def =~ s/\s+/ /g;
	if ($def =~ /privatestruct/) {
		$struct_el->setAttribute('opaque', 'true');
	} else {
		$def =~ /\{(.+)\}/;
		addFieldElems($struct_el, 'public', split(/;/, $1));
	}
}

# really, _really_ opaque structs that aren't even defined in sources. Lovely.
foreach $key (sort (keys (%ptrs))) {
	next if $ptrs{$key} !~ /struct\s+(\w+)/;
	$type = $1;
	$struct_el = addNameElem ($ns_elem, 'struct', $key, $ns);
	$struct_el->setAttribute('opaque', 'true');
	$elem_table{lc($key)} = $struct_el;
}

addFuncElems();
addStaticFuncElems();

# This should probably be done in a more generic way
foreach $define (sort (keys (%defines))) {
	next if $define !~ /[A-Z]_STOCK_/;
	if ($stocks{$ns}) {
		$stock_el = $stocks{$ns};
	} else {
		$stock_el = addNameElem($ns_elem, "object", $ns . "Stock", $ns);
		$stocks{$ns} = $stock_el;
	}
	$string_el = addNameElem ($stock_el, "static-string", $define);
	$string_name = lc($define);
	$string_name =~ s/\w+_stock_//;
	$string_el->setAttribute('name', StudlyCaps($string_name));
	$string_el->setAttribute('value', $defines{$define});
}

##############################################################
# Output the tree
##############################################################

if ($ARGV[1]) {
	open(XMLFILE, ">$ARGV[1]") || die "Couldn't open $ARGV[1] for writing.\n";
	print XMLFILE $doc->toString();
	close(XMLFILE);
} else {
	print $doc->toString();
}

##############################################################
# Generate a few stats from the parsed source.
##############################################################

$scnt = keys(%sdefs); $fcnt = keys(%fdefs); $tcnt = keys(%types);
print "structs: $scnt  enums: $ecnt  callbacks: $cbcnt\n";
print "funcs: $fcnt types: $tcnt  classes: $classcnt\n";
print "props: $propcnt childprops: $childpropcnt signals: $sigcnt\n\n";

sub addClassElem
{
	my ($obj_el, $classdef, @signal_vms) = @_;

	my %is_signal_vm;
	for (@signal_vms) { 
		$is_signal_vm{$_} = 1;
	}

	if ($classdef =~ /struct\s+_?(\w+)\s*{(.*)};/) {
		my $elem = $doc->createElement('class_struct');
		$elem->setAttribute('cname', $1);
		$obj_el->insertBefore($elem, $obj_el->firstChild);
		$fields = $2;
		$fields =~ s!/\*.*?\*/!!g; # Remove comments
		foreach $field (split (/;/, $fields)) {
			if ($field =~ /\s*(G_CONST_RETURN\s+)?(\S+\s*\**)\s*\(\s*\*\s*(\w+)\)\s*(\((.*?)\))?/) {
				$ret = $1 . $2; $cname = $3; $parms = $5;

				$class_elem = $doc->createElement('method');
				$elem->appendChild($class_elem);

				if ($is_signal_vm{$cname}) {
					$class_elem->setAttribute('signal_vm', $cname);
				} else {
					$class_elem->setAttribute('vm', $cname);

					$vm_elem = $doc->createElement('virtual_method');
					$obj_el->appendChild($vm_elem);
					$vm_elem->setAttribute('name', StudlyCaps($cname));
					$vm_elem->setAttribute('cname', $cname);

					addReturnElem($vm_elem, $ret);

					if ($parms && ($parms ne "void")) { # if there are any parameters
						@parm_arr = split(/,/, $parms);
						$parms =~ /\s*(\w+)/; # Get type of first parameter
						if ($1 ne $obj_el->getAttribute ('cname')) {
							$vm_elem->setAttribute('shared', 'true'); # First parameter is not of the type of the declaring class -> static vm
						} else {
							($dump, @parm_arr) = @parm_arr;
						}
						addParamsElem($vm_elem, @parm_arr);
					} else {
						$vm_elem->setAttribute('shared', 'true');
					}

					if ($cname =~ /reserved[0-9]+$/ || $cname =~ /padding[0-9]+$/ || $cname =~ /recent[0-9]+$/) {
						$vm_elem->setAttribute('padding', 'true');
					}
				}
			} elsif ($field =~ /(unsigned\s+)?(\S+)\s+(.+)/) {
				my $type = $1 . $2; $symb = $3;
				foreach $tok (split (/,\s*/, $symb)) { # multiple field defs may occur in one line; like int xrange, yrange;
					$tok =~ /(\*)?(\w+)\s*(.*)/;
					my $field_type = $type . $1; my $cname = $2; my $modifiers = $3;

					$fld_elem = addNameElem($elem, 'field', $cname, "");
					$fld_elem->setAttribute('type', "$field_type");

					if ($modifiers =~ /\[(.*)\]/) {
						$fld_elem->setAttribute('array_len', "$1");
					} elsif ($modifiers =~ /\:\s*(\d+)/) {
						$fld_elem->setAttribute('bits', "$1");
					}
				}
			} elsif ($field =~ /\S+/) {
				print "***** Unmatched class struct field $field\n";
			}
		}
	} else {
		print "***** Unmatched $classdef\n";
	}
}

sub addFieldElems
{
	my ($parent, $defaultaccess, @fields) = @_;
	my $access = $defaultaccess;

	foreach $field (@fields) {
		if ($field =~ m!/\*< (public|private) >.*\*/(.*)$!) {
			$access = $1;
			$field = $2;
		}
		next if ($field !~ /\S/);
		$field =~ s/GSEAL\s*\((.*)\)/\1/g;
		$field =~ s/\s+(\*+)/\1 /g;
		$field =~ s/(const\s+)?(\w+)\*\s+const\*/const \2\*/g;
		$field =~ s/(\w+)\s+const\s*\*/const \1\*/g;
		$field =~ s/const /const\-/g;
		$field =~ s/struct /struct\-/g;
		$field =~ s/.*\*\///g;
		next if ($field !~ /\S/);
		
		if ($field =~ /(\S+\s+\*?)\(\*\s*(.+)\)\s*\((.*)\)/) {
			$elem = addNameElem($parent, 'callback', $2);
			addReturnElem($elem, $1);
			addParamsElem($elem, $3);
		} elsif ($field =~ /(unsigned )?(\S+)\s+(.+)/) {
			my $type = $1 . $2; $symb = $3;
			foreach $tok (split (/,\s*/, $symb)) {
				if ($tok =~ /(\w+)\s*\[(.*)\]/) {
					$elem = addNameElem($parent, 'field', $1, "");
					$elem->setAttribute('array_len', "$2");
				} elsif ($tok =~ /(\w+)\s*\:\s*(\d+)/) {
					$elem = addNameElem($parent, 'field', $1, "");
					$elem->setAttribute('bits', "$2");
				} else {
					$elem = addNameElem($parent, 'field', $tok, "");
				}
				$elem->setAttribute('type', "$type");

				if ($access ne $defaultaccess) {
					$elem->setAttribute('access', "$access");
				}
			}
		} else {
			die "$field\n";
		}
	}
}

sub addFuncElems
{
	my ($obj_el, $inst, $prefix);

	$fcnt = keys(%fdefs);

	foreach $mname (sort (keys (%fdefs))) {
		next if ($mname =~ /^_/);
		$obj_el = "";
		$prefix = $mname;
		$prepend = undef;
		while ($prefix =~ /(\w+)_/) {
			$prefix = $key = $1;
			$key =~ s/_//g;
			# FIXME: lame Gdk API hack
			if ($key eq "gdkdraw") {
				$key = "gdkdrawable";
				$prepend = "draw_";
			}
			if (exists ($elem_table{$key})) {
				$prefix .= "_";
				$obj_el = $elem_table{$key};
				$inst = $key;
				last;
			} elsif (exists ($enums{$key}) && ($mname =~ /_get_type/)) {
				delete $fdefs{$mname};
				last;
			}
		}
		next if (!$obj_el);

		$mdef = delete $fdefs{$mname};
		
		if ($mname =~ /$prefix(new)/) {
			$el = addNameElem($obj_el, 'constructor', $mname);
			if ($mdef =~ /^deprecated/) {
				$el->setAttribute("deprecated", "1");
				$mdef =~ s/deprecated//g;
			}
			$drop_1st = 0;
		} else {
			$el = addNameElem($obj_el, 'method', $mname, $prefix, $prepend);
			if ($mdef =~ /^deprecated/) {
				$el->setAttribute("deprecated", "1");
				$mdef =~ s/deprecated//g;
			}
			$mdef =~ /(.*?)\w+\s*\(/;
			addReturnElem($el, $1);
			$mdef =~ /\(\s*(const)?\s*(\w+)/;
			if (lc($2) ne $inst) {
				$el->setAttribute("shared", "true");
				$drop_1st = 0;
			} else {
				$drop_1st = 1;
			}
		}

		parseParms ($el, $mdef, $drop_1st);

		# Don't add "free" to this regexp; that will wrongly catch all boxed types
		if ($mname =~ /$prefix(new|destroy|ref|unref)/ &&
		    ($obj_el->nodeName eq "boxed" || $obj_el->nodeName eq "struct") &&
		    $obj_el->getAttribute("opaque") ne "true") {
			$obj_el->setAttribute("opaque", "true");
			for my $field ($obj_el->getElementsByTagName("field")) {
				if (!$field->getAttribute("access")) {
					$field->setAttribute("access", "public");
					$field->setAttribute("writeable", "true");
				}
			}
		}
	}
}

sub parseParms
{
	my ($el, $mdef, $drop_1st) = @_;

	$fmt_args = 0;

	if ($mdef =~ /G_GNUC_PRINTF.*\((\d+,\s*\d+)\s*\)/) {
		$fmt_args = $1;
		$mdef =~ s/\s*G_GNUC_PRINTF.*\)//;
	}

	if (($mdef =~ /\((.*)\)/) && ($1 ne "void")) {
		@parms = ();
		$parm = "";
		$pcnt = 0;
		foreach $char (split(//, $1)) {
			if ($char eq "(") {
				$pcnt++;
			} elsif ($char eq ")") {
				$pcnt--;
			} elsif (($pcnt == 0) && ($char eq ",")) {
				@parms = (@parms, $parm);
				$parm = "";
				next;
			}
			$parm .= $char;
		}

		if ($parm) {
			@parms = (@parms, $parm);
		}
		# @parms = split(/,/, $1);
		($dump, @parms) = @parms if $drop_1st;
		if (@parms > 0) {
			addParamsElem($el, @parms);
		}

		if ($fmt_args != 0) {
			$fmt_args =~ /(\d+),\s*(\d+)/;
			$fmt = $1; $args = $2;
			($params_el, @junk) = $el->getElementsByTagName ("parameters");
			(@params) = $params_el->getElementsByTagName ("parameter");
			$offset = 1 + $drop_1st;
			$params[$fmt-$offset]->setAttribute ("printf_format", "true");
			$params[$args-$offset]->setAttribute ("printf_format_args", "true");
		}
	}
}

sub addStaticFuncElems
{
	my ($global_el, $ns_prefix);

	@mnames = sort (keys (%fdefs));
	$mcount = @mnames;

	return if ($mcount == 0);

	$ns_prefix = "";
	$global_el = "";

	for ($i = 0; $i < $mcount; $i++) {
		$mname = $mnames[$i];
		$prefix = $mname;
		next if ($prefix =~ /^_/);

		if ($ns_prefix eq "") {
			my (@toks) = split(/_/, $prefix);
			for ($j = 0; $j < @toks; $j++) {
				if (join ("", @toks[0 .. $j]) eq lc($ns)) {
					$ns_prefix = join ("_", @toks[0 .. $j]);
					last;
				}
			}
			next if ($ns_prefix eq "");
		}
		next if ($mname !~ /^$ns_prefix/);

		if ($mname =~ /($ns_prefix)_([a-zA-Z]+)_\w+/) {
			$classname = $2;
			$key = $prefix = $1 . "_" . $2 . "_";
			$key =~ s/_//g;
			$cnt = 1;
			if (exists ($enums{$key})) {
				$cnt = 1; 
			} elsif ($classname ne "set" && $classname ne "get" &&
			    $classname ne "scan" && $classname ne "find" &&
			    $classname ne "add" && $classname ne "remove" &&
			    $classname ne "free" && $classname ne "register" &&
			    $classname ne "execute" && $classname ne "show" &&
			    $classname ne "parse" && $classname ne "paint" &&
			    $classname ne "string") {
				while ($mnames[$i+$cnt] =~ /$prefix/) { $cnt++; }
			}
			if ($cnt == 1) {
				$mdef = delete $fdefs{$mname};
				
				if (!$global_el) {
					$global_el = $doc->createElement('class');
					$global_el->setAttribute('name', "Global");
					$global_el->setAttribute('cname', $ns . "Global");
					$ns_elem->appendChild($global_el);
				}
				$el = addNameElem($global_el, 'method', $mname, $ns_prefix);
				if ($mdef =~ /^deprecated/) {
					$el->setAttribute("deprecated", "1");
					$mdef =~ s/deprecated//g;
				}
				$mdef =~ /(.*?)\w+\s*\(/;
				addReturnElem($el, $1);
				$el->setAttribute("shared", "true");
				parseParms ($el, $mdef, 0);
				next;
			} else {
				$class_el = $doc->createElement('class');
				$class_el->setAttribute('name', StudlyCaps($classname));
				$class_el->setAttribute('cname', StudlyCaps($prefix));
				$ns_elem->appendChild($class_el);

				for ($j = 0; $j < $cnt; $j++) {
					$mdef = delete $fdefs{$mnames[$i+$j]};
					
					$el = addNameElem($class_el, 'method', $mnames[$i+$j], $prefix);
					if ($mdef =~ /^deprecated/) {
						$el->setAttribute("deprecated", "1");
						$mdef =~ s/deprecated//g;
					}
					$mdef =~ /(.*?)\w+\s*\(/;
					addReturnElem($el, $1);
					$el->setAttribute("shared", "true");
					parseParms ($el, $mdef, 0);
				}
				$i += ($cnt - 1);
				next;
			}
		}
	}
}

sub addNameElem
{
	my ($node, $type, $cname, $prefix, $prepend) = @_;

	my $elem = $doc->createElement($type);
	$node->appendChild($elem);
	if (defined $prefix) {
		my $match;
		if ($cname =~ /$prefix(\w+)/) {
			$match = $1;
		} else {
			$match = $cname;
		}
		if ($prepend) {
			$name = $prepend . $match;
		} else {
			$name = $match;
		}
		$elem->setAttribute('name', StudlyCaps($name));
	}
	if ($cname) {
		$elem->setAttribute('cname', $cname);
	}
	return $elem;
}

sub addParamsElem
{
	my ($parent, @params) = @_;

	my $parms_elem = $doc->createElement('parameters');
	$parent->appendChild($parms_elem);
	my $parm_num = 0;
	foreach $parm (@params) {
		$parm_num++;
		$parm =~ s/\s+(\*+)/\1 /g;
		my $out = $parm =~ s/G_CONST_RETURN/const/g;
		$parm =~ s/(const\s+)?(\w+)\*\s+const\*/const \2\*/g;
		$parm =~ s/(\*+)\s*const\s+/\1 /g;
		$parm =~ s/(\w+)\s+const\s*\*/const \1\*/g;
		$parm =~ s/const\s+/const-/g;
		$parm =~ s/unsigned\s+/unsigned-/g;
		if ($parm =~ /(.*)\(\s*\**\s*(\w+)\)\s+\((.*)\)/) {
			my $ret = $1; my $cbn = $2; my $params = $3;
			my $type = $parent->getAttribute('name') . StudlyCaps($cbn);
			$cb_elem = addNameElem($ns_elem, 'callback', $type, $ns);
			addReturnElem($cb_elem, $ret);
			if ($params && ($params ne "void")) {
				addParamsElem($cb_elem, split(/,/, $params));
				my $data_parm = $cb_elem->lastChild()->lastChild();
				if ($data_parm && $data_parm->getAttribute('type') eq "gpointer") {
				    $data_parm->setAttribute('name', 'data');
				}
			}
			$parm_elem = $doc->createElement('parameter');
			$parm_elem->setAttribute('type', $type);
			$parm_elem->setAttribute('name', $cbn);
			$parms_elem->appendChild($parm_elem);
			next;
		} elsif ($parm =~ /\.\.\./) {
			$parm_elem = $doc->createElement('parameter');
			$parms_elem->appendChild($parm_elem);
			$parm_elem->setAttribute('ellipsis', 'true');
			next;
		}
		$parm_elem = $doc->createElement('parameter');
		$parms_elem->appendChild($parm_elem);
		my $name = "";
		if ($parm =~ /struct\s+(\S+)\s+(\S+)/) {
			$parm_elem->setAttribute('type', $1);
			$name = $2;
		}elsif ($parm =~ /(unsigned )?(\S+)\s+(\S+)/) {
			$parm_elem->setAttribute('type', $1 . $2);
			$name = $3;
		} elsif ($parm =~ /(\w+\*)(\w+)/) {
			$parm_elem->setAttribute('type', $1);
			$name = $2;
		} elsif ($parm =~ /(\S+)/) {
			$parm_elem->setAttribute('type', $1);
			$name = "arg" . $parm_num;
		}
		if ($name =~ /(\w+)\[.*\]/) {
			$name = $1;
			$parm_elem->setAttribute('array', "true");
		}
		if ($out) {
			$parm_elem->setAttribute('pass_as', "out");
		}
		$parm_elem->setAttribute('name', $name);
	}
}

sub addReturnElem
{
	my ($parent, $ret) = @_;

	$ret =~ s/(\w+)\s+const\s*\*/const \1\*/g;
	$ret =~ s/const|G_CONST_RETURN/const-/g;
	$ret =~ s/\s+//g;
	$ret =~ s/(const-)?(\w+)\*(const-)\*/const-\2\*\*/g;
	my $ret_elem = $doc->createElement('return-type');
	$parent->appendChild($ret_elem);
	$ret_elem->setAttribute('type', $ret);
	if ($parent->getAttribute('name') eq "Copy" && $ret =~ /\*$/) {
		$ret_elem->setAttribute('owned', 'true');
	}
	return $ret_elem;
}

sub addPropElem
{
	my ($spec, $node, $is_child) = @_;
	my ($name, $mode, $docs);
	$spec =~ /g_param_spec_(\w+)\s*\((.*)\s*\)\s*\)/;
	my $type = $1;
	my @params = split(/,/, $2);

	$name = $params[0];
	if ($defines{$name}) {
		$name = $defines{$name};
	} else {
		$name =~ s/\s*\"//g;
	}

	$mode = $params[$#params];

	if ($type =~ /boolean|float|double|^u?int|pointer|unichar/) {
		$type = "g$type";
	} elsif ($type =~ /string/) {
		$type = "gchar*";
	} elsif ($type =~ /boxed|object/) {
		$type = $params[$#params-1];
		$type =~ s/TYPE_//;
		$type =~ s/\s+//g;
		$type = StudlyCaps(lc($type));
	} elsif ($type =~ /enum|flags/) {
		$type = $params[$#params-2];
		$type =~ s/TYPE_//;
		$type =~ s/\s+//g;
		$type = StudlyCaps(lc($type));
	}

	$prop_elem = $doc->createElement($is_child ? "childprop" : "property");
	$node->appendChild($prop_elem);
	$prop_elem->setAttribute('name', StudlyCaps($name));
	$prop_elem->setAttribute('cname', $name);
	$prop_elem->setAttribute('type', $type);

	$prop_elem->setAttribute('readable', "true") if ($mode =~ /READ/);
	$prop_elem->setAttribute('writeable', "true") if ($mode =~ /WRIT/);
	$prop_elem->setAttribute('construct', "true") if ($mode =~ /CONSTRUCT(?!_)/);
	$prop_elem->setAttribute('construct-only', "true") if ($mode =~ /CONSTRUCT_ONLY/);
}

sub parseTypeToken
{
	my ($tok) = @_;

	if ($tok =~ /G_TYPE_(\w+)/) {
		my $type = $1;
		if ($type eq "NONE") {
			return "void";
		} elsif ($type eq "INT") {
			return "gint32";
		} elsif ($type eq "UINT") {
			return "guint32";
		} elsif ($type eq "ENUM" || $type eq "FLAGS") {
			return "gint32";
		} elsif ($type eq "STRING") {
			return "gchar*";
		} elsif ($type eq "OBJECT") {
			return "GObject*";
		} else {
			return "g" . lc ($type);
		}
	} else {
		$tok =~ s/_TYPE//; 
		$tok =~ s/\|.*STATIC_SCOPE//; 
		$tok =~ s/\W+//g;
		return StudlyCaps (lc($tok));
	}
}

sub addSignalElem
{
	my ($spec, $class, $node) = @_;
	$spec =~ s/\n\s*//g; $class =~ s/\n\s*//g;

	$sig_elem = $doc->createElement('signal');
	$node->appendChild($sig_elem);

	if ($spec =~ /\(\"([\w\-]+)\"/) {
		$sig_elem->setAttribute('name', StudlyCaps($1));
		$sig_elem->setAttribute('cname', $1);
	}
	$sig_elem->setAttribute('when', $1) if ($spec =~ /_RUN_(\w+)/);

	$sig_elem->setAttribute('manual', 'true') if ($spec =~ /G_TYPE_POINTER/);
	if ($spec =~ /_OFFSET\s*\(\w+,\s*(\w+)\)/) {
		my $method = $1;
		$sig_elem->setAttribute('field_name', $method);

		if ($class =~ /;\s*(\/\*< (public|protected|private) >\s*\*\/)?(G_CONST_RETURN\s+)?(\w+\s*\**)\s*\(\s*\*\s*$method\)\s*\((.*?)\);/) {
			$ret = $4; $parms = $5;
			addReturnElem($sig_elem, $ret);
			if ($parms && ($parms ne "void")) {
				my ($dump, @parm_arr) = split (/,/, $parms);
				addParamsElem($sig_elem, @parm_arr);
			}
			return $method;
	} else {
			die "ERROR: Failed to parse method $method from class definition:\n$class";
		}
	} else {
		@args = split(/,/, $spec);
		my $rettype = parseTypeToken ($args[7]);
		addReturnElem($sig_elem, $rettype);
		$parmcnt = $args[8];
		$parmcnt =~ s/.*(\d+).*/\1/;
		$parms_elem = $doc->createElement('parameters');
		$sig_elem->appendChild($parms_elem);

		for (my $idx = 0; $idx < $parmcnt; $idx++) {
			my $argtype = parseTypeToken ($args[9+$idx]);
			$parm_elem = $doc->createElement('parameter');
			$parms_elem->appendChild($parm_elem);
			$parm_elem->setAttribute('name', "p$idx");
			$parm_elem->setAttribute('type', $argtype);
		}
		return "";
	}
		}

sub addImplementsElem
{
	my ($spec, $node) = @_;
	$spec =~ s/\n\s*//g; 
	if ($spec =~ /,\s*(\w+)_TYPE_(\w+),/) {
		$impl_elem = $doc->createElement('interface');
		$name = StudlyCaps (lc ("$1_$2"));
		$impl_elem->setAttribute ("cname", "$name");
		$node->appendChild($impl_elem);
	}
}


sub parseInitFunc
{
	my ($obj_el, $initfunc, $classdef) = @_;

	my @init_lines = split (/\n/, $initfunc);
	my @signal_vms = ();

	my $linenum = 0;
	while ($linenum < @init_lines) {

		my $line = $init_lines[$linenum];
			
		if ($line =~ /#define/) {
			# FIXME: This ignores the bool helper macro thingie.
		} elsif ($line =~ /g_object_(class|interface)_install_prop/) {
			my $prop = $line;
			while ($prop !~ /\)\s*;/) {
				$prop .= $init_lines[++$linenum];
			}
			addPropElem ($prop, $obj_el, 0);
			$propcnt++;
		} elsif ($line =~ /gtk_container_class_install_child_property/) {
			my $prop = $line;
			do {
				$prop .= $init_lines[++$linenum];
			} until ($init_lines[$linenum] =~ /\)\s*;/);
			addPropElem ($prop, $obj_el, 1);
			$childpropcnt++;
		} elsif ($line =~ /\bg.*_signal_new/) {
			my $sig = $line;
			do {
				$sig .= $init_lines[++$linenum];
			} until ($init_lines[$linenum] =~ /;/);
			$signal_vm = addSignalElem ($sig, $classdef, $obj_el);
			push (@signal_vms, $signal_vm) if $signal_vm;
			$sigcnt++;
		}
		$linenum++;
	}
	return @signal_vms;
}

sub parseTypeFuncMacro
{
	my ($obj_el, $typefunc) = @_;

	$impls_node = undef;
	while ($typefunc =~ /G_IMPLEMENT_INTERFACE\s*\(\s*(\w+)/) {
		$iface = $1;
		if (not $impls_node) {
			$impls_node = $doc->createElement ("implements");
			$obj_el->appendChild ($impls_node);
		}
		addImplementsElem ($prop, $impl_node);
		if ($iface =~ /(\w+)_TYPE_(\w+)/) {
			$impl_elem = $doc->createElement('interface');
			$name = StudlyCaps (lc ("$1_$2"));
			$impl_elem->setAttribute ("cname", "$name");
			$impls_node->appendChild($impl_elem);
		}
		$typefunc =~ s/G_IMPLEMENT_INTERFACE\s*\(.*?\)//;
	}
}

sub parseTypeFunc
{
	my ($obj_el, $typefunc) = @_;

	my @type_lines = split (/\n/, $typefunc);

	my $linenum = 0;
	$impl_node = undef;
	while ($linenum < @type_lines) {

		my $line = $type_lines[$linenum];
			
		if ($line =~ /#define/) {
			# FIXME: This ignores the bool helper macro thingie.
		} elsif ($line =~ /g_type_add_interface_static/) {
			my $prop = $line;
			do {
				$prop .= $type_lines[++$linenum];
			} until ($type_lines[$linenum] =~ /;/);
			if (not $impl_node) {
				$impl_node = $doc->createElement ("implements");
				$obj_el->appendChild ($impl_node);
			}
			addImplementsElem ($prop, $impl_node);
		}
		$linenum++;
	}
}

##############################################################
# Converts a dash or underscore separated name to StudlyCaps.
##############################################################

sub StudlyCaps
{
	my ($symb) = @_;
	$symb =~ s/^([a-z])/\u\1/;
	$symb =~ s/^(\d)/\1_/;
	$symb =~ s/[-_]([a-z])/\u\1/g;
	$symb =~ s/[-_](\d)/\1/g;
	$symb =~ s/^2/Two/;
	$symb =~ s/^3/Three/;
	return $symb;
}

