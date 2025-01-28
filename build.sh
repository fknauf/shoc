#!/bin/sh

# get dependencies and build experiments in a clean DOCA base container (version 2.9.1)

set -e

apt-get update
apt-get -y upgrade
apt-get -y install g++-12 jq gawk curl zip unzip

cd "$(dirname "$0")"

if [ ! -e ~/.vcpkg/vcpkg.path.txt ]; then
    if [ ! -e /doca_devel/vcpkg ]; then
        git clone https://github.com/microsoft/vcpkg.git /doca_devel/vcpkg
        /doca_devel/vcpkg/bootstrap-vcpkg.sh -disableMetrics
    fi
    /doca_devel/vcpkg/vcpkg integrate install
fi

CC=gcc-12 CXX=g++-12 meson setup --buildtype=release bench

cd bench
ninja
