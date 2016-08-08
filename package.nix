{stdenv, cmake, ninja, qtbase, qtsvg, lmdb, git, makeQtWrapper}:

stdenv.mkDerivation rec {
  name = "nachat-${version}";
  version = "0.0";

  buildInputs = [ cmake ninja qtbase qtsvg lmdb git ];
  nativeBuildInputs = [ makeQtWrapper ];
  preFixup = ''
    wrapQtProgram $out/bin/nachat
  '';

  src = builtins.filterSource
    (path: type: type != "directory" || baseNameOf path != "build")
    ./.;
  enableParallelBuilding = true;
}
