#!/bin/sh

if [ $# -ne 2 ]; then
    echo "Error: Invalid number of arguments."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile=$1
writestr=$2

writedir=$(dirname "$writefile")
mkdir -p "$writedir"

if ! echo "$writestr" > "$writefile"; then
    echo "Error: Could not create or write to file '$writefile'."
    exit 1
fi

exit 0
