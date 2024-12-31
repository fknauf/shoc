#!/bin/sh

# get dependencies and build experiments in a clean DOCA base container (version 2.9.1)

set -e

apt-get update
apt-get -y upgrade
apt-get install libspdlog-dev g++-12 libgtest-dev nlohmann-json3-dev jq python3-pandas

CC=gcc-12 CXX=g++-12 meson setup --buildtype=release bench
cd bench
ninja
