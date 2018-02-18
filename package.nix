{stdenv, cmake, ninja, qtbase, qtsvg, lmdb, git}:

stdenv.mkDerivation rec {
  name = "nachat-${version}";
  version = "0.0";

  buildInputs = [ cmake ninja qtbase qtsvg lmdb git ];

  src = builtins.filterSource
    (path: type: type != "directory" || baseNameOf path != "build")
    ./.;
  enableParallelBuilding = true;
}
