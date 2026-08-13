# ============================================================
# braille_cam.py — Pi 4 카메라 → 점자 패턴 → ESP32 시리얼 전송
#
# 파이프라인: 카메라 → QR 인식(OpenCV) → 첫 글자 → 점자 6점 →
#             3x3 패턴 문자열 → "B:XXXXXXXXX\n" 시리얼 전송
#
# 실행 (Pi):
#   pip install -r requirements.txt
#   python3 braille_cam.py --port /dev/ttyACM0
#   (CSI 카메라 모듈이 cv2로 안 잡히면: libcamerify python3 braille_cam.py)
#
# 실행 (Windows에서 웹캠으로 미리 테스트):
#   python braille_cam.py --port COM5
#
# --no-camera : 카메라 없이 키보드 입력으로 프로토콜만 테스트
# ============================================================
import argparse
import sys
import time

import serial

# 영어 1종 점자: 글자 -> 점 번호 집합 (점 1~6)
# 점 배치(표준):  1 4
#                2 5
#                3 6
BRAILLE = {
    'a': {1}, 'b': {1, 2}, 'c': {1, 4}, 'd': {1, 4, 5}, 'e': {1, 5},
    'f': {1, 2, 4}, 'g': {1, 2, 4, 5}, 'h': {1, 2, 5}, 'i': {2, 4},
    'j': {2, 4, 5}, 'k': {1, 3}, 'l': {1, 2, 3}, 'm': {1, 3, 4},
    'n': {1, 3, 4, 5}, 'o': {1, 3, 5}, 'p': {1, 2, 3, 4},
    'q': {1, 2, 3, 4, 5}, 'r': {1, 2, 3, 5}, 's': {2, 3, 4},
    't': {2, 3, 4, 5}, 'u': {1, 3, 6}, 'v': {1, 2, 3, 6},
    'w': {2, 4, 5, 6}, 'x': {1, 3, 4, 6}, 'y': {1, 3, 4, 5, 6},
    'z': {1, 3, 5, 6},
}

# 점 번호 -> 3x3 그리드 (행, 열). 펌웨어 패턴은 행 우선 9자리.
# 6점 셀을 왼쪽 2열에 배치, 셋째 열은 예비(항상 0).
DOT_POS = {1: (0, 0), 2: (1, 0), 3: (2, 0), 4: (0, 1), 5: (1, 1), 6: (2, 1)}


def char_to_pattern(ch):
    """글자 하나 -> 'XXXXXXXXX' 9자리 패턴. 모르는 글자는 None."""
    dots = BRAILLE.get(ch.lower())
    if dots is None:
        return None
    grid = [['0'] * 3 for _ in range(3)]
    for d in dots:
        r, c = DOT_POS[d]
        grid[r][c] = '1'
    return ''.join(''.join(row) for row in grid)


def send_pattern(ser, pattern, label=""):
    ser.write(f"B:{pattern}\n".encode())
    time.sleep(0.05)
    reply = ser.read_all().decode(errors="replace").strip()
    print(f"[TX] {label!r} -> B:{pattern}  |  {reply}")


def run_keyboard(ser):
    print("카메라 없이 키보드 테스트 모드. a~z 입력(엔터), 빈 줄 = 전부 내림, Ctrl+C 종료")
    while True:
        line = input("> ").strip()
        if not line:
            send_pattern(ser, "0" * 9, "clear")
            continue
        pat = char_to_pattern(line[0])
        if pat is None:
            print(f"'{line[0]}' 는 매핑 없음 (a~z만)")
            continue
        send_pattern(ser, pat, line[0])


def run_camera(ser, cam_index):
    import cv2
    cap = cv2.VideoCapture(cam_index)
    if not cap.isOpened():
        sys.exit("카메라를 열 수 없음. CSI 모듈이면 libcamerify로 실행하거나 --no-camera로 프로토콜만 테스트.")
    detector = cv2.QRCodeDetector()
    last_sent = None
    print("QR을 카메라에 보여주세요. QR 내용의 첫 글자를 출력합니다. Ctrl+C 종료")
    while True:
        ok, frame = cap.read()
        if not ok:
            time.sleep(0.1)
            continue
        text, _, _ = detector.detectAndDecode(frame)
        if text:
            ch = text.strip()[:1]
            pat = char_to_pattern(ch) if ch else None
            if pat and pat != last_sent:
                send_pattern(ser, pat, ch)
                last_sent = pat
        time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser(description="카메라 -> 점자 패턴 -> ESP32")
    ap.add_argument("--port", default="/dev/ttyACM0", help="ESP32 시리얼 포트 (Pi: /dev/ttyACM0 또는 /dev/ttyUSB0, Windows: COM5)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--camera", type=int, default=0, help="cv2.VideoCapture 인덱스")
    ap.add_argument("--no-camera", action="store_true", help="키보드 입력으로 프로토콜만 테스트")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(1.5)  # ESP32 리셋 대기 (포트 열면 보드가 리셋됨)
    ser.reset_input_buffer()

    try:
        if args.no_camera:
            run_keyboard(ser)
        else:
            run_camera(ser, args.camera)
    except KeyboardInterrupt:
        pass
    finally:
        ser.write(b"B:000000000\n")  # 종료 시 전부 내림 (홀드 전류/발열 방지)
        ser.close()
        print("\n전부 내리고 종료")


if __name__ == "__main__":
    main()
