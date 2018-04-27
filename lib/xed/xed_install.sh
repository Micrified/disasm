echo Building...
./mfile.py install

echo Installing headers...
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/include/xed /usr/local/include/xed

echo Installing static library.
sudo cp -r kits/xed-install-base-2018-04-26-lin-x86-64/lib/ /usr/local/lib/xed/

echo Done. Remember to run programs by linking with: -lxed