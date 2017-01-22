#!/usr/bin/env bash
set -xeuo pipefail

sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo add-apt-repository -y ppa:beineri/opt-qt562-trusty
sudo add-apt-repository -y ppa:george-edison55/cmake-3.x
sudo apt-get update -qq
sudo apt-get install -qq -y gcc-${GCC_VERSION} g++-${GCC_VERSION} liblmdb-dev qt56base cmake
