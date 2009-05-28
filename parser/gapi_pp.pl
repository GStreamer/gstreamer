#!/usr/bin/perl
#
# gapi_pp.pl : A source preprocessor for the extraction of API info from a
#	       C library source directory.
#
# Authors: Mike Kestner <mkestner@speakeasy.net>
# 	   Martin Willemoes Hansen <mwh@sysrq.dk>
#
# Copyright (c) 2001 Mike Kestner
# Copyright (c) 2003 Martin Willemoes Hansen
# Copyright (c) 2003-2004 Novell, Inc.
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

$private_regex = '^#if.*(ENABLE_BACKEND|ENABLE_ENGINE)';
$eatit_regex = '^#if(.*(__cplusplus|DEBUG|DISABLE_COMPAT|ENABLE_BROKEN)|\s+0\s*$)';
$ignoreit_regex = '^\s+\*|#ident|#error|#\s*include|#\s*else|#\s*undef|G_(BEGIN|END)_DECLS|GDKVAR|GTKVAR|GTKMAIN_C_VAR|GTKTYPEUTILS_VAR|VARIABLE|GTKTYPEBUILTIN';

foreach $arg (@ARGV) {
	if (-d $arg && -e $arg) {
		@hdrs = (@hdrs, `ls $arg/*.h`);
		@srcs = (@srcs, `ls $arg/*.c`);
	} elsif (-f $arg && -e $arg) {
		@hdrs = (@hdrs, $arg) if ($arg =~ /\.h$/);
		@srcs = (@srcs, $arg) if ($arg =~ /\.c$/);
	} else {
		die "unable to process arg: $arg";
	}
}

