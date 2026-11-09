#!/usr/bin/env python3
"""Generate 144x80 PSP ICON0.PNG files (pure Python, no image libs)."""
import struct, zlib, os

W, H = 144, 80

def new_canvas(bg=(0,0,0)):
    return bytearray([c for c in bg for _ in range(W*H)])

def idx(x, y):
    return (y*W + x)*3

def put(px, x, y, color):
    if 0 <= x < W and 0 <= y < H:
        i = idx(x, y)
        px[i:i+3] = color

def fill_rect(px, x, y, w, h, color):
    for yy in range(y, y+h):
        for xx in range(x, x+w):
            put(px, xx, yy, color)

def hline(px, x0, x1, y, color, t=1):
    for yy in range(y, y+t):
        for xx in range(min(x0,x1), max(x0,x1)+1):
            put(px, xx, yy, color)

def vline(px, x, y0, y1, color, t=1):
    for xx in range(x, x+t):
        for yy in range(min(y0,y1), max(y0,y1)+1):
            put(px, xx, yy, color)

def line(px, x0, y0, x1, y1, color, t=1):
    dx = abs(x1-x0); dy = abs(y1-y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy
    while True:
        for tx in range(t):
            for ty in range(t):
                put(px, x0+tx, y0+ty, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2*err
        if e2 > -dy:
            err -= dy; x0 += sx
        if e2 < dx:
            err += dx; y0 += sy

def circle(px, cx, cy, r, color, fill=False):
    if fill:
        for y in range(cy-r, cy+r+1):
            for x in range(cx-r, cx+r+1):
                if (x-cx)**2 + (y-cy)**2 <= r*r:
                    put(px, x, y, color)
    else:
        x = r; y = 0; err = 1 - r
        while x >= y:
            for (a,b) in [(x,y),(y,x),(-x,y),(-y,x),(-x,-y),(-y,-x),(x,-y),(y,-x)]:
                put(px, cx+a, cy+b, color)
            y += 1
            if err < 0:
                err += 2*y + 1
            else:
                x -= 1
                err += 2*(y - x) + 1

def write_png(path, px):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw.extend(px[y*W*3:(y+1)*W*3])
    comp = zlib.compress(bytes(raw), 9)
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ+data) & 0xffffffff))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", comp))
        f.write(chunk(b"IEND", b""))

def make_snake():
    px = new_canvas((8, 12, 8))
    fill_rect(px, 0, 0, W, 4, (20, 60, 20))
    fill_rect(px, 0, H-4, W, 4, (20, 60, 20))
    fill_rect(px, 0, 0, 4, H, (20, 60, 20))
    fill_rect(px, W-4, 0, 4, H, (20, 60, 20))
    segs = [(20,40),(28,40),(36,40),(44,40),(52,40),(52,32),(52,24),(60,24),(68,24),(76,24)]
    for i,(x,y) in enumerate(segs):
        col = (60, 200, 80) if i < len(segs)-1 else (120, 240, 120)
        fill_rect(px, x, y, 6, 6, col)
        hline(px, x, x+5, y, (20,80,30))
        hline(px, x, x+5, y+5, (20,80,30))
        vline(px, x, y, y+5, (20,80,30))
        vline(px, x+5, y, y+5, (20,80,30))
    hx, hy = segs[-1]
    put(px, hx+1, hy+1, (0,0,0)); put(px, hx+4, hy+1, (0,0,0))
    fill_rect(px, 100, 36, 6, 6, (220, 60, 60))
    circle(px, 103, 39, 4, (255, 120, 120))
    return px

def make_cube():
    px = new_canvas((10, 10, 30))
    pts_top = [(72,20),(102,35),(72,50),(42,35)]
    pts_left = [(42,35),(72,50),(72,72),(42,57)]
    pts_right = [(72,50),(102,35),(102,57),(72,72)]
    def fill_poly(pts, color):
        ys = [p[1] for p in pts]
        ymin, ymax = min(ys), max(ys)
        for y in range(ymin, ymax+1):
            xs = []
            for i in range(len(pts)):
                a = pts[i]; b = pts[(i+1)%len(pts)]
                if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
                    x = a[0] + (y - a[1]) * (b[0]-a[0]) / (b[1]-a[1])
                    xs.append(x)
            xs.sort()
            for k in range(0, len(xs)-1, 2):
                for x in range(int(xs[k]), int(xs[k+1])+1):
                    put(px, x, y, color)
    fill_poly(pts_top, (80, 140, 220))
    fill_poly(pts_left, (40, 80, 160))
    fill_poly(pts_right, (60, 110, 190))
    for pts in (pts_top, pts_left, pts_right):
        for i in range(len(pts)):
            line(px, pts[i][0], pts[i][1], pts[(i+1)%len(pts)][0], pts[(i+1)%len(pts)][1], (20,20,40), 2)
    return px

def make_racing():
    px = new_canvas((40, 80, 160))
    fill_rect(px, 0, 0, W, 30, (80, 160, 230))
    fill_rect(px, 0, 30, W, H-30, (60, 60, 60))
    for y in range(30, H):
        t = (y-30)/(H-30)
        half = int(10 + t*50)
        cx = W//2
        fill_rect(px, cx-half, y, half*2, 1, (40,40,40))
    for y in range(34, H, 8):
        t = (y-30)/(H-30)
        half = int(10 + t*50)
        cx = W//2
        fill_rect(px, cx-2, y, 4, 4, (230,230,230))
    fill_rect(px, 62, 52, 20, 10, (220, 60, 40))
    fill_rect(px, 66, 48, 12, 6, (180, 40, 30))
    circle(px, 66, 62, 3, (20,20,20), fill=True)
    circle(px, 78, 62, 3, (20,20,20), fill=True)
    return px

def make_pong():
    px = new_canvas((0,0,0))
    for y in range(0, H, 6):
        fill_rect(px, W//2-1, y, 2, 4, (120,120,120))
    fill_rect(px, 12, 24, 6, 32, (60, 220, 120))
    fill_rect(px, W-18, 20, 6, 32, (80, 140, 255))
    circle(px, W//2+10, 40, 4, (240,240,240), fill=True)
    return px

def make_wifi():
    px = new_canvas((8, 18, 32))
    fill_rect(px, 0, 0, W, 3, (20, 60, 90))
    fill_rect(px, 0, H-3, W, 3, (20, 60, 90))
    fill_rect(px, 0, 0, 3, H, (20, 60, 90))
    fill_rect(px, W-3, 0, 3, H, (20, 60, 90))
    cx, cy = 72, 60
    for r in (12, 24, 36):
        for x in range(-r, r + 1):
            y = int((r * r - x * x) ** 0.5)
            for t in range(3):
                put(px, cx + x, cy - y + t, (80, 220, 255))
    circle(px, cx, cy, 5, (80, 220, 255), fill=True)
    circle(px, 118, 18, 5, (80, 240, 120), fill=True)
    return px

def main():
    base = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets")
    os.makedirs(base, exist_ok=True)
    icons = {
        "icon_snake.png": make_snake,
        "icon_cube3d.png": make_cube,
        "icon_racing.png": make_racing,
        "icon_pong.png": make_pong,
        "icon_wifi.png": make_wifi,
    }
    for name, fn in icons.items():
        px = fn()
        write_png(os.path.join(base, name), px)
        print("wrote", name)

if __name__ == "__main__":
    main()
