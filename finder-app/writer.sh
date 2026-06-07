#!/bin/bash
# Bhumika Sood
# Write a file at the path specified with a string.

WRITE_FILE="$1"
WRITE_STRING="$2"

# Check number of command line arguments
if [ $# -ne 2 ]; then
    echo "ERROR: Invalid number of arguments. Total number of arguments should be 2."
    echo "The order of the arguments should be:"
    echo "1) File path to be written"
    echo "2) String to be written to the file specified"
    exit 1
fi

# Extract the directory path and make the directory if it doesn't exist
WRITE_DIRECTORY=$(dirname "$WRITE_FILE")
mkdir -p "$WRITE_DIRECTORY"

if [ ! -d "$WRITE_DIRECTORY" ]; then
    echo "ERROR: Could not create directory $WRITE_DIRECTORY"
    exit 1
fi

# Write a file at path specified and check if file is created successfully
touch $WRITE_FILE
if [ ! -f "$WRITE_FILE" ]; then
    echo "ERROR: Could not write file $WRITE_FILE"
    exit 1
fi

# Write string to file
echo $WRITE_STRING >> $WRITE_FILE
