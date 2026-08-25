"""ESP32 점자 모듈 시리얼 링크.

- B:XXXXXXXXX = 전체 동기화 (모든 자리 무조건 발사 — 크로스토크 재정렬)
- D:XXXXXXXXX = 디프 갱신 (바뀐 자리만 — 쓸기 모드용, 빠름)
포트를 열면 보드가 자동 리셋되므로 부팅 대기 후 자동테스트를 정지한다.
"""
import time

import serial

import config


class BrailleLink:
    def __init__(self, port=None, quiet=False):
        self.ser = serial.Serial(port or config.SERIAL_PORT, config.BAUD, timeout=0.1)
        self.quiet = quiet
        self._boot()

    def _boot(self):
        self._read_until("[AUTO]", 6)   # 부팅 배너 + 자동시작 대기
        self.ser.write(b"t")            # 부팅 자동테스트 정지
        self._read_until("정지", 2)
        self.ser.reset_input_buffer()

    def _read_until(self, marker, timeout):
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            buf += self.ser.read(256)
            if marker.encode() in buf:
                break
        return buf.decode("utf-8", errors="replace")

    def send(self, pattern, diff=True):
        """9자리 '0'/'1' 패턴 전송, [OK]/[ERR] 응답 반환."""
        assert len(pattern) == 9 and set(pattern) <= {"0", "1"}, pattern
        prefix = b"D:" if diff else b"B:"
        self.ser.write(prefix + pattern.encode() + b"\n")
        reply = self._read_until("[", 3).strip()
        if not self.quiet:
            print(f"  TX {prefix.decode()}{pattern} -> {reply.splitlines()[-1] if reply else '(무응답)'}")
        return reply

    def all_down(self):
        return self.send("0" * 9, diff=False)

    def close(self):
        self.ser.close()
