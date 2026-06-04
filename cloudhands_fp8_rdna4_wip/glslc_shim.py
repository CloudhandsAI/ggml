#!/usr/bin/env python3
# glslc shim: route fp8 shaders to glslang-main (which has fp8 GLSL); pass everything
# else through to the stock glslc unchanged (keeps -O + native #include handling).
# fp8 shaders are detected by a -DDATA_A_E4M3 define. For those, translate glslc args to
# glslangValidator, inject GL_GOOGLE_include_directive (glslang needs it; glslc auto-enables),
# and emit a stub Make-format depfile so ninja stays happy.
import sys, os, subprocess, tempfile, re

REAL_GLSLC = os.environ.get("REAL_GLSLC", "/usr/bin/glslc.real")
GLSLANG    = os.environ.get("GLSLANG_BIN", "/b/glslang-main/build/StandAlone/glslang")

args = sys.argv[1:]
is_fp8 = any("DATA_A_E4M3" in a for a in args)

if not is_fp8:
    os.execv(REAL_GLSLC, [REAL_GLSLC] + args)

# --- fp8 path: translate to glslang ---
out = inp = depf = None
target = "vulkan1.2"
passthru = []
i = 0
while i < len(args):
    a = args[i]
    if a.startswith("-fshader-stage"):       pass
    elif a.startswith("--target-env="):       target = a.split("=", 1)[1]
    elif a in ("-O", "-g"):                    pass
    elif a == "-MD":                           pass
    elif a == "-MF":                           depf = args[i+1]; i += 1
    elif a == "-o":                            out = args[i+1]; i += 1
    elif a.startswith("-D") or a.startswith("-I"): passthru.append(a)
    elif a.startswith("-"):                    pass
    else:                                      inp = a
    i += 1

# inject GL_GOOGLE_include_directive after the #version line into a temp copy
src = open(inp).read()
if "GL_GOOGLE_include_directive" not in src:
    src = re.sub(r"(#version[^\n]*\n)", r"\1#extension GL_GOOGLE_include_directive : require\n", src, count=1)
tmp = tempfile.NamedTemporaryFile("w", suffix=".comp", delete=False, dir=os.path.dirname(out) or ".")
tmp.write(src); tmp.close()

cmd = [GLSLANG, "-V", "--target-env", target, "-I" + (os.path.dirname(inp) or "."), "-S", "comp", "-o", out, tmp.name] + passthru
r = subprocess.run(cmd, capture_output=True, text=True)
os.unlink(tmp.name)
if depf and out and inp:
    open(depf, "w").write(f"{out}: {inp}\n")
if r.returncode != 0:
    sys.stderr.write(r.stdout + r.stderr)
sys.exit(r.returncode)
