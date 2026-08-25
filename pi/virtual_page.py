"""가상 페이지 — 점자 점(0/1) 비트맵과 3×3 창 크롭.

글자든 그림 윤곽이든 결과물은 같은 격자 위의 0/1 점이다.
v1 데모 콘텐츠: 의사 점자 두 줄 + 하트 윤곽 (컨셉 아티팩트와 동일).
나중에 한글→점자 변환(liblouis ko 테이블)이 이 격자를 채우게 된다.
"""
COLS, ROWS = 27, 16


def _glyph_bits(k):
    """결정적 의사 점자 글리프 (2x3 = 6점)."""
    h = ((k + 7) * 2654435761) & 0xFFFFFFFF
    b = (h >> 3) & 0x3F
    return b or 0x15


def _heart_f(c, r, hc=19.5, hr=10.2, hs=4.6):
    u = (c - hc) / hs
    v = -(r - hr) / hs + 0.12
    q = u * u + v * v - 1
    return q ** 3 - u * u * v ** 3


def build_demo_page():
    grid = [[0] * COLS for _ in range(ROWS)]

    def put_line(row, col0, n, seed):
        for k in range(n):
            bits = _glyph_bits(k + seed)
            for d in range(6):
                if bits & (1 << d):
                    grid[row + d // 2][col0 + k * 3 + d % 2] = 1

    put_line(2, 2, 6, 0)    # 줄 1
    put_line(6, 2, 4, 11)   # 줄 2

    # 하트: 음함수 채움의 경계 셀만 남겨 윤곽 생성
    for r in range(5, ROWS):
        for c in range(13, COLS):
            if _heart_f(c, r) <= 0 and any(
                _heart_f(c + dc, r + dr) > 0
                for dc, dr in ((-1, 0), (1, 0), (0, -1), (0, 1))
            ):
                grid[r][c] = 1
    return grid


class VirtualPage:
    def __init__(self, grid=None):
        self.grid = grid if grid is not None else build_demo_page()
        self.rows = len(self.grid)
        self.cols = len(self.grid[0])

    def clamp(self, col, row):
        return (max(1, min(self.cols - 2, col)), max(1, min(self.rows - 2, row)))

    def window(self, col, row):
        """(col,row) 중심 3×3 → 행 우선 9자리 '0'/'1' 문자열."""
        col, row = self.clamp(col, row)
        return "".join(
            str(self.grid[row + dr][col + dc])
            for dr in (-1, 0, 1)
            for dc in (-1, 0, 1)
        )

    def dump(self):
        return "\n".join(
            "".join("●" if v else "·" for v in line) for line in self.grid
        )


if __name__ == "__main__":
    print(VirtualPage().dump())
