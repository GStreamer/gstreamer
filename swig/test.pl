#!/usr/bin/perl -w

use Gst;

Gst::init ();

my $bin = Gst::gst_pipeline_new ("pipeline")
  or die "Cannot create pipeline !\n";
print "DEBUG: bin: $bin\n";

my $parse = Gst::gst_elementfactory_make ("mp3parse", "parse")
  or die "Cannot create mp3parse element !\n";
my $disksrc = Gst::gst_elementfactory_make ("disksrc", "disk_source")
  or die "Cannot create disksrc element !\n";
print "DEBUG: disksrc: $disksrc\n";
Gst::gobject_set ($disksrc, "location", $ARGV[1]);
