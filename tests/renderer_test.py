"""Exercise the installed IPC contract and real model rendering, without a display."""
import json
import pathlib
import subprocess
import sys

helper, model, output = sys.argv[1:]
backend = pathlib.Path(helper).name.removeprefix("live2d-render-")
request = dict(source=pathlib.Path(model).as_uri(), width=400, height=600,
               delta=1 / 30, zoom=1, follow=True, gazeX=.3, gazeY=.2, idle="Idle")


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        assert chunk, "renderer terminated during frame"
        data.extend(chunk)
    return bytes(data)


def frame(process, **changes):
    current = request | changes
    process.stdin.write(json.dumps(current).encode() + b"\n")
    process.stdin.flush()
    header = json.loads(process.stdout.readline())
    assert "error" not in header, header
    assert header["backend"] == backend
    assert header["bytes"] == current["width"] * current["height"] * 4
    return header, read_exact(process.stdout, header["bytes"])


def close(process):
    process.stdin.close()
    assert process.wait(timeout=30) == 0


with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
    metadata, first = frame(process)
    for _ in range(30):
        _, second = frame(process)
    opaque = sum(alpha > 128 for alpha in second[3::4])
    transparent = second[3::4].count(0)
    assert opaque > 5000 and transparent > 5000, (opaque, transparent)
    assert first != second, "animation did not advance"
    assert all(r <= a + 2 and g <= a + 2 and b <= a + 2
               for r, g, b, a in zip(second[::4], second[1::4], second[2::4], second[3::4])), "non-premultiplied output"
    pathlib.Path(output).write_bytes(second)
    changes = dict(taps=[[.5, .65]])
    if metadata["expressions"]:
        changes["expression"] = metadata["expressions"][0]
    frame(process, **changes)
    frame(process, width=300, height=420)
    # Recreate a model in the same device and Framework instance.
    # Source URI fragment changes the URL while keeping all resources read-only.
    frame(process, source=pathlib.Path(model).as_uri() + "#reload")
    close(process)

# A new process must be able to initialize the backend after a previous exit.
with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
    frame(process)
    close(process)

# Invalid inputs must return a structured error, not crash or allocate huge buffers.
with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
    process.stdin.write(json.dumps(request | dict(width=999999)).encode() + b"\n")
    process.stdin.flush()
    assert "error" in json.loads(process.stdout.readline())
    process.stdin.close()
    assert process.wait(timeout=30) == 2

print(f"{backend}: animated transparent model, premultiplied alpha, resize, interaction, reload and errors passed")
