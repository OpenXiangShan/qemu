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
        # Picker's filelist parser accepts source paths only, whereas VCS
        # needs the include directories and defines in the complete list.
        # Parse just the public top and pass the rest unchanged to VCS.
        top_source=$(sed -n '\|/io_system_rtl_wrapper.sv$|p' "$rtl_abs")
        [[ -f "$top_source" ]] || { echo "missing unique wrapper in $rtl_abs" >&2; exit 1; }
        rtl_args=("$top_source")
        ;;
    *)
        rtl_args=("$rtl_abs")
        ;;
esac

trace_args=()
if [[ ${IO_SYSTEM_RTL_WAVE:-0} == 1 ]]; then
    trace_args=(-w "${IO_SYSTEM_RTL_WAVE_FILE:-io_system_rtl_wrapper.fsdb}")
fi
coverage_args=()
if [[ ${IO_SYSTEM_RTL_COVERAGE:-0} == 1 ]]; then
    coverage_args=(-c)
fi

"$picker" export "${rtl_args[@]}" \
    --sname io_system_rtl_wrapper \
    --tdir "$picker_abs" \
    --lang cpp \
    --sim vcs \
    --autobuild false \
    --cp_lib false \
    --cflag "-DENABLE_XCOROUTINE=true -I$picker_template/../include" \
    -e "${coverage_args[@]}" "${trace_args[@]}" \
    --sdir "$picker_template"

if [[ -n ${top_source:-} ]]; then
    sed '\|/io_system_rtl_wrapper.sv$|d' "$rtl_abs" > "$picker_abs/filelist.f"
fi

find "$picker_abs" -type f \( -name '*.cmake' -o -name CMakeLists.txt \) \
    -exec sed -i 's/^[[:space:]]*vcs -e VcsMain/bash vcs -e VcsMain/' {} +
# Packages must precede the generated copy of our public wrapper.
sed -i 's/${ModuleName}_top.sv ${ModuleName}.v -f filelist.f/-f filelist.f ${ModuleName}.v ${ModuleName}_top.sv/' \
    "$picker_abs/cmake/vcs.cmake"
# The VCS shared libraries reference one another. Ubuntu's default as-needed
# otherwise drops libuclinative before libvcsnew and the model cannot load.
sed -i 's/-LDFLAGS "-shared/-LDFLAGS "-shared -Wl,--no-as-needed/' \
    "$picker_abs/cmake/vcs.cmake"

# Picker's VCS destructor invokes an exported SV task containing $finish,
# which exits the embedding QEMU process. Keep the VCS context resident until
# process exit while releasing the picker host objects normally. Reset/reuse
# is supported through the existing handle; opening a second VCS instance is
# not supported by this package.
python3 - "$picker_abs/dut_base.cpp" <<'PY'
from pathlib import Path
import re
import sys
p = Path(sys.argv[1])
s = p.read_text()
s, count = re.subn(r'int DutVcsBase::Finish\(\)\s*\{.*?\n\};',
                  'int DutVcsBase::Finish()\n{\n    return 0; // Embedded host owns process lifetime.\n};',
                  s, count=1, flags=re.S)
if count != 1:
    raise SystemExit('unrecognized picker VCS Finish implementation')
p.write_text(s)
PY

make -C "$picker_abs" build \
    NPROC="${IO_SYSTEM_BUILD_JOBS:-8}" \
    INSTALL_XSPCOMM_LIB="$xspcomm_abs"

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
