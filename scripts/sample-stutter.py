#!/usr/bin/env python3
"""Bounded /proc sampling for desktop stutter; records no arguments or key data."""
import os
from pathlib import Path
import sys
import time

seconds = min(300, max(1, int(sys.argv[1])))
uid = os.getuid()
hz = os.sysconf('SC_CLK_TCK')
previous = {}
threads = {}
end = time.monotonic() + seconds
next_scan = 0
tracked = []


def stat(path):
    raw = path.read_text()
    left, right = raw.index('('), raw.rindex(')')
    fields = raw[right + 2:].split()
    return raw[left + 1:right], fields[0], int(fields[11]) + int(fields[12]), fields[19]


while time.monotonic() < end:
    now = time.monotonic()
    if now >= next_scan:
        candidates, current, tracked = [], {}, []
        for path in Path('/proc').iterdir():
            if not path.name.isdecimal():
                continue
            try:
                if path.stat().st_uid != uid:
                    continue
                name, state, ticks, birth = stat(path / 'stat')
                key = (path.name, birth)
                current[key] = (ticks, now)
                if key in previous:
                    old, then = previous[key]
                    cpu = 100 * (ticks - old) / hz / (now - then)
                    if cpu >= 2:
                        candidates.append((cpu, path.name, name))
                if name == 'plasmashell' or name.startswith('live2d-render') or name.startswith('kwin_wayland'):
                    tracked.append(path)
            except (OSError, ValueError, IndexError):
                continue
        previous = current
        top = ' '.join(f'{pid}:{name}:{cpu:.1f}%' for cpu, pid, name in sorted(candidates, reverse=True)[:10])
        print(f'[{now:.6f}] Live2D system cpu {top}', flush=True)
        next_scan = now + 1
    for path in tracked:
        try:
            tasks = list((path / 'task').iterdir())
        except OSError:
            continue
        for task in tasks:
            try:
                name, state, ticks, birth = stat(task / 'stat')
                runtime, waiting, slices = map(int, (task / 'schedstat').read_text().split())
                key = (task.name, birth)
                old = threads.get(key)
                threads[key] = (runtime, waiting, now)
                if old:
                    run_ms = (runtime - old[0]) / 1e6
                    wait_ms = (waiting - old[1]) / 1e6
                    elapsed = (now - old[2]) * 1000
                    if run_ms >= 20 or wait_ms >= 15:
                        wchan = (task / 'wchan').read_text().strip()
                        print(f'[{now:.6f}] Live2D system thread pid={path.name} tid={task.name} '
                              f'name={name} state={state} interval_ms={elapsed:.1f} '
                              f'cpu_ms={run_ms:.1f} runnable_wait_ms={wait_ms:.1f} wchan={wchan}', flush=True)
            except (OSError, ValueError, IndexError):
                continue
    time.sleep(.1)
