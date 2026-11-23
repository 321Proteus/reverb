#!/usr/bin/env bash
# Runs tilgen in a pinned toolchain. Every argument after the mode goes to
# `python -m tilgen` (e.g. --abi itanium64 --source juce -j 8).
#
#   run.sh host   [args]  this machine's IDA and venv. For GNU ABIs every GCC
#                         under /usr/lib/gcc but $TILGEN_CXX's (default g++-12)
#                         is hidden with bwrap
#   run.sh docker [args]  the image of the Dockerfile (GCC 12,
#                         Debian 12 headers) with correct mounts mounted
#
# Environment:
# TILGEN_IDA    (/opt/ida-pro),
# TILGEN_SDKS   (~/Source/rv_sdks),
# TILGEN_XWIN   (~/xwin/splat),
# TILGEN_CXX    (host mode, g++-12),
# TILGEN_IMAGE  (reverb-tilgen),
# TILGEN_VENV   (host mode, <repo>/venv).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
IDA="${TILGEN_IDA:-/opt/ida-pro}"
SDKS="${TILGEN_SDKS:-$HOME/Source/rv_sdks}"
XWIN="${TILGEN_XWIN:-$HOME/xwin/splat}"
IMAGE="${TILGEN_IMAGE:-reverb-tilgen}"

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

# The --abi of the arguments (tilgen's default: msvc64)
abi_of() {
    local abi=msvc64 prev=
    for a in "$@"; do
        case "$a" in --abi=*) abi="${a#--abi=}" ;; esac
        [ "$prev" = --abi ] && abi="$a"
        prev="$a"
    done
    echo "$abi"
}

host() {
    local cxx="${TILGEN_CXX:-g++-12}"
    local venv="${TILGEN_VENV:-$REPO/venv}"

    # shellcheck disable=SC1091
    source "$venv/bin/activate"
    export IDADIR="$IDA"

    cd "$REPO/tools"

    case "$(abi_of "$@")" in
    msvc*)
        exec python -m tilgen --sdks "$SDKS" --xwin "$XWIN" "$@" ;;
    esac

    command -v "$cxx" >/dev/null || { echo "no $cxx (set TILGEN_CXX)" >&2; exit 1; }
    command -v bwrap >/dev/null || { echo "host mode needs bwrap" >&2; exit 1; }

    # If the libgcc lives outside /usr/lib/gcc/<triple>, clang finds
    # no GCC at all and only the dirs from cxx -v are used.
    local keep binds=()
    keep="$(dirname "$("$cxx" -print-libgcc-file-name)")"

    for t in /usr/lib/gcc/*linux*; do
        [ -d "$t" ] || continue
        binds+=(--tmpfs "$t")
        case "$keep" in "$t"/*) binds+=(--ro-bind "$keep" "$keep") ;; esac
    done

    exec bwrap --dev-bind / / "${binds[@]}" \
        python -m tilgen --sdks "$SDKS" --xwin "$XWIN" --cxx "$cxx" "$@"
}

BUILD_STATE=build/tilgen-docker

container_path() {
    local p
    p="$(realpath -m -- "$1")"
    case "$p" in
        "$REPO") echo /work ;;
        "$REPO"/*) echo "/work/${p#"$REPO"/}" ;;
        *) echo "run.sh docker: $1 is outside $REPO, the container cannot write it" >&2
           return 1 ;;
    esac
}

docker_run() {
    local args=() prev= a
    for a in "$@"; do
        case "$prev" in
            --out|--db) a="$(container_path "$a")" || exit 1 ;;
        esac
        case "$a" in
            --out=*|--db=*) a="${a%%=*}=$(container_path "${a#*=}")" || exit 1 ;;
        esac
        args+=("$a"); prev="$a"
    done
    set -- "${args[@]}"
    local state="$REPO/$BUILD_STATE"
    mkdir -p "$state/home/.idapro"

    # IDA's registry holds the license/EULA acceptance
    [ -f "$state/home/.idapro/ida.reg" ] ||
        cp "$HOME/.idapro/ida.reg" "$state/home/.idapro/"

    docker build -t "$IMAGE" "$HERE"
    local opts=(--rm)
    [ -t 1 ] && opts+=(-t)
    
    # Rootless+rootful docker setup
    if ! docker info -f '{{.SecurityOptions}}' 2>/dev/null | grep -q rootless; then
        opts+=(--user "$(id -u):$(id -g)")
    fi
    exec docker run "${opts[@]}" \
        -e HOME="/work/$BUILD_STATE/home" \
        -v "$IDA:/opt/ida-pro:ro" \
        -v "$REPO:/work" \
        -v "$SDKS:/sdks:ro" \
        -v "$XWIN:/xwin:ro" \
        "$IMAGE" /work/tools/tilgen/run.sh _inside --sdks /sdks --xwin /xwin "$@"
}

# In the container
inside() {
    local venv="/work/$BUILD_STATE/venv"
    if [ ! -x "$venv/bin/python" ]; then
        python3 -m venv "$venv"
        "$venv/bin/pip" install --no-index -q /opt/ida-pro/idalib/python/idapro-*.whl
    fi

    local lib
    lib="$(python3 -c 'import sysconfig as s, os; print(os.path.join(s.get_config_var("LIBDIR"), s.get_config_var("INSTSONAME")))')"
    /opt/ida-pro/idapyswitch --force-path "$lib" >/dev/null

    printf '{"Paths": {"ida-install-dir": "/opt/ida-pro"}}\n' \
        > "$HOME/.idapro/ida-config.json"
    git config --global --get safe.directory >/dev/null ||
        git config --global safe.directory '*'

    cd /work/tools
    exec "$venv/bin/python" -m tilgen --cxx g++ "$@"
}

[ $# -ge 1 ] || usage
mode="$1"; shift

case "$mode" in
    host) host "$@" ;;
    docker) docker_run "$@" ;;
    _inside) inside "$@" ;;
    *) usage ;;
esac
