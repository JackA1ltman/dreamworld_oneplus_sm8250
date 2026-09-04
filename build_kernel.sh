#!/usr/bin/env bash
#============================================#
#                                            #
#       Dreamworld Kernel Build Kernel       #
#                                            #
#============================================#

set -e

# ---------- Colorful Output ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color
info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ---------- Help----------
usage() {
    cat << EOF
Usage: $0 [Option]

Option Descriptions (Choose one of the three; options cannot be used simultaneously):
  -g, --get      Get Toolchain
  -b, --build    Skipped get toolchain and build kernel directly
  -p, --package  Pack AnyKernel3
  -a, --all      All steps have been completed
  -h, --help     Show this help message

Example:
  $0 --get
  $0 -b
  $0 --package
EOF
    exit 0
}

PARSED_ARGS=$(getopt -o gbpah --long get,build,package,all,help -n "$0" -- "$@")
if [ $? -ne 0 ]; then
    exit 1
fi

eval set -- "$PARSED_ARGS"

ACTION=""

while true; do
    case "$1" in
        -g|--get)
            ACTION="get"
            shift
            ;;
        -b|--build)
            ACTION="build"
            shift
            ;;
        -p|--package)
            ACTION="package"
            shift
            ;;
        -a|--all)
            ACTION="all"
            shift
            ;;
        -h|--help)
            usage
            ;;
        --)
            shift
            break
            ;;
        *)
            error "Unknown Option: $1"
            ;;
    esac
done

if [ $# -gt 0 ]; then
    error "Unrecognized raw parameter detected: $1 (This script does not accept additional positional parameters.)"
fi

if [ -z "$ACTION" ]; then
    error "Exactly one operation must be specified: -g, -b, -p or -a"
fi

do_get() {
    info "=== Execution Mode: Get Toolchain ==="
    if [ -d "build_toolchain" ]; then
        info "Detect folder exsited, Skip."
    else
        mkdir -p build_toolchain
        info "Created folder."
    fi

    missing=()
    for cmd in curl git ccache 7za; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing+=("$cmd")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        error "Error the package not installed：${missing[*]}"
        exit 1
    fi
    info "All packages installed, Skip."

    if [ -f "build_toolchain/clang_los_12/bin/clang" ]; then
        info "Detect clang existed, Skip."
    else
        git clone -b lineage-20.0 https://github.com/LineageOS/android_prebuilts_clang_kernel_linux-x86_clang-r416183b.git build_toolchain/clang_los_12 --depth 1
        info "Get clang successfully."
    fi

    if [ -f "build_toolchain/gcc_32/bin/arm-linux-androideabi-ar" ]; then
        info "Detect GCC 32, Skip."
    else
        git clone -b arm32 https://github.com/JackA1ltman/Google-GCC-Android-4.9.git build_toolchain/gcc_32 --depth 1
        info "Get GCC 32 successfully."
    fi

    if [ -f "build_toolchain/gcc_64/bin/aarch64-linux-android-ar" ]; then
        info "Detect GCC 64, Skip."
    else
        git clone -b aarch64 https://github.com/JackA1ltman/Google-GCC-Android-4.9.git build_toolchain/gcc_64 --depth 1
        info "Get GCC 64 successfully."
    fi
}

do_build() {
    info "=== Execution Mode: Build Kernel ==="
    export PATH=$PWD/build_toolchain/clang_los_12/bin:$PATH

    if [ ! -f "build_toolchain/clang_los_12/bin/clang" ] || [ ! -f "build_toolchain/gcc_32/bin/arm-linux-androideabi-ar" ] || [ ! -f "build_toolchain/gcc_64/bin/aarch64-linux-android-ar" ]; then
        error "Build toolchain is incomplete. Please re-download it."
        exit 1
    fi

    if [ -d "out" ]; then
        rm -rf out
    fi

    make ARCH=arm64 O=out CC="ccache clang" LD=ld.lld CROSS_COMPILE=$PWD/build_toolchain/gcc_64/bin/aarch64-linux-android- CROSS_COMPILE_ARM32=$PWD/build_toolchain/gcc_32/bin/arm-linux-androideabi- vendor/kona-perf_defconfig vendor/oplus.config vendor/droidspaces.config
    make ARCH=arm64 O=out CC="ccache clang" LD=ld.lld CROSS_COMPILE=$PWD/build_toolchain/gcc_64/bin/aarch64-linux-android- CROSS_COMPILE_ARM32=$PWD/build_toolchain/gcc_32/bin/arm-linux-androideabi- -j$(nproc --all)
}

do_package() {
    info "=== Execution Mode: AnyKernel3 ==="
    if [ -f "out/arch/arm64/boot/Image" ] && [ -f "out/arch/arm64/boot/dtbo.img" ] && [ -f "out/arch/arm64/boot/dtb" ]; then
        BUILD_TIMESTAMP=$(date +%s)

        if [ -d "Anykernel3" ]; then
            info "Detect Anykernel3, Skip."

            if [ -f "AnyKernel3/Image" ] || [ -f "AnyKernel3/dtbo.img" ] || [ -f "AnyKernel3/dtb" ]; then
                rm -f AnyKernel3/Image
                rm -f AnyKernel3/dtbo.img
                rm -f AnyKernel3/dtb
                info "Cleared older Image files."
            fi
        else
            git clone https://github.com/osm0sis/AnyKernel3.git -b master
            cd AnyKernel3
            git checkout dca9dc370838d919d56c1f59ec78b27a14a72c68
            cd ..
        fi

        cp -fp out/arch/arm64/boot/Image AnyKernel3/Image
        cp -fp out/arch/arm64/boot/dtbo.img AnyKernel3/dtbo.img
        cp -fp out/arch/arm64/boot/dtb AnyKernel3/dtb

        sed -i 's/do.devicecheck=1/do.devicecheck=0/g' Anykernel3/anykernel.sh
        sed -i 's!BLOCK=/dev/block/platform/omap/omap_hsmmc.0/by-name/boot;!BLOCK=auto;!g' Anykernel3/anykernel.sh
        sed -i 's/IS_SLOT_DEVICE=0;/is_slot_device=auto;/g' Anykernel3/anykernel.sh

        7za a -mx9 ${BUILD_DEVICE}_kona_${BUILD_TIMESTAMP}.zip AnyKernel3/*
        info "Packed Anykernel3, filename: ${BUILD_DEVICE}_kona_${BUILD_TIMESTAMP}.zip"
    else
        error "Could not found Image, dtbo.img and dtb. Please build kernel."
        exit 1
    fi
}

case "$ACTION" in
    get)
        do_get
        ;;
    build)
        do_build
        ;;
    package)
        do_package
        ;;
    all)
        do_get
        do_build
        do_package
        ;;
    *)
        error "Internal Error: unknown operation $ACTION"
        ;;
esac

info "The operation has been completed！"
exit 0
