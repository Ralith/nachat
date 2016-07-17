{stdenv, cmake, qtbase, lmdb, makeQtWrapper}:

stdenv.mkDerivation rec {
  name = "native-chat-${version}";
  version = "0.0";

  buildInputs = [ cmake qtbase lmdb ];
  nativeBuildInputs = [ makeQtWrapper ];
  preFixup = ''
    wrapQtProgram $out/bin/native-chat
  '';

  src = builtins.filterSource
    (path: type: type != "directory" || (baseNameOf path != ".git" && baseNameOf path != "build"))
    ./.;
  enableParallelBuilding = true;
}
