let
  # we need emscripten < 2.x to avoid duplicated symbols error
  nixpkgs = import (builtins.fetchTarball {
    name = "nixos-unstable-20.03";
    url =
      "https://github.com/nixos/nixpkgs/archive/0bf298df24f721a7f85c580339fb7eeff64b927c.tar.gz";
    # Hash obtained using `nix-prefetch-url --unpack <url>`
    sha256 = "0kdx3pz0l422d0vvvj3h8mnq65jcg2scb13dc1z1lg2a8cln842z";
  }) { };
in nixpkgs.stdenv.mkDerivation {
  name = "wasm-gnugo";
  buildInputs = [ nixpkgs.emscripten ];
}
