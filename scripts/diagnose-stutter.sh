#!/usr/bin/env bash
set -euo pipefail

compare_pointer=0
compare_backend=0
if [[ "${1:-}" == "--compare-pointer" ]]; then
    compare_pointer=1
elif [[ "${1:-}" == "--compare-backend" ]]; then
    compare_backend=1
elif [[ $# -gt 0 ]]; then
    printf 'Usage: %s [--compare-pointer|--compare-backend]\n' "$0" >&2
    exit 2
fi
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
log_file="$repo_dir/live2d-stutter-$(date +%Y%m%d-%H%M%S).log"
# Install the instrumented build before restarting; never log the user's full
# environment or model configuration.
cmake --install "$repo_dir/build" --prefix "$HOME/.local"
previous=$(systemctl --user show-environment | sed -n 's/^LIVE2D_TRACE_SECONDS=//p')
previous_compare=$(systemctl --user show-environment | sed -n 's/^LIVE2D_TRACE_POINTER_COMPARE=//p')
previous_backend=$(systemctl --user show-environment | sed -n 's/^LIVE2D_TRACE_BACKEND_COMPARE=//p')
restore_environment() {
    if [[ -n "$previous_backend" ]]; then
        systemctl --user set-environment "LIVE2D_TRACE_BACKEND_COMPARE=$previous_backend"
    else
        systemctl --user unset-environment LIVE2D_TRACE_BACKEND_COMPARE
    fi
    if [[ -n "$previous_compare" ]]; then
        systemctl --user set-environment "LIVE2D_TRACE_POINTER_COMPARE=$previous_compare"
    else
        systemctl --user unset-environment LIVE2D_TRACE_POINTER_COMPARE
    fi
    if [[ -n "$previous" ]]; then
        systemctl --user set-environment "LIVE2D_TRACE_SECONDS=$previous"
    else
        systemctl --user unset-environment LIVE2D_TRACE_SECONDS
    fi
}
trap restore_environment EXIT
if ! command -v python3 >/dev/null 2>&1; then
    printf '需要 python3 以采样系统线程调度。\n' >&2
    exit 1
fi
system_log=$(mktemp)
sampler_pid=""
cleanup_sampling() {
    if [[ -n "$sampler_pid" ]]; then
        kill "$sampler_pid" 2>/dev/null || true
        wait "$sampler_pid" 2>/dev/null || true
    fi
    rm -f -- "$system_log"
    restore_environment
}
trap cleanup_sampling EXIT
since=$(date --iso-8601=seconds)
systemctl --user set-environment LIVE2D_TRACE_SECONDS=60 "LIVE2D_TRACE_POINTER_COMPARE=$compare_pointer" "LIVE2D_TRACE_BACKEND_COMPARE=$compare_backend"
systemctl --user restart plasma-plasmashell.service
python3 "$repo_dir/scripts/sample-stutter.py" 65 > "$system_log" &
sampler_pid=$!
sleep 2
restore_environment
if [[ "$compare_pointer" == 1 ]]; then
    printf '对照模式：前 30 秒正常跟随，后 30 秒临时关闭跟随，结束自动恢复；请保持移动鼠标。\n'
fi
if [[ "$compare_backend" == 1 ]]; then
    printf '后端对照：前 30 秒当前后端，后 30 秒切换另一个后端，结束恢复；请保持移动鼠标。\n'
fi
printf '请在接下来 60 秒内移动鼠标，复现卡顿；也可以留一段静止时间。\n'
sleep 60
journalctl --user -u plasma-plasmashell.service --since "$since" --no-pager -o short-monotonic |
    rg 'Live2D trace' > "$log_file" || true
kill "$sampler_pid" 2>/dev/null || true
wait "$sampler_pid" 2>/dev/null || true
cat "$system_log" >> "$log_file"
rm -f -- "$system_log"
trap - EXIT
if ! rg -q 'Live2D trace item=' "$log_file"; then
    printf '未捕获帧记录，请检查 Plasma 服务是否正确启动。日志：%s\n' "$log_file" >&2
    exit 1
fi
printf '记录完成：%s\n' "$log_file"
