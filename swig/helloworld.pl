#!/usr/bin/perl -w

use Gst;

Gst::init ();

my $bin = Gst::gst_pipeline_new ("pipeline")
  or die "Cannot create pipeline !\n";
print "DEBUG: bin: $bin\n";

my $file = shift || die "Please give a file to test !";

my $parse = Gst::gst_elementfactory_make ("mp3parse", "parse")
  or die "Cannot create mp3parse element !\n";

my $disksrc = Gst::gst_elementfactory_make ("disksrc", "disk_source")
  or die "Cannot create disksrc element !\n";
Gst::gobject_set ($disksrc, "location", $file);

my $decoder = Gst::gst_elementfactory_make ("mpg123", "decoder")
  or die "Cannot create decoder element !\n";
my $osssink = Gst::gst_elementfactory_make ("osssink", "play_audio")
  or die "Cannot create decoder element !\n";

Gst::wrap_gst_bin_add ($bin, $disksrc);
Gst::wrap_gst_bin_add ($bin, $parse);
Gst::wrap_gst_bin_add ($bin, $decoder);
Gst::wrap_gst_bin_add ($bin, $osssink);

Gst::gst_pad_connect (Gst::gst_element_get_pad ($disksrc, "src"),
		      Gst::gst_element_get_pad ($parse, "sink"));
Gst::gst_pad_connect (Gst::gst_element_get_pad ($parse, "src"),
		      Gst::gst_element_get_pad ($decoder, "sink"));
Gst::gst_pad_connect (Gst::gst_element_get_pad ($decoder, "src"),
		      Gst::gst_element_get_pad ($osssink, "sink"));
#print "DEBUG: disksrc: $disksrc\n";

#FIXME
#Gst::gst_element_set_state ($bin, GST_STATE_PLAYING);
Gst::gst_element_set_state_play ($bin);

my $playing = 1;

while ($playing)
{
  Gst::wrap_gst_bin_iterate ($bin);
}

