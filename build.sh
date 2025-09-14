#!/bin/bash

set -ex

# installed devkitpro via pacman: https://devkitpro.org/wiki/devkitPro_pacman
# copied /opt/ dir into externals
export DEVKITPRO=./externals/devkitpro
export DEVKITARM=./externals/devkitpro/devkitARM
export DEVKITPPC=./externals/devkitpro/devkitPPC

# build parent hoshi
#echo "building parent hoshi for hoshi.bin..."
#pushd externals/hoshi
#DEVKITPRO=../devkitpro DEVKITARM=../devkitpro/devkitARM DEVKITPPC=../devkitpro/devkitPPC BUILD=debug make all
#popd

# build hoshi AP mod
echo "building hoshi AP mods..."
make all

# extract iso
# https://github.com/JoshuaMKW/pyisotools
echo "extracting KAR iso..."
python -m pyisotools 'iso/Kirby Air Ride (USA).iso' E --dest="extracted-iso/"

# patch the default iso's main.dol with the entrypoint gecko codes for hoshi
# https://github.com/JoshuaMKW/GeckoLoader
echo "patching main.dol of extracted iso with hoshi entrypoint gecko codes..."
python externals/GeckoLoader/GeckoLoader.py extracted-iso/root/sys/main.dol externals/hoshi/entrypoint/out/codes.gct --hooktype=PAD --dest=extracted-iso/root/sys/main.dol

# copy hoshi.bin into root/files of extracted iso
echo "copying hoshi.bin into root/files of extracted iso..."
cp externals/hoshi/out/release/hoshi.bin extracted-iso/root/files/

# copy hoshi.bin into riivolution folder
echo "copying hoshi.bin into riivolution folder..."
cp externals/hoshi/out/release/hoshi.bin riivolution/karap/

# copy mod files into root/files of extracted iso
echo "copying mod .bin files into root/files of extracted iso..."
cp out/mods/*.bin extracted-iso/root/files/

# copy mod files into riivolution folder
echo "copying mod .bin files into riivolution folder..."
cp out/mods/*.bin riivolution/karap/mods/

# rebuild iso
echo "rebuilding iso..."
python -m pyisotools 'extracted-iso/root' B --dest="../../iso/Kirby Air Ride (USA) (Hoshi).iso"

# copy modded iso into dolphin dir
cp 'iso/Kirby Air Ride (USA) (Hoshi).iso' ~/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/ISOS/

# copy riivolution files to dolphin dir
pushd riivolution
rm -r ~/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Load/Riivolution/*
cp -r * ~/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Load/Riivolution/
popd
