#!/bin/bash
# Bhumika Sood
# Find string in files of specified directory.

FILES_DIR="$1"
SEARCH_STRING="$2"

# Check number of command line arguments
if [ $# -ne 2 ]; then
    echo "ERROR: Invalid number of arguments. Total number of arguments should be 2."
    echo "The order of the arguments should be:"
    echo "1) File directory path"
    echo "2) String to be searched"
    exit 1
fi

# Check if $FILES_DIR is a directory that exists on the file system
if [ ! -d "$FILES_DIR" ]; then
    echo "ERROR: Invalid file path. $FILES_DIR is not a directory."
    exit 1
fi

# Search for $SEARCH_STRING in #FILES_DIR and count number of files and occurences
NUM_OCCURENCES=$(grep -ro ${SEARCH_STRING} ${FILES_DIR} | wc -l)
NUM_FILES=$(grep -rl ${SEARCH_STRING} ${FILES_DIR} | wc -l)

# Return number of files and occurences of search string
echo "The number of files are $NUM_FILES and the number of matching lines are $NUM_OCCURENCES"