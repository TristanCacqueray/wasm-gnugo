# wasm-gnugo

A gnugo fork for the browser.

## Build native version

```
export CFLAGS="-g -O2 -Wno-format-security -Wno-constant-conversion"
mkdir -p build/native
pushd build/native
  ../../configure && make
popd
```

## Build llvm ir

```
nix-shell
mkdir -p build/wasm
pushd build/wasm
  emconfigure ../../configure --without-readline --without-curses --disable-socket-support --disable-color
  emmake make
  # first make should fail because pattern helper are not native, run to complete build
  for helper in mkpat mkeyes uncompress_fuseki joseki mkmcpat; do
    cp ../native/patterns/${helper} patterns/
    chmod +x patterns/${helper}
    emmake make
  done
popd
```

`file build/wasm/interface/gnugo` prints `LLVM IR bitcode`

## Build gnugo.wasm

```
INPUTS="build/wasm/interface/*.o build/wasm/engine/libengine.a build/wasm/patterns/libpatterns.a build/wasm/sgf/libsgf.a build/wasm/utils/libutils.a"
emcc -s BINARYEN_ASYNC_COMPILATION=0 \
     -s ALLOW_MEMORY_GROWTH=1        \
     -s EXPORTED_RUNTIME_METHODS='["ccall"]' \
     -s EXPORTED_FUNCTIONS="['_get_version', '_play', '_score']" \
     -o gnugo.js $INPUTS
```

## Test with node

```
> let main = require('./gnugo.js')
> main.ccall("play", "string", ["number", "string"], [0, "(;GM[1]SZ[10])"])
'(;GM[1]FF[4]\nSZ[10]\nDT[2020-11-08]\nAP[GNU Go:3.9.1]\n;B[fe]C[load and analyze mode])\n'
```

## Patch javascript interface to work synchronously

Edit the `gnugo.js` file:

- Replace `var Module = typeof Module !== 'undefined' ? Module : {};` with `exports.init = function(Module) {`
- Add a `}` at the end

## Build test interface

```
parcel serve javascript/index.html
parcel build javascript/index.html
```

## Test with browser

Visit [demo](https://tristancacqueray.github.io/wasm-gnugo/)

## Update pages

```
rm -Rf dist/ && \
    parcel build javascript/index.html --public-url /wasm-gnugo/ && \
    git checkout pages && \
    rsync -a dist/ $(pwd)/ && \
    git add -A && \
    git commit -m "Updates" && \
    git push origin pages && \
    git checkout master
```
