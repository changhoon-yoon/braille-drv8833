"""무한 캔버스 점자 리더 — 메인 루프.

  python3 reader.py --selftest          # 카메라·시리얼 없이 로케이터 수학 검증
  python3 reader.py --sim               # 가상 쓸기 경로 → ESP32 전송 (카메라 없이)
  python3 reader.py --sim --no-serial   # 시리얼도 없이 패턴 출력만
  python3 reader.py --camera            # 실전: 카메라 → 마커 → 좌표 → 창 → 전송

흐름: 카메라 프레임 → locator(마커→mm) → 셀 양자화(히스테리시스)
      → virtual_page 3×3 크롭 → D: 디프 전송 (멈추면 B: 재동기화)
"""
import argparse
import time

import config
from virtual_page import VirtualPage


class CellQuantizer:
    """mm 좌표 → 점자 칸. 칸 경계 떨림 방지용 히스테리시스 포함."""

    def __init__(self):
        self.col = None
        self.row = None

    def update(self, x_mm, y_mm):
        fc = x_mm / config.MM_PER_CELL
        fr = y_mm / config.MM_PER_CELL
        if self.col is None:
            self.col, self.row = round(fc), round(fr)
            return True
        moved = False
        if abs(fc - self.col) > 0.5 + config.HYSTERESIS:
            self.col = round(fc)
            moved = True
        if abs(fr - self.row) > 0.5 + config.HYSTERESIS:
            self.row = round(fr)
            moved = True
        return moved


def run_selftest():
    """보드 이미지를 합성해 여러 위치·회전에서 로케이터 오차를 검증 (하드웨어 불필요)."""
    import cv2
    import numpy as np
    import locator
    import marker_board

    board = marker_board.render_board()
    px_per_mm = marker_board.PX_PER_MM
    w_mm, h_mm = marker_board.board_size_mm()
    view_w, view_h = 640, 480

    cases = [(40, 40, 0), (90, 130, 0), (150, 220, 0), (65, 95, 12), (120, 180, -18)]
    failures = 0
    for tx, ty, angle in cases:
        # 보드에서 (tx,ty)mm가 화면 중앙에 오는 카메라 뷰를 합성 (0.5x 축소 = 카메라 거리)
        scale = 0.5
        m = cv2.getRotationMatrix2D((tx * px_per_mm, ty * px_per_mm), angle, scale)
        m[0, 2] += view_w / 2 - tx * px_per_mm
        m[1, 2] += view_h / 2 - ty * px_per_mm
        view = cv2.warpAffine(board, m, (view_w, view_h), borderValue=255)
        result = locator.locate(view)
        if result is None:
            print(f"  ({tx:3},{ty:3}) rot{angle:+3}° -> 마커 검출 실패  FAIL")
            failures += 1
            continue
        x, y, n = result
        err = ((x - tx) ** 2 + (y - ty) ** 2) ** 0.5
        ok = err < 2.0
        print(f"  ({tx:3},{ty:3}) rot{angle:+3}° -> ({x:6.1f},{y:6.1f}) "
              f"마커{n}개 오차 {err:.2f}mm  {'OK' if ok else 'FAIL'}")
        failures += 0 if ok else 1
    print(f"selftest: {len(cases) - failures}/{len(cases)} 통과 (허용 오차 2mm)")
    return failures == 0


def make_sender(no_serial):
    if no_serial:
        def send(pattern, diff=True):
            print(f"  TX {'D' if diff else 'B'}:{pattern}")
        return send, lambda: None
    from braille_link import BrailleLink
    link = BrailleLink()
    return link.send, link.close


def run_sim(no_serial):
    """마커/카메라 없이: 가상 좌표가 페이지를 쓸고 다니며 창 패턴을 전송."""
    page = VirtualPage()
    send, close = make_sender(no_serial)
    q = CellQuantizer()
    last = None
    # 줄1 쓸기 → 줄2 쓸기 → 하트 가장자리 근처 통과
    path = [(c, 3) for c in range(2, 19)] + \
           [(c, 7) for c in range(2, 13)] + \
           [(c, 10) for c in range(14, 26)]
    try:
        for col, row in path:
            q.update(col * config.MM_PER_CELL, row * config.MM_PER_CELL)
            pattern = page.window(q.col, q.row)
            if pattern != last:
                print(f"[SIM] 창 중심 ({q.col:2},{q.row:2})")
                send(pattern, diff=True)
                last = pattern
            time.sleep(0.25)
        send("0" * 9, diff=False)  # 종료: 전체 내림 + 재동기화
    finally:
        close()