foreach $fname (@hdrs) {

	if ($fname =~ /test|private|internals|gtktextlayout|gtkmarshalers/) {
		@privhdrs = (@privhdrs, $fname);
		next;
	}

	open(INFILE, $fname) || die "Could open $fname\n";

	$braces = 0;
	$deprecated = -1;
	$ifdeflevel = 0;
	$prepend = "";
	while ($line = <INFILE>) {
		$braces++ if ($line =~ /{/ and $line !~ /}/);
		$braces-- if ($line =~ /}/ and $line !~ /{/);
		
		next if ($line =~ /$ignoreit_regex/);

		$line =~ s/\/\*[^<].*?\*\///g;
		
		next if ($line !~ /\S/);

		$line = $prepend . $line;
		$prepend = "";

		while ($line =~ /(.*)\\$/) { $line = $1 . <INFILE>; }

		if ($line =~ /#\s*define\s+\w+\s+\"/) {
			$def = $line;
			while ($def !~ /\".*\"/) {$def .= ($line = <INFILE>);}
			print $def;
		} elsif ($line =~ /#\s*define\s+\w+\s*\D+/) {
			$def = $line;
			while ($line =~ /\\\n/) {$def .= ($line = <INFILE>);}
			if ($def =~ /_CHECK_\w*CAST|INSTANCE_GET_INTERFACE/) {
				$def =~ s/\\\n//g;
				print $def;
			}
		} elsif ($line =~ /^\s*\/\*[^<]/) {
			while ($line !~ /\*\//) {$line = <INFILE>;}
		} elsif ($line =~ /^extern/) {
			while ($line !~ /;/) {$line = <INFILE>;}
		} elsif ($line =~ /^#ifndef\s+\w+_H_*\b/) {
			while ($line !~ /#define|#endif/) {$line = <INFILE>;}
		} elsif ($line =~ /$private_regex/) {
			$nested = 0;
			while ($line = <INFILE>) {
				last if (!$nested && ($line =~ /#else|#endif/));
				if ($line =~ /#if/) {
					$nested++;
				} elsif ($line =~ /#endif/) {
					$nested--
				}
				next if ($line !~ /^struct/);

				print "private$line";
				do {
					$line = <INFILE>;
					print $line;
				} until ($line =~ /^\}/);
			}
		} elsif ($line =~ /$eatit_regex/) {
			$nested = 0;
			while ($line = <INFILE>) {
				last if (!$nested && ($line =~ /#else|#endif/));
				if ($line =~ /#if/) {
					$nested++;
				} elsif ($line =~ /#endif/) {
					$nested--
				}
			}
		} elsif ($line =~ /^#\s*ifn?\s*\!?def/) {
			$ifdeflevel++;
			#print "#ifn?def ($ifdeflevel): $line\n";
			if ($line =~ /#ifndef.*DISABLE_DEPRECATED/) {
				$deprecated = $ifdeflevel;
			} elsif ($line =~ /#if !defined.*DISABLE_DEPRECATED/) {
				$deprecated = $ifdeflevel;
			}
		} elsif ($line =~ /^#\s*endif/) {
			#print "#endif   ($ifdeflevel): $line\n";
			if ($deprecated == $ifdeflevel) {
				$deprecated = -1;
			}
			$ifdeflevel--;
		} elsif ($line =~ /typedef struct\s*\{?\s*$/) {
			while ($line !~ /{/) {
				chomp ($line);
				$line .= <INFILE>;
			}
			my $first_line = $line;
			my @lines = ();
			$line = <INFILE>;
			while ($line !~ /^}\s*(\w+);/) {
				if ($line =~ /\(.*\).*\(/) {
					while ($line !~ /;/) {
						chomp ($line);
						$nxt = <INFILE>;
						$nxt =~ s/^\s+/ /;
						$line .= $nxt;
					}
				}
				push @lines, $line;
				$line = <INFILE>;
			}
			$line =~ /^}\s*(\w+);/;
			my $name = $1;
			print "typedef struct _$name $name;\n";
			print "struct _$name {\n";
			foreach $line (@lines) {
				if ($line =~ /(\s*.+\;)/) {
					$field = $1;
					$field =~ s/(\w+) const/const $1/;
					print "$field\n";
				}
			}
			print "};\n";
		} elsif ($line =~ /^enum\s+\{/) {
			while ($line !~ /^};/) {$line = <INFILE>;}
		} elsif ($line =~ /^(typedef\s+)?union/) {
			next if ($line =~ /^typedef\s+union\s+\w+\s+\w+;/);
			while ($line !~ /^};/) {$line = <INFILE>;}
		} elsif ($line =~ /(\s+)union\s*{/) {
			# this is a hack for now, but I need it for the fields to work
			$indent = $1;
			$do_print = 1;
			while ($line !~ /^$indent}\s*\w+;/) {
				$line = <INFILE>;
				next if ($line !~ /;/);
				print $line if $do_print;
				$do_print = 0;
			}
		} else {
			if ($braces or $line =~ /;|\/\*/) {
				if ($deprecated == -1) {
					print $line;
				} else {
					print "deprecated$line";
				}
			} else {
				$prepend = $line;
				$prepend =~ s/\n/ /g;
			}
		}
	}
}

foreach $fname (@srcs, @privhdrs) {

	open(INFILE, $fname) || die "Could open $fname\n";

	if ($fname =~ /builtins_ids/) {
		while ($line = <INFILE>) {
			next if ($line !~ /\{/);

			chomp($line);
			$builtin = "BUILTIN" . $line;
			$builtin .= <INFILE>;
			print $builtin;
		}
		next;
	}

	while ($line = <INFILE>) {
		next if ($line !~ /^(struct|\w+_class_init|\w+_base_init|\w+_get_type\b|G_DEFINE_TYPE_WITH_CODE)/);

		if ($line =~ /^G_DEFINE_TYPE_WITH_CODE/) {
			my $macro;
			my $parens = 0;
			do {
				chomp ($line);
				$line =~ s/(.*)\\$/\1/;
				$line =~ s/^\s+(.*)/ \1/;
				$macro .= $line;
				foreach $chr (split (//, $line)) {
					if ($chr eq "(") {
						$parens++;
					} elsif ($chr eq ")") {
						$parens--;
					}
				}
				$line = <INFILE>;
			} while ($parens > 0);
			print "$macro\n";
			next if ($line !~ /^(struct|\w+_class_init|\w+_base_init)/);
		}

		if ($line =~ /^struct/) {
			# need some of these to parse out parent types
			print "private";
			if ($line =~ /;/) {
				print $line;
				next;
			}
		}

		$comment = 0;
		$begin = 0;
		$end = 0;
		do {
			# Following ifs strips out // and /* */ C comments
			if ($line =~ /\/\*/) {
				$comment = 1;
				$begin = 1;
			}

			if ($comment != 1) {
				$line =~ s/\/\/.*//;
				print $line;
			}

			if ($line =~ /\*\//) {
				$comment = 0;
				$end = 1;
			}

			if ($begin == 1 && $end == 1) {
				$line =~ s/\/\*.*\*\///;
				print $line;
			}

			$begin = 0;
			$end = 0;
		} until (($line = <INFILE>) =~ /^}/);
		print $line;
	}
}

