"""마커 보드 — 인쇄용 ArUco 격자 생성 + ID→페이지 좌표 조회.

보드 좌표계: 왼쪽 위 원점(mm), x=오른쪽, y=아래쪽.
마커 ID는 행 우선 (ID = row * GRID_COLS + col).

    python3 marker_board.py board.png                       # 검정 마커 (기본)
    python3 marker_board.py board_y.png --color yellow      # 노란 마커 (블루 채널 검출용)
    python3 marker_board.py b8.png --size 8 --pitch 24      # 크기 실험
"""
import argparse

import cv2
import numpy as np

import config

DICT = cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, config.MARKER_DICT))
PX_PER_MM = config.PAGE_DPI / 25.4


def board_size_mm():
    w = config.GRID_COLS * config.MARKER_PITCH_MM
    h = config.GRID_ROWS * config.MARKER_PITCH_MM
    return w, h


def marker_center_mm(marker_id):
    """마커 ID → 보드 위 마커 중심 좌표 (mm)."""
    row, col = divmod(marker_id, config.GRID_COLS)
    x = (col + 0.5) * config.MARKER_PITCH_MM
    y = (row + 0.5) * config.MARKER_PITCH_MM
    return x, y


def render_board(color="black", blue=140):
    """인쇄용 보드 이미지 (흰 바탕 + 마커 격자).

    color="yellow": 마커의 검은 칸을 노랑으로 — 사람 눈엔 옅게, 카메라
    블루 채널에선 검정처럼 보인다 (reader --yellow 로 검출).
    blue: 노랑의 블루 성분 (0=진한 노랑 ~ 160=연한 크림, 190부터 검출 실패.
          동화책 삽화 합성 실험에서 160까지 25/25 검출 확인)
    """
    w_mm, h_mm = board_size_mm()
    h_px, w_px = int(h_mm * PX_PER_MM), int(w_mm * PX_PER_MM)
    gray = np.full((h_px, w_px), 255, np.uint8)
    size_px = int(config.MARKER_SIZE_MM * PX_PER_MM)
    for mid in range(config.GRID_COLS * config.GRID_ROWS):
        cx, cy = marker_center_mm(mid)
        x0 = int(cx * PX_PER_MM - size_px / 2)
        y0 = int(cy * PX_PER_MM - size_px / 2)
        marker = cv2.aruco.generateImageMarker(DICT, mid, size_px)
        gray[y0:y0 + size_px, x0:x0 + size_px] = marker
    if color == "black":
        return gray
    # 노랑: 검은 픽셀을 노랑으로, 흰 픽셀은 그대로 흰색
    img = np.full((h_px, w_px, 3), 255, np.uint8)
    dark = gray < 128
    img[dark] = (blue, 235, 250)  # BGR — 블루가 낮을수록 블루 채널에서 진하게 보임
    return img


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="board.png")
    ap.add_argument("--color", choices=["black", "yellow"], default="black")
    ap.add_argument("--blue", type=int, default=140, help="노랑의 연하기 0~160 (클수록 연함)")
    ap.add_argument("--size", type=float, help="마커 한 변 mm (config 덮어씀)")
    ap.add_argument("--pitch", type=float, help="마커 간격 mm (config 덮어씀)")
    ap.add_argument("--cols", type=int, help="마커 격자 열 수 (config 덮어씀)")
    ap.add_argument("--rows", type=int, help="마커 격자 행 수 (config 덮어씀)")
    args = ap.parse_args()
    if args.size:
        config.MARKER_SIZE_MM = args.size
    if args.pitch:
        config.MARKER_PITCH_MM = args.pitch
    if args.cols:
        config.GRID_COLS = args.cols
    if args.rows:
        config.GRID_ROWS = args.rows
    img = render_board(args.color, args.blue)
    cv2.imwrite(args.out, img)
    w_mm, h_mm = board_size_mm()
    print(f"saved {args.out}  ({img.shape[1]}x{img.shape[0]}px = {w_mm:.0f}x{h_mm:.0f}mm @ {config.PAGE_DPI}dpi)")
    print(f"markers: {config.GRID_COLS}x{config.GRID_ROWS}, color {args.color}, "
          f"size {config.MARKER_SIZE_MM}mm, pitch {config.MARKER_PITCH_MM}mm")
    print("인쇄 시 '실제 크기(100%)'로 출력할 것 — 크기가 틀어지면 좌표 스케일이 어긋남")
