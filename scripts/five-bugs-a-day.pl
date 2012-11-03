#!/usr/bin/env perl
#
# ----------------------------------------------------------------------------
#
# five-bugs-a-day.pl
#
# Little script that outputs a list of N random open bugs from bugzilla
# for a certain bugzilla product
#
# ----------------------------------------------------------------------------
#
# Copyright (C) 2011-2012 Tim-Philipp Muller <tim centricular net>
#
# This script is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
#
# ----------------------------------------------------------------------------
#
# You can use it to send yourself an e-mail with a few bugs to check up on
# every day, just put it into your crontab on a computer of your choice
# (with proper e-mail forwarding configured in some way):
#
# $ crontab -e
#
# add:
#
#   MAILTO=you@nowhere.org
#   # send ten random buglinks every day at 16.30
#   30 16 * * *  /usr/bin/perl  /path/to/five-bugs-a-day.pl
#
#
# Yes, it's PERL, sorry.
#
# Yes, I know the default is 10 bugs.
#
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
#    subroutines
# ----------------------------------------------------------------------------

sub shuffle
{
    my $array = shift;
    my $i = @$array;
    while ( --$i )
    {
        my $j = int rand( $i+1 );
        @$array[$i,$j] = @$array[$j,$i];
    }

    return @$array;
}

# ----------------------------------------------------------------------------
#    main
# ----------------------------------------------------------------------------

my $NUM_BUGS = 10;

my $PRODUCT = "GStreamer";

# ----- command line options -----

if (@ARGV) {
  $NUM_BUGS = shift @ARGV;
}

if (@ARGV) {
  $PRODUCT = shift @ARGV;
}


my $QUERY_URL = "https://bugzilla.gnome.org/buglist.cgi?product=$PRODUCT&" .
                'bug_status=UNCONFIRMED&bug_status=NEW&bug_status=ASSIGNED&' .
                'bug_status=REOPENED&' .
                'query_format=advanced&ctype=csv';

my $COL_ID;
my $COL_DESC;

my %BUGS;

# for testing/debugging:
# unless (@lines = `cat bugs.csv`) {
unless (@lines = `wget --no-check-certificate --quiet -O - '$QUERY_URL'`) {
  die 'Could not download bug list';
}

# ----- parse column headers -----

my $headers;

# get first line which contains the field names
$headers = shift @lines;

# get rid of newline at end
chop $headers;

my @fields = split (/,/, $headers);
my $num_fields = scalar(@fields);

for (my $c = 0; $c < $num_fields; $c++) {
  #print "$c $fields[$c] \n";
  if ($fields[$c] =~ m/bug_id/) {
    $COL_ID = $c;
  } elsif ($fields[$c] =~ m/short_desc/) {
    $COL_DESC = $c;
  }
}

die "Could not find bug_id column in CVS file" if not defined ($COL_ID);
die "Could not find short_desc column in CVS file" if not defined ($COL_DESC);

#print "bugid is column $COL_ID\n";
#print "desc  is column $COL_DESC\n";

foreach (@lines) {
  if (m/,/) {
    chop;

    # We specify num_fields as limit here, because the short_desc field
    # might contain commas as well, and we don't want it to get cut off.
    # This is a hack for the fact that we don't handle quoted fields
    # (12345,"UNCONFIRMED","foo, bar: errors out") properly. As long as the
    # short_desc field is the last one, that should be ok. (FIXME)
    my @vals = split (/,/, $_, $num_fields);
    my $id = $vals[$COL_ID];
    my $desc = $vals[$COL_DESC];

    $desc =~ s/^"(.*)"$/$1/;
    $BUGS{$id} = $desc;
  }
}

my @all_bugs = keys %BUGS;
my @bugs = shuffle (\@all_bugs);

# only want first NUM_BUGS bugs
@bugs = splice (@bugs, 0, $NUM_BUGS);

print "\n";
print "$NUM_BUGS random bugs:\n";
print "\n";

for my $bug_id ( @bugs ) {
  print "$BUGS{$bug_id}\n";
  print "https://bugzilla.gnome.org/show_bug.cgi?id=$bug_id\n";
  print "\n";
}

print "\n";
print "More bugs at:\n";
print "  - http://gstreamer.freedesktop.org/bugs/\n";
print "  - https://bugzilla.gnome.org/browse.cgi?product=GStreamer\n";
print "\n";
