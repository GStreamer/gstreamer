#!/bin/bash

# copies from gstreamer filter boilerplate
# to new filter
# changing file names and function references

# thomas@apestaart.org

# modified 23 aug 2001 apwingo@eos.ncsu.edu:
# conform better to gtk naming conventions (GstFoo->gst_foo in functions, etc)

if [ "$1" = "" ]
then
  echo "please specify the filter to copy FROM (e.g. Passthrough)"
  exit
fi

if [ "$2" = "" ]
then
  echo "please specify the filter to copy TO (e.g. NewFilter)"
  exit
fi

FROM=$1
TO=$2
FROM_LC=`echo $FROM | tr [A-Z] [a-z]`
TO_LC=`echo $TO | tr [A-Z] [a-z]`
FROM_UC=`echo $FROM | tr [a-z] [A-Z]`
TO_UC=`echo $TO | tr [a-z] [A-Z]`
FROM_LC_UNDERSCORE=`echo $FROM | perl -n -p -e 's/([a-z])([A-Z])/$1_$2/g; tr/A-Z/a-z/'`
TO_LC_UNDERSCORE=`echo $TO | perl -n -p -e 's/([a-z])([A-Z])/$1_$2/g; tr/A-Z/a-z/'`

echo "Copying filter boilerplate $FROM to new filter $TO..."

if [ ! -d $FROM_LC ]
then
  echo "Filter directory $FROM_LC does not exist !"
  exit
fi

if [ -d $TO_LC ]
then
  echo "Filter directory $TO_LC already exists !"
  exit
fi

cp  -r $FROM_LC $TO_LC

cd $TO_LC

for a in *$FROM_LC*; do mv $a `echo $a | sed s/$FROM_LC/$TO_LC/g`; done

perl -i -p -e "s/$FROM/$TO/g" *
perl -i -p -e "s/${FROM_LC_UNDERSCORE}_/${TO_LC_UNDERSCORE}_/g" *
perl -i -p -e "s/$FROM_LC/$TO_LC/g" *
perl -i -p -e "s/$FROM_UC/$TO_UC/g" *

