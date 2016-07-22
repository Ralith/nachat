with import <nixpkgs> {};

qt5.callPackage ./package.nix { stdenv = libcxxStdenv; }
