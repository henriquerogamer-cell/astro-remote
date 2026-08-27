#!/bin/sh
set -eu

CHIaki_COMMIT="6547d8aed03503646fe1043512616e26c03fa9db"
BASE="https://raw.githubusercontent.com/streetpea/chiaki-ng/${CHIaki_COMMIT}"
mkdir -p third_party/chiaki/src third_party/chiaki/include/chiaki
wget -q "${BASE}/lib/src/rpcrypt.c" -O third_party/chiaki/src/rpcrypt.c
wget -q "${BASE}/lib/include/chiaki/rpcrypt.h" -O third_party/chiaki/include/chiaki/rpcrypt.h
wget -q "${BASE}/lib/include/chiaki/common.h" -O third_party/chiaki/include/chiaki/common.h
test -s third_party/chiaki/src/rpcrypt.c
test -s third_party/chiaki/include/chiaki/rpcrypt.h
test -s third_party/chiaki/include/chiaki/common.h
