#!/usr/bin/env bash
# shellcheck shell=bash
# 用法：source aclass.env.sh

GD32_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export GD32_PROJECT_ROOT

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 is required." >&2
    return 1 2>/dev/null || exit 1
fi

build() { python3 "${GD32_PROJECT_ROOT}/scripts/build.py" "$@"; }

gd32_help() {
    echo "Available commands:"
    echo "  build          配置并编译 Debug 固件"
    echo "  build release  配置并编译 Release 固件"
    echo "  build rebuild  清理后重新编译 Debug 固件"
    echo "  build clean    删除所有构建产物"
}

gd32_help
