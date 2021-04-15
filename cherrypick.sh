#!/bin/bash

if [ -z "$1" ] 
then
  echo "You need to supply a URL to a patch file."
  exit
fi

URL=$1;

# Download a patch and apply it.
curl $URL | git apply -v --index
echo "Please add and commit"
