let filebytes;
async function onFile(ev, view, instance) {
  const offset = instance.exports.__heap_base;
  const file = ev.target.files[0];
  const buffer = await file.arrayBuffer();
  filebytes = new Uint8Array(buffer);
  view.set(filebytes, offset);

  const MaxMemory = BigInt(view.length - offset);
  const resultOffset = instance.exports.Entry(offset, filebytes.length, MaxMemory);

  const wordExtractorView = new Uint32Array(view.buffer, resultOffset, 1);
  const numBytesInResult = wordExtractorView[0];

  const outputView = new Uint8Array(view.buffer, resultOffset + 4, numBytesInResult - 4);
  const outputLog = String.fromCharCode.apply(null, outputView);

  const input = document.querySelector("#input");
  let outputString = "";
  for (let i = 0; i < filebytes.length; ++i) {
    outputString += filebytes[i].toString(2).padStart(8, '0') + " ";
  }
  input.innerText = outputString;
  const output = document.querySelector("#output");
  output.innerText = outputLog;
}

export async function main() {
  const filePicker = document.querySelector("input#choose");

  const wasmMemory = new WebAssembly.Memory({ initial: 160 });
  const view = new Uint8Array(wasmMemory.buffer);
  const importObject = {
    env: {
      memory: wasmMemory
    }
  };

  const { instance } = await WebAssembly.instantiateStreaming(
    fetch("./sim8086-test.wasm"), importObject
  );

  const version = instance.exports.Sim86_GetVersion();
  const versionContainer = document.querySelector("h2#version");
  versionContainer.innerText = "Sim8086: " + version;

  filePicker.addEventListener("change", 
    (ev) => onFile(ev, view, instance)
  );

  const stepper = document.querySelector("button#stepper");
  stepper.addEventListener("click", () => {
    const offset = instance.exports.__heap_base;
    const MaxMemory = BigInt(view.length - offset);
    const registerOffset = instance.exports.Step(offset, filebytes.length, MaxMemory);
    const wordView = new Uint32Array(view.buffer, registerOffset);
    const currentOffset = wordView[0];
    const currentInstruction = wordView[1];
    const registers = new Uint16Array(view.buffer, registerOffset + 4);
    console.log(`
      currentOffset: ${currentOffset},
      currentInstruction: ${currentInstruction},
      AX: ${registers[1]},
      BX: ${registers[2]},
      CX: ${registers[3]},
      DX: ${registers[4]},
      SP: ${registers[5]},
      BP: ${registers[6]},
      SI: ${registers[7]},
      DI: ${registers[8]},
      ES: ${registers[9]},
      CS: ${registers[10]},
      SS: ${registers[11]},
      DS: ${registers[12]},
      IP: ${registers[13]},
      FLAGS: ${registers[14]},
      `);
  });
}

