#!/usr/bin/perl
#
# Copyright (C) 2006 Daniel Berrange
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


print <<EOF;
<html>
<head>
<title>Coverage report for $ARGV[0]</title>
<style type="text/css">
          span.perfect {
            background: rgb(0,255,0);
          }
          span.terrible {
            background: rgb(255,0,0);
          }
</style>
</head>
<body>
<h1>Coverage report for $ARGV[0]</h1>

<pre>
EOF


while (<>) {
    s/&/&amp;/g;
    s/</&lt;/g;
    s/>/&gt;/g;

    if (/^\s*function (\S+) called (\d+) returned \d+% blocks executed \d+%/) {
	my $class = $2 > 0 ? "perfect" : "terrible";
	$_ = "<span class=\"$class\" id=\"" . $1 . "\">$_</span>";
    } elsif (/^\s*branch\s+\d+\s+taken\s+(\d+)%\s+.*$/) {
	my $class = $1 > 0 ? "perfect" : "terrible";
	$_ = "<span class=\"$class\">$_</span>";
    } elsif (/^\s*branch\s+\d+\s+never executed.*$/) {
	my $class = "terrible";
	$_ = "<span class=\"$class\">$_</span>";
    } elsif (/^\s*call\s+\d+\s+never executed.*$/) {
	my $class = "terrible";
	$_ = "<span class=\"$class\">$_</span>";
    } elsif (/^\s*call\s+\d+\s+returned\s+(\d+)%.*$/) {
	my $class = $1 > 0 ? "perfect" : "terrible";
	$_ = "<span class=\"$class\">$_</span>";
    }
    

    print;
}

print <<EOF;
</pre>
</body>
</html>
EOF
