"""Exercise the installed IPC contract and real model rendering, without a display."""
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

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
    line = process.stdout.readline()
    assert len(line) % 4 == 0, "RGBA payload is not 32-bit aligned after the header"
    header = json.loads(line)
    assert "error" not in header, header
    assert header["backend"] == backend
    expected = current["width"] * current["height"] * 4
    if header.get("sharedFrame"):
        assert header["bytes"] == 0 and header["pixelBytes"] == expected
        with open(current["sharedFrame"], "rb") as shared:
            return header, shared.read(expected)
    assert header["bytes"] == expected
    return header, read_exact(process.stdout, header["bytes"])


def close(process):
    process.stdin.close()
    assert process.wait(timeout=30) == 0


with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
    metadata, first = frame(process, trace=True, sentNs=time.monotonic_ns())
    assert metadata["ipc"]["request_wait_ms"] >= 0
    assert metadata["ipc"]["readyNs"] > 0
    assert metadata["ipc"]["previous_write_ms"] == 0
    if backend == "vulkan":
        detail = metadata["readbackDetail"]
        assert detail["gpu_wait_ms"] >= 0 and detail["pixel_copy_ms"] >= 0
        assert isinstance(detail["host_cached"], bool)
    traced, _ = frame(process, trace=True, sentNs=time.monotonic_ns(), metadataOnce=True)
    for name in ("expressions", "motionGroups", "hotkeys"):
        assert name not in traced, "static metadata repeated after opt-in"
    compatible, _ = frame(process)
    assert compatible["expressions"] == metadata["expressions"]
    reloaded, _ = frame(process, source=pathlib.Path(model).as_uri() + "#metadata", metadataOnce=True)
    assert reloaded["motionGroups"] == metadata["motionGroups"]
    _, _ = frame(process, metadataOnce=True)
    assert traced["ipc"]["previous_write_ms"] >= 0
    for _ in range(30):
        _, second = frame(process)
    # Shared-memory and pipe paths must produce identical paused pixels;
    # resize must publish the correct new payload without trailing stdout data.
    _, expected = frame(process, delta=0, follow=False)
    with tempfile.NamedTemporaryFile() as shared:
        shared.truncate(2048 * 2048 * 4)
        header, actual = frame(process, delta=0, follow=False, sharedFrame=shared.name)
        assert header["sharedFrame"] and actual == expected
        frame(process, width=300, height=420, sharedFrame=shared.name)
        frame(process, sharedFrame=shared.name)
    frame(process)  # Pipe fallback remains usable after shared frames.
    _, expected = frame(process, delta=0, follow=False)
    header, fallback = frame(process, delta=0, follow=False, sharedFrame="/missing/live2d-frame")
    assert not header.get("sharedFrame") and fallback == expected
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

