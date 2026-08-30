#!/bin/bash
# PC(Git Bash)에서 한 번에: pio 빌드 산출물 → 파이로 복사 → 파이에서 flash_fw.sh 실행
#   bash pi/deploy_fw.sh                          # 기본 주소
#   bash pi/deploy_fw.sh chyoon_pi4b8g@192.168.0.63   # IP 직접 지정 (.local 안 풀릴 때)
# 먼저 `pio run` 으로 빌드해 둘 것. boot_app0.bin은 PlatformIO 패키지에서 가져온다.
set -e
PI=${1:-chyoon_pi4b8g@raspberrypi4b8gb.local}
cd "$(dirname "$0")/.."
B=.pio/build/esp32s3
APP0=$(ls ~/.platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin | head -1)
for f in "$B/bootloader.bin" "$B/partitions.bin" "$B/firmware.bin" "$APP0"; do
  [ -f "$f" ] || { echo "[ERR] $f 없음 — 먼저 pio run"; exit 1; }
done
ssh "$PI" 'mkdir -p ~/braille/fw'
scp "$B/bootloader.bin" "$B/partitions.bin" "$B/firmware.bin" "$APP0" "$PI:~/braille/fw/"
scp pi/flash_fw.sh "$PI:~/braille/"
ssh "$PI" 'bash ~/braille/flash_fw.sh'
