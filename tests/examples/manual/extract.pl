#!/usr/bin/perl

# extract code fragments from xml program listings
# first argument: source code file to find
# second argument: xml files to extract code from

# main

# decodes xml by translating &amp; &lt; &gt; back to what they should be
# and also ignore
# <![CDATA[ and ]]> and <!-- and -->
sub
xml_decode ($)
{
  my $input = shift;

  $input =~ s/\&amp;/&/g;
  $input =~ s/&lt;/</g;
  $input =~ s/&gt;/>/g;

  if ($input =~ /<!\[CDATA\[/) { $input = ""; }
  if ($input =~ /]]>/) { $input = ""; }
  if ($input =~ /<!--/) { $input = ""; }
  if ($input =~ /-->/) { $input = ""; }

  #print "Returning line $input";
  return $input;
}

# main
my $output = shift @ARGV;
my $outputname;

# strip path parts
if ($output =~ m/.*\/(.*)$/)
{
  $outputname = $1;
}
else
{
  $outputname = $output;
}

$found = 0;
%blocks = ();

foreach $file (@ARGV)
{
  open FILE, $file or die "Cannot open file $file";

  while ($line = <FILE>)
  {
    if ($line =~ /<!-- example-begin $outputname (.*?)-->/)
    {
      $found = 1;
      $block_id = $1;
      $block = "\n/*** block $block_id from $file ***/\n";

      print "Extracting $outputname block $block_id from $file\n";

      while ($line = <FILE>)
      {
        if ($line =~ /<!-- example-end $outputname (.*?)-->/)
        {
          last;
        }
        $block .= xml_decode ($line);
      }
      $blocks{$block_id} = $block;
    }
  }
}


if (!$found)
{
  print "Could not find $outputname example !\n";
  exit(1);
}

# now output all the blocks in the right order
open OUTPUT, ">$output";
@block_ids = keys %blocks;
foreach $block_id (sort @block_ids)
{
  print "Writing $output block $block_id\n";
  print OUTPUT $blocks{$block_id};
}
close OUTPUT;