# Build a temporary VTube Studio-style fixture from the selected test model.
# This exercises settings reserialization through the actual Cubism parser and
# checks that importing Idle produces exactly the native model's paused frame.
settings = json.loads(pathlib.Path(model).read_text(encoding="utf-8-sig"))
idle = settings["FileReferences"].get("Motions", {}).get("Idle", [])
if idle:
    with tempfile.TemporaryDirectory(prefix="live2d-vtube-") as directory:
        fixture = pathlib.Path(directory) / "model"
        shutil.copytree(pathlib.Path(model).parent, fixture)
        refs = settings["FileReferences"]
        refs.pop("Pose", None)  # Cover PartOpacity curves without a pose file.
        refs["Motions"] = {"Idle": [{"File": idle[0]["File"]}]}
        native = fixture / "native.model3.json"
        native.write_text(json.dumps(settings, ensure_ascii=False, indent=2))
        motion = fixture / idle[0]["File"]
        (fixture / "animations").mkdir(exist_ok=True)
        imported_motion = fixture / "animations" / "vts-idle.motion3.json"
        shutil.copyfile(motion, imported_motion)
        refs.pop("Motions")
        imported = fixture / "imported.model3.json"
        imported.write_text(json.dumps(settings, ensure_ascii=False, indent=2))
        (fixture / "imported.vtube.json").write_text(json.dumps({
            "FileReferences": {"Model": imported.name, "IdleAnimation": imported_motion.name}
        }))
        with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
            _, expected = frame(process, source=native.as_uri(), delta=0, follow=False)
            _, actual = frame(process, source=imported.as_uri(), delta=0, follow=False)
            assert actual == expected, "VTube Studio idle import changed the initial frame"
            _, paused = frame(process, source=imported.as_uri(), delta=0, follow=False)
            assert paused == actual, "paused VTube Studio model changed between frames"
            close(process)

        # Models like p298 have no idle: actions exist only in the hotkey list.
        expression = fixture / "held.exp3.json"
        expression.write_text(json.dumps({"Type": "Live2D Expression", "Parameters": [
            {"Id": "ParamAngleX", "Value": 25, "Blend": "Add"}
        ]}, indent=2))
        (fixture / "imported.vtube.json").write_text(json.dumps({
            "FileReferences": {"Model": imported.name, "IdleAnimation": ""},
            "Hotkeys": [
                {"Action": "TriggerAnimation", "File": imported_motion.name,
                 "FadeSecondsAmount": 0, "Triggers": {"Trigger1": "W"}},
                {"Action": "ToggleExpression", "File": expression.name,
                 "DeactivateAfterKeyUp": True, "Triggers": {"Trigger1": "LeftMouseButton"}}
            ]
        }))
        with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
            meta, baseline = frame(process, source=imported.as_uri(), delta=0, follow=False)
            assert meta["motionGroups"] == ["vts-idle"]
            assert len(meta["hotkeys"]) == 2
            assert "held" in meta["expressions"]
            command = dict(action="expression", name="held", active=True)
            _, pressed = frame(process, source=imported.as_uri(), delta=0, follow=False, actions=[command])
            assert pressed != baseline, "held expression was not applied"
            command["active"] = False
            _, released = frame(process, source=imported.as_uri(), delta=0, follow=False, actions=[command])
            assert released == baseline, "expression did not reset on release"
            # After a first play the cache must not touch the file again.
            # Repeated holds also check cached expression ownership and reset.
            expression.unlink()
            for _ in range(3):
                command["active"] = True
                _, repeated = frame(process, source=imported.as_uri(), delta=0, follow=False, actions=[command])
                assert repeated == pressed, "cached held expression changed on replay"
                command["active"] = False
                _, repeated = frame(process, source=imported.as_uri(), delta=0, follow=False, actions=[command])
                assert repeated == baseline, "cached held expression failed to release"
            frame(process, source=imported.as_uri(), follow=False,
                  actions=[dict(action="motion", group="vts-idle", index=0)])
            for _ in range(10):
                _, playing = frame(process, source=imported.as_uri(), follow=False)
            assert playing != baseline, "explicit hotkey motion did not play"
            imported_motion.unlink()
            # Trigger while the same motion is still fading; each active play
            # needs its own object, while all reuse the already-read JSON.
            for _ in range(3):
                frame(process, source=imported.as_uri(), follow=False,
                      actions=[dict(action="motion", group="vts-idle", index=0)])
            # Let all entries finish, then replay a reusable cached instance.
            duration = json.loads(motion.read_text(encoding="utf-8-sig"))["Meta"]["Duration"]
            for _ in range(int(duration / .1) + 30):
                frame(process, source=imported.as_uri(), delta=.1, follow=False)
            frame(process, source=imported.as_uri(), follow=False,
                  actions=[dict(action="motion", group="vts-idle", index=0)])
            close(process)

