"""마커 보드 — 인쇄용 ArUco 격자 생성 + ID→페이지 좌표 조회.

보드 좌표계: 왼쪽 위 원점(mm), x=오른쪽, y=아래쪽.
마커 ID는 행 우선 (ID = row * GRID_COLS + col).

    python3 marker_board.py board.png   # 인쇄용 PNG 생성 (PAGE_DPI 기준 실측 크기)
"""
import sys

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


def render_board():
    """인쇄용 보드 이미지 (흰 바탕 + 마커 격자)."""
    w_mm, h_mm = board_size_mm()
    img = np.full((int(h_mm * PX_PER_MM), int(w_mm * PX_PER_MM)), 255, np.uint8)
    size_px = int(config.MARKER_SIZE_MM * PX_PER_MM)
    for mid in range(config.GRID_COLS * config.GRID_ROWS):
        cx, cy = marker_center_mm(mid)
        x0 = int(cx * PX_PER_MM - size_px / 2)
        y0 = int(cy * PX_PER_MM - size_px / 2)
        marker = cv2.aruco.generateImageMarker(DICT, mid, size_px)
        img[y0:y0 + size_px, x0:x0 + size_px] = marker
    return img


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "board.png"
    img = render_board()
    cv2.imwrite(out, img)
    w_mm, h_mm = board_size_mm()
    print(f"saved {out}  ({img.shape[1]}x{img.shape[0]}px = {w_mm:.0f}x{h_mm:.0f}mm @ {config.PAGE_DPI}dpi)")
    print(f"markers: {config.GRID_COLS}x{config.GRID_ROWS}, "
          f"size {config.MARKER_SIZE_MM}mm, pitch {config.MARKER_PITCH_MM}mm")
    print("인쇄 시 '실제 크기(100%)'로 출력할 것 — 크기가 틀어지면 좌표 스케일이 어긋남")
