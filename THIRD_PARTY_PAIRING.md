# Astro Remote pairing third-party notice

The experimental Astro Remote self-registration path uses the Remote Play registration protocol and `rpcrypt.c` implementation from **chiaki-ng** by the chiaki-ng contributors.

Pinned upstream revision: `6547d8aed03503646fe1043512616e26c03fa9db`

Upstream: `https://github.com/streetpea/chiaki-ng`

The upstream files carry `SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL`. They are fetched at build time by `scripts/fetch_chiaki_rpcrypt.sh` and compiled into the experimental Astro Remote ELF. Keep the upstream copyright/license notices and comply with the chiaki-ng license when distributing a build containing that code.

Astro's small mbedTLS compatibility layer only supplies AES-128-CFB and HMAC-SHA256 primitives required by the upstream `rpcrypt.c` on the PS5 payload toolchain.
