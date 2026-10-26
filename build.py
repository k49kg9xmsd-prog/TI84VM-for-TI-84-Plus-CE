"""Build the TI84VM App and its matching installer with CEdev."""
from pathlib import Path
import os
import subprocess
import zlib

root = Path(__file__).resolve().parent
sdk_dir = os.environ.get("CEDEV")
if not sdk_dir:
    raise SystemExit("Set CEDEV to your CEdev installation directory before building.")
sdk = Path(sdk_dir)
env = os.environ.copy()
env["PATH"] = str(sdk / "bin") + os.pathsep + env.get("PATH", "")

def run(args, cwd=root):
    executable = args[0] + (".exe" if os.name == "nt" else "")
    args[0] = str(sdk / "bin" / executable)
    subprocess.run(args, cwd=cwd, env=env, check=True)

run(["make"])
run(["convbin", "-j", "8ek", "-i", "bin/TI84VM.8ek", "-k", "8xv-split",
     "-m", "65200", "-o", "bin/TVM9A.8xv", "-n", "TVM9A", "-r"])
run(["convbin", "-j", "8ek", "-i", "bin/TI84VM.8ek", "-k", "bin",
     "-o", "bin/TI84VM.raw"])
data = (root / "bin/TI84VM.raw").read_bytes()
chunks = [data[i:i + 65200] for i in range(0, len(data), 65200)]
if len(chunks) != 3:
    raise SystemExit("Installer expects three AppVar chunks; update it before packaging.")
header = (f"#define EXPECTED_APP_SIZE {len(data)}UL\n"
          f"#define EXPECTED_APP_CRC 0x{zlib.crc32(data):08X}UL\n")
for index, chunk in enumerate(chunks):
    header += (f"#define EXPECTED_CHUNK_{index}_SIZE {len(chunk)}UL\n"
               f"#define EXPECTED_CHUNK_{index}_CRC 0x{zlib.crc32(chunk):08X}UL\n")
(root / "installer/src/payload_check.h").write_text(header)
run(["make"], root / "installer")
run(["make"], root / "checker")
print("Built TI84VM App and matching installer files.")
