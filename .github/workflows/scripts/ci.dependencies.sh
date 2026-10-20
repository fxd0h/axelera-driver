#!/bin/bash
# Kernel headers used only for the metis-dkms build dry-run that verifies the
# module compiles. CI defines KVER in the workflow env (single source of truth);
# the value below is only a fallback for local runs.
KVER="${KVER:-6.14.0-27-generic}"
echo "Using kernel headers: linux-headers-${KVER}" >&2

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  cmake build-essential debhelper devscripts fakeroot dkms dh-dkms \
  "linux-headers-${KVER}"