# Mouse mappings must work for arbitrary real parameters, use pointer position
# independently of gaze, compose with held expressions, and restore on disable.
with tempfile.TemporaryDirectory(prefix="live2d-pointer-") as directory:
    fixture = pathlib.Path(directory) / "model"
    shutil.copytree(pathlib.Path(model).parent, fixture)
    settings = json.loads(pathlib.Path(model).read_text(encoding="utf-8-sig"))
    refs = settings["FileReferences"]
    for key in ("Motions", "Physics", "Pose"):
        refs.pop(key, None)
    settings.pop("Groups", None)  # No random blinking in snapshot comparisons.
    refs["Expressions"] = [{"Name": "held", "File": "held.exp3.json"}]
    (fixture / "held.exp3.json").write_text(json.dumps({
        "Type": "Live2D Expression", "FadeInTime": 0, "FadeOutTime": 0, "Parameters": [
            {"Id": "ParamAngleX", "Value": 15, "Blend": "Add"}
        ]
    }, indent=2))
    source = fixture / "pointer.model3.json"
    source.write_text(json.dumps(settings, indent=2))
    def mapping(parameter, axis, lower, upper):
        return dict(Input="MousePosition" + axis, OutputLive2D=parameter,
                    InputRangeLower=-1, InputRangeUpper=1,
                    OutputRangeLower=lower, OutputRangeUpper=upper, Smoothing=0)
    (fixture / "pointer.vtube.json").write_text(json.dumps({
        "FileReferences": {"Model": source.name},
        "ParameterSettings": [mapping("ParamAngleX", "X", -15, 15),
                              mapping("ParamAngleY", "Y", -20, 20),
                              mapping("ParamEyeBallX", "X", 0, 0),
                              mapping("ParamEyeBallY", "Y", 0, 0)]
    }))
    command = dict(action="expression", name="held", active=True)
    with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        common = dict(source=source.as_uri(), delta=0, gazeX=0, gazeY=0, pointerX=0, pointerY=0)
        _, baseline = frame(process, **(common | dict(follow=False)))
        _, reference = frame(process, **(common | dict(follow=False, actions=[command])))
        command["active"] = False
        _, right = frame(process, **(common | dict(pointerX=1, actions=[command])))
        assert right == reference and right != baseline, "mouse range mapping did not match the expression pose"
        _, left = frame(process, **(common | dict(pointerX=-1)))
        assert left != right and left != baseline, "horizontal mouse input did not move the model"
        _, up = frame(process, **(common | dict(pointerY=1)))
        _, down = frame(process, **(common | dict(pointerY=-1)))
        assert up != down and up != baseline, "vertical mouse input did not move the model"
        command["active"] = True
        _, combined = frame(process, **(common | dict(pointerX=-1, actions=[command])))
        assert combined == baseline, "mouse mapping did not compose with held expressions"
        command["active"] = False
        _, restored = frame(process, **(common | dict(follow=False, actions=[command])))
        assert restored == baseline, "disabling mouse follow did not restore the model"
        _, once = frame(process, **(common | dict(source=source.as_uri()+"#one", delta=.1, pointerX=.5)))
        _, with_gaze = frame(process, **(common | dict(source=source.as_uri()+"#two", delta=.1,
                                                    pointerX=.5, gazeX=1, gazeY=1)))
        assert once == with_gaze, "standard gaze was added a second time to mapped parameters"
        normal = common | dict(source=source.as_uri()+"#normal", follow=False)
        _, regular = frame(process, **(normal | dict(expression="held")))
        (fixture / "held.exp3.json").unlink()
        for _ in range(3):
            _, replayed = frame(process, **(normal | dict(expression="held")))
            assert replayed == regular, "cached normal expression changed on replay"
        close(process)

# A two-texel black/white checkerboard must converge to its gray average when
# minified, rather than aliasing as arbitrary speckles. Compare real rendering
# against the same model with a solid-gray atlas, using only stdlib PNG encoding.
if backend == "vulkan":
    def png(width, height, checker):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        rows = bytearray()
        for y in range(height):
            rows.append(0)  # PNG filter: None.
            for x in range(width):
                value = (255 if (x // 2 + y // 2) % 2 else 0) if checker else 128
                rows.extend((value, value, value, 255))
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

    with tempfile.TemporaryDirectory(prefix="live2d-mipmap-") as directory:
        fixture = pathlib.Path(directory) / "model"
        shutil.copytree(pathlib.Path(model).parent, fixture)
        settings = json.loads(pathlib.Path(model).read_text(encoding="utf-8-sig"))
        refs = settings["FileReferences"]
        for key in ("Motions", "Physics", "Pose", "Expressions"):
            refs.pop(key, None)
        settings.pop("Groups", None)
        texture_count = len(refs["Textures"])
        images = {}
        with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
            for label, checker in (("checker", True), ("gray", False)):
                texture = fixture / f"{label}.png"
                texture.write_bytes(png(1024, 1024, checker))
                refs["Textures"] = [texture.name] * texture_count
                source = fixture / f"{label}.model3.json"
                source.write_text(json.dumps(settings, indent=2))
                _, images[label] = frame(process, source=source.as_uri(), width=160, height=240,
                                         delta=0, follow=False, idle="")
            # Exclude transparent background so it cannot hide texture aliasing.
            pixels = [i for i in range(0, len(images["gray"]), 4) if images["gray"][i + 3] > 128]
            assert len(pixels) > 500
            difference = sum(abs(images["checker"][i + channel] - images["gray"][i + channel])
                             for i in pixels for channel in range(3)) / (len(pixels) * 3)
            assert difference < 2, f"minified texture did not average to gray: {difference}"
            # Odd and single-pixel atlas dimensions exercise all mip copy extents
            # and the sampler's zero maximum LOD for a one-level texture.
            for width, height in ((257, 129), (1, 1)):
                texture.write_bytes(png(width, height, False))
                frame(process, source=source.as_uri() + f"#{width}x{height}", width=160, height=240,
                      delta=0, follow=False, idle="")
            close(process)

# Invalid inputs must return a structured error, not crash or allocate huge buffers.
with subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
    process.stdin.write(json.dumps(request | dict(width=999999)).encode() + b"\n")
    process.stdin.flush()
    assert "error" in json.loads(process.stdout.readline())
    process.stdin.close()
    assert process.wait(timeout=30) == 2

print(f"{backend}: animated transparent model, premultiplied alpha, resize, interaction, reload and errors passed")
