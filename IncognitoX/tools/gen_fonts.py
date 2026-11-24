import re, math, os
from PIL import Image, ImageDraw, ImageFont

BASE = "C:/Users/egork/OneDrive/Desktop/OpenCODE/IncognitoX"

def parse(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    bm = re.search(r"const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S)
    bitmap = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm.group(2))]
    i = src.find("const GFXglyph")
    j = src.find("const GFXfont")
    gl = re.search(r"const GFXglyph (\w+)\[\] PROGMEM = \{(.*?)\};", src[i:j], re.S)
    ents = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+)\s*\}", gl.group(2))
    ent = [(int(a), int(b), int(c), int(d), int(e), int(f)) for a, b, c, d, e, f in ents]
    yf = re.search(r"const GFXfont (\w+) PROGMEM = \{.*?,.*?,\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*(\d+)\s*\};", src, re.S)
    first, last, yadv = int(yf.group(2), 16), int(yf.group(3), 16), int(yf.group(4))
    return bitmap, ent, first, last, yadv

STYLES = [
    ("Roboto9pt7b",       "FreeSans9pt7b",      18, 22, False),
    ("RobotoBold9pt7b",   "FreeSansBold9pt7b",  18, 22, True),
    ("Roboto12pt7b",      "FreeSans12pt7b",     23, 29, False),
    ("RobotoBold12pt7b",  "FreeSansBold12pt7b", 23, 29, True),
    ("Roboto18pt7b",      "FreeSans18pt7b",     34, 42, False),
    ("RobotoBold18pt7b",  "FreeSansBold18pt7b", 34, 42, True),
]

TTF = BASE + "/tools/fonts/Roboto.ttf"

_fc_cache = {}
def render_glyph(S, ch, bold):
    key = (S, bold)
    if key not in _fc_cache:
        font = ImageFont.truetype(TTF, S)
        if bold:
            font.set_variation_by_axes([700, 100])
        else:
            font.set_variation_by_axes([400, 100])
        asc, desc = font.getmetrics()
        probe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        W = int(math.ceil(font.getlength(probe))) + 6
        H = int(math.ceil(asc + desc)) + 6
        _fc_cache[key] = (font, asc, W, H)
    font, B, W, H = _fc_cache[key]
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 0), ch, font=font, fill=255)
    pix = img.load()
    l, t, r, b = d.textbbox((0, 0), ch, font=font)
    w = max(1, int(round(r - l)) + 1)
    h = max(1, int(b - t) + 1)

    bits = []
    val = 0
    cnt = 0
    for yy in range(h):
        for xx in range(w):
            if pix[l + xx, t + yy] > 127:
                val |= 0x80 >> cnt
            cnt += 1
            if cnt == 8:
                bits.append(val)
                val = 0
                cnt = 0
    if cnt:
        bits.append(val)
    data = bytes(bits)
    yoff = t - B
    xa = int(round(float(font.getlength(ch))))
    return data, w, h, xa, 0, yoff

def build(name, old, S, bold, first, last, yadv):
    bitmap, ent, ofirst, olast, oyadv = old
    glyphs = []
    bitmap_out = []
    pos = 0
    def emit(cp, off, w, h, xa, xo, yo, data, label):
        nonlocal pos
        glyphs.append((cp, off, w, h, xa, xo, yo, data, label))
        if data is not None:
            bitmap_out.extend(data)
            pos = off + len(data)
        else:
            pos = off
    for cp in range(0x20, 0x500):
        if 0x20 <= cp <= 0x7E:
            e = ent[cp - 0x20] if cp - 0x20 < len(ent) else (0, 0, 0, 5, 0, 1)
            off, w, h, xa, xo, yo = e
            nbytes = (w * h + 7) // 8
            data = bytes(bitmap[off:off + nbytes]) if nbytes else b""
            emit(cp, off, w, h, xa, xo, yo, data, chr(cp))
        elif 0x410 <= cp <= 0x4FF:
            data, w, h, xa, xo, yo = render_glyph(S, chr(cp), bold)
            emit(cp, pos, w, h, xa, xo, yo, data, chr(cp))
        else:
            emit(cp, pos, 0, 0, 0, 0, 0, None, None)
    return glyphs, bitmap_out

def header_text(name, glyphs, bitmap, first, last, yadv):
    L = []
    L.append("// " + name + " - bitmap font (Roboto OFL: latin + cyrillic) for IncognitoX")
    L.append("const uint8_t " + name + "Bitmaps[] PROGMEM = {")
    for i in range(0, len(bitmap), 12):
        chunk = bitmap[i:i + 12]
        L.append("  " + ", ".join("0x%02X" % b for b in chunk) + ",")
    L.append("};")
    L.append("")
    L.append("const GFXglyph " + name + "Glyphs[] PROGMEM = {")
    for g in glyphs:
        cp, off, w, h, xa, xo, yo, data, label = g
        if data is None:
            L.append("  {     0,   0,   0,   0,    0,    0 },   // 0x%03X" % cp)
        else:
            L.append("  { %5d,  %3d,  %3d,  %3d,  %3d,  %4d },   // 0x%03X '%s'" % (off, w, h, xa, xo, yo, cp, label))
    L.append("};")
    L.append("")
    L.append("const GFXfont " + name + " PROGMEM = {")
    L.append("  (uint8_t  *)" + name + "Bitmaps,")
    L.append("  (GFXglyph *)" + name + "Glyphs,")
    L.append("  0x%02X, 0x%03X, %d };" % (first, last, yadv))
    L.append("")
    return "\n".join(L)

os.makedirs(BASE + "/include/assets", exist_ok=True)
for (name, oldname, S, yadv, bold) in STYLES:
    old = parse(BASE + "/include/assets/" + oldname + ".h")
    glyphs, bitmap = build(name, old, S, bold, 0x20, 0x4FF, yadv)
    txt = header_text(name, glyphs, bitmap, 0x20, 0x4FF, yadv)
    with open(BASE + "/include/assets/" + name + ".h", "w", encoding="utf-8") as f:
        f.write(txt)
    print("wrote", name, "glyphs:", len(glyphs), "bitmap bytes:", len(bitmap))

print("OK")
