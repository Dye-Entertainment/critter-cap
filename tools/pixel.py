"""Tiny pixel-art canvas and PNG writer (standard library only)."""
import math
import struct
import zlib


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[None] * w for _ in range(h)]

    # -- basic access ---------------------------------------------------------------------------
    def put(self, x, y, c):
        x, y = int(math.floor(x)), int(math.floor(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.p[y][x]
        return None

    def copy(self):
        c = Canvas(self.w, self.h)
        c.p = [row[:] for row in self.p]
        return c

    # -- shapes ---------------------------------------------------------------------------------
    def rect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.put(xx, yy, c)

    def hline(self, x0, x1, y, c):
        for x in range(int(x0), int(x1) + 1):
            self.put(x, y, c)

    def vline(self, x, y0, y1, c):
        for y in range(int(y0), int(y1) + 1):
            self.put(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(steps + 1):
            t = i / max(steps, 1)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def ell(self, cx, cy, rx, ry, c, shade=None, light=None, clip=None, shade_dir=(0.55, 0.8),
            shade_at=0.45, light_at=(-0.4, -0.45, 0.32)):
        """Filled ellipse with optional shading toward shade_dir and a highlight spot."""
        for y in range(self.h):
            for x in range(self.w):
                dx = (x + 0.5 - cx) / rx
                dy = (y + 0.5 - cy) / ry
                if dx * dx + dy * dy > 1.0:
                    continue
                if clip and not clip(x, y):
                    continue
                col = c
                if shade and dx * shade_dir[0] + dy * shade_dir[1] > shade_at:
                    col = shade
                if light and (dx - light_at[0]) ** 2 + (dy - light_at[1]) ** 2 < light_at[2] ** 2:
                    col = light
                self.put(x, y, col)

    def ring(self, cx, cy, r0, r1, c):
        for y in range(self.h):
            for x in range(self.w):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                if r0 <= d <= r1:
                    self.put(x, y, c)

    def ascii(self, x0, y0, rows, palette):
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in palette and palette[ch] is not None:
                    self.put(x0 + x, y0 + y, palette[ch])

    def blit(self, other, x0, y0):
        for y in range(other.h):
            for x in range(other.w):
                c = other.p[y][x]
                if c is not None:
                    self.put(x0 + x, y0 + y, c)

    # -- post processing ------------------------------------------------------------------------
    def outline(self, c, diagonal=False):
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.p[y][x] is not None:
                    continue
                n = [(1, 0), (-1, 0), (0, 1), (0, -1)]
                if diagonal:
                    n += [(1, 1), (-1, 1), (1, -1), (-1, -1)]
                for dx, dy in n:
                    v = self.get(x + dx, y + dy)
                    if v is not None and v != c:
                        add.append((x, y))
                        break
        for x, y in add:
            self.p[y][x] = c
        return self

    def flip_x(self):
        c = Canvas(self.w, self.h)
        c.p = [row[::-1] for row in self.p]
        return c

    def flip_y(self):
        c = Canvas(self.w, self.h)
        c.p = [row[:] for row in self.p[::-1]]
        return c

    def transpose(self):
        c = Canvas(self.h, self.w)
        for y in range(self.h):
            for x in range(self.w):
                c.p[x][y] = self.p[y][x]
        return c

    def recolor(self, mapping):
        c = self.copy()
        for y in range(c.h):
            for x in range(c.w):
                v = c.p[y][x]
                if v in mapping:
                    c.p[y][x] = mapping[v]
        return c

    def shift(self, dx, dy, region=None):
        """Moves pixels by (dx, dy). region=(x0,y0,x1,y1) limits which pixels move."""
        c = Canvas(self.w, self.h)
        for y in range(self.h):
            for x in range(self.w):
                v = self.p[y][x]
                if v is None:
                    continue
                if region and not (region[0] <= x < region[2] and region[1] <= y < region[3]):
                    c.put(x, y, v)
        for y in range(self.h):
            for x in range(self.w):
                v = self.p[y][x]
                if v is None:
                    continue
                if not region or (region[0] <= x < region[2] and region[1] <= y < region[3]):
                    c.put(x + dx, y + dy, v)
        return c


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def write_png(path, w, h, pixels):
    """pixels: rows of (r,g,b) or (r,g,b,a) or None."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            c = pixels[y][x]
            if c is None:
                raw += b"\x00\x00\x00\x00"
            elif len(c) == 3:
                raw += bytes((c[0], c[1], c[2], 255))
            else:
                raw += bytes(c)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
