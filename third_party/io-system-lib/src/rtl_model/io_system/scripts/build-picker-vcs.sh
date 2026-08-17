#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 RTL_INPUT_OR_FILELIST PICKER_OUT_DIR PICKER PICKER_TEMPLATE XSPCOMM_LIB" >&2
    exit 2
fi

rtl_input=$1
picker_out=$2
picker=$3
picker_template=$4
xspcomm_lib=$5

case "$rtl_input" in
    /*) rtl_abs=$rtl_input ;;
    *) rtl_abs=$PWD/$rtl_input ;;
esac

case "$picker_out" in
    /*) picker_abs=$picker_out ;;
    *) picker_abs=$PWD/$picker_out ;;
esac

case "$xspcomm_lib" in
    /*) xspcomm_abs=$xspcomm_lib ;;
    *) xspcomm_abs=$PWD/$xspcomm_lib ;;
esac

if [[ ! -e "$xspcomm_abs" ]]; then
    unitychip_root=${xspcomm_abs%/share/picker/lib/libxspcomm.so}
    if [[ -e "$unitychip_root/xcomm/libxspcomm.so" ]]; then
        xspcomm_abs=$unitychip_root/xcomm/libxspcomm.so
    fi
fi

mkdir -p "$(dirname "$picker_abs")"
rm -rf "$picker_abs"

case "$rtl_abs" in
    *.f|*.flist|*.txt)
        rtl_args=(--fs "$rtl_abs")
        ;;
    *)
        rtl_args=("$rtl_abs")
        ;;
esac

"$picker" export "${rtl_args[@]}" \
    --sname io_system_rtl_wrapper \
    --tdir "$picker_abs" \
    --lang cpp \
    --sim vcs \
    --autobuild false \
    --cp_lib false \
    -e -c \
    -w io_system_rtl_wrapper.fsdb \
    --sdir "$picker_template"

find "$picker_abs" -type f \( -name '*.cmake' -o -name CMakeLists.txt \) \
    -exec sed -i 's/^[[:space:]]*vcs -e VcsMain/bash vcs -e VcsMain/' {} +

make -C "$picker_abs" install \
    INSTALL_PREFIX="$picker_abs" \
    INSTALL_XSPCOMM_LIB="$xspcomm_abs"

rm -f "$picker_abs/libxspcomm.so"
cp -L "$xspcomm_abs" "$picker_abs/libxspcomm.so"

for required in \
    "$picker_abs/UT_io_system_rtl_wrapper.hpp" \
    "$picker_abs/UT_io_system_rtl_wrapper.cpp" \
    "$picker_abs/libUTio_system_rtl_wrapper.so" \
    "$picker_abs/libDPIio_system_rtl_wrapper.so" \
    "$picker_abs/libxspcomm.so" \
    "$picker_abs/vc_hdrs.h"; do
    if [[ ! -e "$required" ]]; then
        echo "missing picker/VCS output: $required" >&2
        exit 1
    fi
done

touch "$picker_abs/.picker-vcs.ready.stamp"
