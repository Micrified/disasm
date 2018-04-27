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

# Prompt for full installation.
echo -n "Install headers and static library (y/n)? "
read answer

if [ "$answer" != "${answer#[Yy]}" ] ;then
    echo Okay.
else
    echo Exiting...
    exit 0
fi

# Install headers.
echo Installing headers...
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/include/xed /usr/local/include/xed

# Install static library.
echo Installing static library.
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/lib/ /usr/local/lib/xed/

# Finished.
echo Done.
