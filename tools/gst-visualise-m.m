#!/usr/bin/perl -w

# launch a gst-launch pipeline to display a visualisation of the 
# input audio.
# make use of default input srcs.
# visualisation plugin is specified on command line.

### packages

use File::Basename;


my (%pipes, %cfg);

sub read_config
{
  my $config_file = `echo -n ~`."/.gst";
  if (-e $config_file)
  {
    open CONFIG, $config_file;
    while (<CONFIG>)
    {
      chomp;
      s/#.*//;
      s/\s+$//;
      next unless length;
      my ($var, $value) = split (/\s*=\s*/, $_, 2);
      $cfg{$var} = $value;
    }
    if (!($cfg{AUDIOSRC}))
    {
      print "Please add an AUDIOSRC to $config_file !\n";
    }
    if (!($cfg{VIDEOSINK}))
    {
      print "Please add a VIDEOSINK to $config_file !\n";
    }
  }
  else
  {
    print "No configuration file $config_file found.  You might want to create one.\n";
  }
  if (!defined $cfg{AUDIOSRC})   { $cfg{AUDIOSRC} = "esdmon"; }
  if (!defined $cfg{VIDEOSINK})  { $cfg{VIDEOSINK} = "sdlvideosink"; }
  if (!defined $cfg{CVS_PATH})   { $cfg{CVS_PATH} =  `echo -n ~`."/gst/cvs"; }
}

sub visualise(@)
{
    my $vis = $cfg{VISUALIZER};
    $vis = shift() if ($#_ != -1);
    $vis = "goom" unless $vis;

    my $pipe;
    $pipe = $vis unless $pipe = $pipes{$vis};

    $command = "gst-launch-@GST_MAJORMINOR@ $cfg{AUDIOSRC} ! $pipe ! { queue ! colorspace ! $cfg{VIDEOSINK} }";
    print "Running $command\n";
    system ("PATH=\$PATH:".$cfg{CVS_PATH}."/gstreamer/tools $command");
}

### main

read_config ();

%pipes = ( 
  "goom", "goom",
  "chart", "stereo2mono ! chart",
  "synaesthesia", "synaesthesia",
  "monoscope", "stereo2mono ! monoscope"
);

if ($#ARGV > 0) {
    print STDERR "Usage: gst-visualise [visualiser]\n";
    exit 1;
}

visualise(@ARGV);

