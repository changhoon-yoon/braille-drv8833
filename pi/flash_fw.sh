#!/bin/bash
# 파이에서 ESP32-S3 펌웨어 굽기 (파이 USB에 꽂힌 보드, /dev/ttyACM0)
#   bash ~/braille/flash_fw.sh [펌웨어폴더] [포트]
# 순서: 리더 서비스 정지(포트 해제) → esptool 쓰기 → 서비스 재시작 → 상태 출력
# 데비안 esptool 패키지는 S3 스텁 JSON이 빠져 있어 --no-stub 필수 (없으면 연결 실패)
set -e
FW=${1:-$HOME/braille/fw}
PORT=${2:-/dev/ttyACM0}
for f in bootloader partitions boot_app0 firmware; do
  [ -f "$FW/$f.bin" ] || { echo "[ERR] $FW/$f.bin 없음 — PC에서 pi/deploy_fw.sh 로 보낼 것"; exit 1; }
done
sudo systemctl stop braille-reader 2>/dev/null || true
pkill -f '[m]initerm' 2>/dev/null || true       # 수동 모니터가 포트를 잡고 있으면 해제
esptool --chip esp32s3 --port "$PORT" --baud 460800 --no-stub write_flash \
  0x0     "$FW/bootloader.bin" \
  0x8000  "$FW/partitions.bin" \
  0xe000  "$FW/boot_app0.bin" \
  0x10000 "$FW/firmware.bin"
sudo systemctl start braille-reader
sleep 5
echo "[SVC] braille-reader: $(systemctl is-active braille-reader)"
journalctl -u braille-reader -n 8 --no-pager -o cat
