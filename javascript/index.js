let loader = require("./loader.js");
loader.load("./gnugo.wasm", (Module) => () => {
  document.querySelector("#gnugoOutput").innerHTML = "Ready to play!";
  document.querySelector("#playButton").addEventListener("click", function () {
    let input = document.querySelector("#textInput").value;
    let output = Module.ccall(
      "play",
      "string",
      ["number", "string"],
      [0, input]
    );
    document.querySelector("#gnugoOutput").innerHTML = output;
  });
});
