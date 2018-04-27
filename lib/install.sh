echo Cloning XED.
git clone https://github.com/intelxed/xed.git xed

echo Cloning mbuild.
git clone https://github.com/intelxed/mbuild.git mbuild

echo Building XED.
cd xed
./mfile.py

echo Installing headers...
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/include/xed /usr/local/include/xed

echo Installing static library.
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/lib/ /usr/local/lib/xed/

cd ..
echo Done. Remember to run programs by linking with: -lxed

