#!/bin/bash

# This is an out of tree build but your source tree
# can not in-tree build

# sudo apt-get build-dep -y qemu
qemu_path=$(pwd)
qemu_name="${qemu_path##*/}"

buildir=../build-$qemu_name
if [ ! -d  $buildir ] ; then
  git submodule init
  mkdir -p $buildir
else
  make distclean
fi

git submodule update
pushd $buildir

../$qemu_name/configure --python=python3 --target-list="arm-softmmu" --disable-vnc --disable-curses --disable-sdl --disable-hax --disable-rdma --enable-debug --enable-pie --enable-kvm --enable-linux-user --disable-gtk

popd

make -j`nproc`
