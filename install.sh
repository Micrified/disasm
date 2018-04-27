#!/bin/bash

# Create lib sub-directory.
mkdir lib

# Move to lib.
cd lib

# Clone Repositories.
git clone https://github.com/intelxed/xed.git xed
a=$?
git clone https://github.com/intelxed/mbuild.git mbuild
b=$?

# Verify success.
echo Cloning Repositories ... 
if [ $a -ne 0 ] || [ $b -ne 0 ] ; then
    echo Failed to clone. Exiting...
    exit 1
fi

# Build project.
cd xed
./mfile.py install

# Provide final instructions.
echo Done.
echo Install headers with:    cp -r lib/xed/kits/<install-base-name>/include/xed /usr/local/include/xed
echo Install static lib with: cp -r lib/xed/kits/<install-base-name>/lib/ /usr/local/lib/
echo Don't forget to link with -xed when using this library.
