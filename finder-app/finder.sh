#!/bin/sh
filesdir=$1
searchstr=$2

if [[ -z "$filesdir" || -z "$searchstr" ]]; then
    echo "Invalid Arguments"
    exit 1
fi

if [[ ! -d "$filesdir" ]]; then
    echo "not a directory"
    exit 1
fi

files=`find "$filesdir" -type f | wc -l`
count=`grep -rn "$searchstr" "$filesdir" | wc -l`
echo "The number of files are $files and the number of matching lines are $count"