def _annotate(cv2, frame, corners, ids, result, cell, pattern):
    """뷰어용 오버레이 — 마커 테두리, 화면 중심 십자, 좌표/셀 텍스트, 3×3 창 미리보기."""
    h, w = frame.shape[:2]
    if ids is not None and len(ids):
        cv2.aruco.drawDetectedMarkers(frame, corners, ids)
    cv2.drawMarker(frame, (w // 2, h // 2), (0, 200, 255),
                   cv2.MARKER_CROSS, 24, 2)
    if result:
        x, y, n = result
        # 실측: 마커 한 변 px → 현재 높이의 시야(mm) 환산 (크기·간격 튜닝 근거)
        import numpy as np
        sides = [float(np.linalg.norm(q[0][0] - q[0][1])) for q in corners] if corners else []
        if sides:
            side_px = sum(sides) / len(sides)
            fov_w = w / side_px * config.MARKER_SIZE_MM
            fov_h = h / side_px * config.MARKER_SIZE_MM
            text = (f"x={x:6.1f} y={y:6.1f}mm  mk={n}  cell=({cell[0]},{cell[1]})  "
                    f"side={side_px:.0f}px  FOV={fov_w:.0f}x{fov_h:.0f}mm")
        else:
            text = f"x={x:6.1f}mm y={y:6.1f}mm  markers={n}  cell=({cell[0]},{cell[1]})"
        color = (80, 220, 80)
    else:
        text = "NO MARKER - hold steady / adjust height"
        color = (60, 60, 230)
    cv2.rectangle(frame, (0, 0), (w, 26), (20, 20, 20), -1)
    cv2.putText(frame, text, (8, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)
    if pattern:  # 오른쪽 아래에 현재 3×3 창 상태
        for i, ch in enumerate(pattern):
            cx = w - 62 + (i % 3) * 22
            cy = h - 62 + (i // 3) * 22
            if ch == "1":
                cv2.circle(frame, (cx, cy), 8, (60, 140, 240), -1)
            else:
                cv2.circle(frame, (cx, cy), 8, (90, 90, 90), 1)


def run_camera(no_serial, view=False, yellow=False):
    import cv2
    import locator

    # 저전압 등으로 USB가 재연결되면 카메라 번호가 바뀐다(video0→video1)
    # → 고정 인덱스 대신 열리는 카메라를 스캔해서 사용
    cap = None
    for idx in dict.fromkeys([config.CAMERA_INDEX, 0, 1, 2, 3]):
        c = cv2.VideoCapture(idx)
        c.set(cv2.CAP_PROP_FRAME_WIDTH, config.FRAME_WIDTH)
        c.set(cv2.CAP_PROP_FRAME_HEIGHT, config.FRAME_HEIGHT)
        if c.isOpened() and c.read()[0]:
            print(f"카메라 index {idx} 사용")
            cap = c
            break
        c.release()
    if cap is None:
        raise SystemExit("카메라를 열 수 없음 (index 0~3 스캔 실패)")
    read_fails = 0

    streamer = None
    if view:
        from viewer import Streamer
        streamer = Streamer()
        print(f"라이브 뷰: http://<파이IP>:{streamer.port}/  (브라우저로 접속)")

    page = VirtualPage()
    send, close = make_sender(no_serial)
    q = CellQuantizer()
    last_pattern = None
    last_change = time.time()
    resynced = False
    lost_since = None

    print("카메라 리더 시작 — Ctrl+C로 종료")
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                read_fails += 1
                if read_fails > 100:  # 카메라가 뽑힘 — 종료하면 systemd가 재시작+재스캔
                    raise SystemExit("카메라 응답 없음 — 재시작으로 재스캔")
                continue
            read_fails = 0
            # 노란 마커: 블루 채널에서 검정으로 보임 (노랑 = 파란빛 흡수)
            gray = frame[:, :, 0] if yellow else cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            result, corners, ids = locator.locate(gray, detail=True)
            now = time.time()

            if result is not None:
                lost_since = None
                x_mm, y_mm, n = result
                if q.update(x_mm, y_mm):
                    pattern = page.window(q.col, q.row)
                    if pattern != last_pattern:
                        print(f"\n[POS] ({x_mm:6.1f},{y_mm:6.1f})mm 마커{n} -> 셀({q.col},{q.row})")
                        send(pattern, diff=True)
                        last_pattern = pattern
                        last_change = now
                        resynced = False
                elif not resynced and now - last_change > 2.0 and last_pattern:
                    send(last_pattern, diff=False)  # 정지 상태: 전체 재동기화 1회
                    resynced = True
            else:
                if lost_since is None:
                    lost_since = now
                elif now - lost_since > 1.0:
                    print("\r[LOST] 마커 없음 — 마지막 위치 유지          ", end="")

            if streamer is not None:
                cell = (q.col, q.row) if q.col is not None else ("-", "-")
                _annotate(cv2, frame, corners, ids, result, cell, last_pattern)
                ok2, jpg = cv2.imencode(".jpg", frame,
                                        [cv2.IMWRITE_JPEG_QUALITY, 70])
                if ok2:
                    streamer.publish(jpg.tobytes())
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--selftest", action="store_true")
    mode.add_argument("--sim", action="store_true")
    mode.add_argument("--camera", action="store_true")
    ap.add_argument("--no-serial", action="store_true")
    ap.add_argument("--view", action="store_true",
                    help="브라우저 라이브 뷰 (http://<파이IP>:8501)")
    ap.add_argument("--yellow", action="store_true",
                    help="노란 마커 보드 — 블루 채널로 검출")
    ap.add_argument("--marker-size", type=float, help="마커 한 변 mm (config 덮어씀)")
    ap.add_argument("--marker-pitch", type=float, help="마커 간격 mm (config 덮어씀)")
    ap.add_argument("--grid-cols", type=int, help="마커 격자 열 수 (보드와 일치 필수)")
    ap.add_argument("--grid-rows", type=int, help="마커 격자 행 수 (보드와 일치 필수)")
    args = ap.parse_args()

    if args.marker_size:
        config.MARKER_SIZE_MM = args.marker_size
    if args.marker_pitch:
        config.MARKER_PITCH_MM = args.marker_pitch
    if args.grid_cols:
        config.GRID_COLS = args.grid_cols
    if args.grid_rows:
        config.GRID_ROWS = args.grid_rows

    if args.selftest:
        raise SystemExit(0 if run_selftest() else 1)
    elif args.sim:
        run_sim(args.no_serial)
    else:
        run_camera(args.no_serial, view=args.view, yellow=args.yellow)
