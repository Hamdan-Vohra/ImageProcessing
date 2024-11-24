#!/bin/bash

# Set file extension for images
ext="png"  # Change this to your file extension

# Counter starts
counter=1200

# Loop through all files with the specified extension in the current directory
for file in *."$ext"; do
  # Check if file exists to prevent errors
  if [ -e "$file" ]; then
    # Rename the file to the new counter value with extension
    mv "$file" "$counter.$ext"
    ((counter++))  # Increment the counter
  fi
done

echo "Renaming complete. Files are now numbered from 1 to $((counter - 1))."
