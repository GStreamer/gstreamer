#!/usr/bin/perl

# extract code fragments from xml program listings
# first argument: source code file to find
# second argument: xml files to extract code from

# main

# decodes xml by translating &amp; &lt; &gt; back to what they should be
sub
xml_decode ($)
{
  my $input = shift;

  $input =~ s/\&amp;/&/g;
  $input =~ s/&lt;/</g;
  $input =~ s/&gt;/>/g;

  return $input;
}

# main
my $output = shift @ARGV;

foreach $file (@ARGV)
{
  open FILE, $file or die "Cannot open file $file";

  while ($line = <FILE>)
  {
    if ($line =~ /\/\* example-begin $output \*\//)
    {
      print "Extracting $output from $file\n";
      open OUTPUT, ">$output";
      print OUTPUT xml_decode ($line); 
      my $example = 1;
      while (($line = <FILE>) && $example)
      {
        if ($line =~ /\/\* example-end/)
        {
          print OUTPUT xml_decode ($line);
          close OUTPUT;
          $example = 0;
        }
        else
        {
          print OUTPUT xml_decode ($line);
        }
      }
    }
  }
}
