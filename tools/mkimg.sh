#!/usr/bin/env bash
set -euo pipefail

IMG=${1:-build/disk.img}
SIZE=${2:-10G}

mkdir -p "$(dirname "$IMG")"

if [ -f "$IMG" ]; then
  rm -f "$IMG"
fi
truncate -s "$SIZE" "$IMG"

if command -v parted >/dev/null 2>&1; then
  parted -s "$IMG" mklabel gpt
  parted -s "$IMG" mkpart primary ext4 1MiB 100%

  LOOP=$(sudo losetup --show -fP "$IMG")
  sudo mkfs.ext4 -F "${LOOP}p1"
  sudo losetup -d "$LOOP"

  echo "Created $IMG with GPT + ext4 partition"
  exit 0
fi

if command -v sfdisk >/dev/null 2>&1; then
  if printf 'label: gpt\nstart=2048, type=8300\n' | sfdisk --no-reread --force "$IMG"; then
    LOOP=$(sudo losetup --show -fP "$IMG")
    if [ -b "${LOOP}p1" ]; then
      sudo mkfs.ext4 -F "${LOOP}p1"
      sudo losetup -d "$LOOP"
      echo "Created $IMG with GPT + ext4 partition (sfdisk)"
      exit 0
    fi
    sudo losetup -d "$LOOP"
  fi
  echo "sfdisk failed; falling back to raw ext4"
fi

LOOP=$(sudo losetup --show -f "$IMG")
sudo mkfs.ext4 -F "$LOOP"
sudo losetup -d "$LOOP"
echo "Created $IMG with raw ext4 (no partition table)"
