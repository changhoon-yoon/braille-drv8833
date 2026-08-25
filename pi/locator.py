"""로케이터 — 카메라 프레임에서 ArUco 마커를 찾아 카메라 중심의 보드 좌표(mm)를 계산.

원리 (마커 1개면 충분):
  - ID          → 그 마커가 보드 어디에 인쇄돼 있는지 (marker_board.marker_center_mm)
  - 네 모서리   → 화면상 마커 중심·축 방향·크기(px) → 픽셀↔mm 환산 + 회전 보정
  - 화면 중심이 마커 중심에서 마커 축 기준으로 몇 mm 떨어졌는지 투영
여러 개 보이면 각 추정을 평균해 정밀도를 올린다.
"""
import cv2
import numpy as np

import config
import marker_board

DICT = cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, config.MARKER_DICT))

# OpenCV 4.7+ 신 API / 구 API 겸용
try:
    _detector = cv2.aruco.ArucoDetector(DICT, cv2.aruco.DetectorParameters())

    def _detect(gray):
        return _detector.detectMarkers(gray)
except AttributeError:  # pragma: no cover
    _params = cv2.aruco.DetectorParameters_create()

    def _detect(gray):
        return cv2.aruco.detectMarkers(gray, DICT, parameters=_params)


def locate(gray, detail=False):
    """그레이스케일 프레임 → (x_mm, y_mm, 마커 수) 또는 None (마커 0개).

    detail=True면 (결과, corners, ids)를 돌려줘 뷰어 오버레이에 쓴다.
    """
    corners, ids, _ = _detect(gray)
    if ids is None or len(ids) == 0:
        return (None, corners, ids) if detail else None
    h, w = gray.shape[:2]
    img_center = np.array([w / 2.0, h / 2.0])

    estimates = []
    for quad, mid in zip(corners, ids.flatten()):
        mid = int(mid)
        if mid >= config.GRID_COLS * config.GRID_ROWS:
            continue  # 이 보드의 마커가 아님
        pts = quad[0]  # 4x2: TL, TR, BR, BL (마커 자체 좌표계 기준)
        center_px = pts.mean(axis=0)
        # 마커 축 벡터 (평행사변형 평균으로 원근 왜곡 완화)
        ax = (pts[1] - pts[0] + pts[2] - pts[3]) / 2.0  # 마커 +x (보드 오른쪽)
        ay = (pts[3] - pts[0] + pts[2] - pts[1]) / 2.0  # 마커 +y (보드 아래)
        len_x, len_y = np.linalg.norm(ax), np.linalg.norm(ay)
        if len_x < 4 or len_y < 4:
            continue  # 너무 작게 잡힌 마커는 신뢰 불가
        # 화면 중심이 마커 중심에서 얼마나 떨어졌는지를 마커 축으로 분해 → mm
        d = img_center - center_px
        off_x_mm = float(np.dot(d, ax / len_x)) / (len_x / config.MARKER_SIZE_MM)
        off_y_mm = float(np.dot(d, ay / len_y)) / (len_y / config.MARKER_SIZE_MM)
        mx, my = marker_board.marker_center_mm(mid)
        estimates.append((mx + off_x_mm, my + off_y_mm))

    if not estimates:
        return (None, corners, ids) if detail else None
    arr = np.array(estimates)
    x, y = arr.mean(axis=0)
    result = (float(x), float(y), len(estimates))
    return (result, corners, ids) if detail else result
