#!/usr/bin/env python3

import argparse
import math
import os
import sys
import time
import urllib.request

try:
    from PIL import Image
except ImportError:
    print("Нужен Pillow:  pip install pillow")
    sys.exit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".tilecache")
OUT_DIR = os.path.join(ROOT, "sd", "maps")

TILE_URL = "https://a.tile.openstreetmap.de/{z}/{x}/{y}.png"
UA = "IncognitoX-offline-map/1.0 (ESP32 hobby project; 24 tiles once)"

def deg2num(lat, lon, z):
    n = 2.0 ** z
    x = (lon + 180.0) / 360.0 * n
    lat_r = math.radians(lat)
    y = (1.0 - math.log(math.tan(lat_r) + 1.0 / math.cos(lat_r)) / math.pi) / 2.0 * n
    return x, y

def num2deg(x, y, z):
    n = 2.0 ** z
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n))))
    return lat, lon

def fetch_tile(z, x, y):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, f"{z}_{x}_{y}.png")
    if os.path.exists(p) and os.path.getsize(p) > 100:
        return Image.open(p).convert("RGB")
    url = TILE_URL.format(z=z, x=x, y=y)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
            if data[:8] != b"\x89PNG\r\n\x1a\n":
                raise ValueError("not a PNG")
            with open(p, "wb") as f:
                f.write(data)
            time.sleep(0.15)
            return Image.open(p).convert("RGB")
        except Exception as e:
            if attempt == 3:
                raise
            time.sleep(1.5 * (attempt + 1))
    raise RuntimeError("unreachable")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="minsk")
    ap.add_argument("--zoom", type=int, default=12)

    ap.add_argument("--bbox", default="53.815,27.355,53.985,27.775",
                    help="lat_min,lon_min,lat_max,lon_max")
    ap.add_argument("--max-tiles", type=int, default=36)
    args = ap.parse_args()

    lat_min, lon_min, lat_max, lon_max = [float(v) for v in args.bbox.split(",")]
    if not (lat_min < lat_max and lon_min < lon_max):
        print("bbox должен быть: lat_min,lon_min,lat_max,lon_max")
        return 2

    z = args.zoom
    x0f, y0f = deg2num(lat_max, lon_min, z)
    x1f, y1f = deg2num(lat_min, lon_max, z)
    x0, y0 = int(math.floor(x0f)), int(math.floor(y0f))
    x1, y1 = int(math.floor(x1f)), int(math.floor(y1f))
    nx, ny = x1 - x0 + 1, y1 - y0 + 1
    print(f"zoom {z}: tiles x={x0}..{x1} ({nx}), y={y0}..{y1} ({ny}) = {nx * ny}")
    if nx * ny > args.max_tiles:
        print(f"Слишком много тайлов ({nx * ny} > {args.max_tiles}). "
              f"Уменьшите bbox или zoom.")
        return 2

    img = Image.new("RGB", (nx * 256, ny * 256))
    for ty in range(ny):
        for tx in range(nx):
            t = fetch_tile(z, x0 + tx, y0 + ty)
            img.paste(t, (tx * 256, ty * 256))
        print(f"  строка {ty + 1}/{ny}")

    os.makedirs(OUT_DIR, exist_ok=True)
    bmp_path = os.path.join(OUT_DIR, args.name + ".bmp")
    geo_path = os.path.join(OUT_DIR, args.name + ".geo")
    img.save(bmp_path, "BMP")

    lat_top, lon_left = num2deg(x0, y0, z)
    lat_bottom, lon_right = num2deg(x0 + nx, y0 + ny, z)
    with open(geo_path, "w", encoding="utf-8") as f:
        f.write("# IncognitoX geo: lat_top lon_left lat_bottom lon_right\n")
        f.write(f"lat_top={lat_top:.7f}\n")
        f.write(f"lon_left={lon_left:.7f}\n")
        f.write(f"lat_bottom={lat_bottom:.7f}\n")
        f.write(f"lon_right={lon_right:.7f}\n")
        f.write("attribution=© OpenStreetMap contributors\n")

    print(f"OK: {bmp_path} ({os.path.getsize(bmp_path) / 1048576:.2f} MB)")
    print(f"    {geo_path}")
    print(f"    {img.width}x{img.height}px, "
          f"lat {lat_bottom:.5f}..{lat_top:.5f}, lon {lon_left:.5f}..{lon_right:.5f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
