#!/usr/bin/env python3
"""Compose Whatly README 'feature cards': a teal-gradient background + a
rounded, shadowed screenshot + the logo + a bold feature title. This is the
promo style used in the README; run it after capturing fresh screenshots.

Usage:
    python3 tools/make-cards.py [--shots DIR] [--out DIR]

--shots  directory holding the raw PNG screenshots (default tools/shots)
--out    where to write the cards (default docs/img)

Expected screenshots in --shots (any missing one is skipped):
    settings.png   the Settings dialog
    about.png      the About dialog
    chat.png       the main window / chat list (ideally with the privacy blur on)

Requires: Pillow  (pip install pillow) and a DejaVu Sans font.
"""
import os, sys, argparse
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(REPO, "dist/linux/hicolor/256x256/apps/net.shakaran.whatly.png")

# Brand palette (teal). Change these to re-skin every card.
C1 = (13, 148, 136)     # #0d9488 gradient top
C2 = (6, 78, 70)        # #064e46 gradient bottom
ACCENT = (45, 212, 191) # #2dd4bf underline
RIM = (243, 243, 243)

def _font(bold, size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else ""),
              "/usr/share/fonts/truetype/liberation/LiberationSans%s.ttf" % ("-Bold" if bold else "-Regular")):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

def gradient(W, H):
    g = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(g)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=lerp(C1, C2, y / (H - 1)))
    streak = Image.new("L", (W, H), 0)
    ImageDraw.Draw(streak).polygon([(W*0.55,0),(W,0),(W,H),(W*0.30,H)], fill=40)
    streak = streak.filter(ImageFilter.GaussianBlur(120)).point(lambda v: int(v*0.35))
    return Image.composite(Image.new("RGB",(W,H),(255,255,255)), g, streak)

def trim_transparent(img):
    """Drop a window screenshot's semi-transparent drop-shadow border, so the
    rounded frame does not turn it into an opaque black edge. No-op for opaque
    images (e.g. CDP web captures)."""
    img = img.convert("RGBA")
    a = img.split()[3]
    # Keep only near-opaque pixels: a window screenshot's drop shadow fades from
    # translucent to transparent, so a low threshold leaves its darkest band as a
    # black-looking edge. 200 cuts the whole shadow and keeps just the window.
    bbox = a.point(lambda v: 255 if v > 200 else 0).getbbox()
    return img.crop(bbox) if bbox else img

def rounded_shadow(img, radius=22, pad=60, blur=34, alpha=150):
    w, h = img.size
    card = img.convert("RGBA")
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w-1, h-1], radius=radius, fill=255)
    card.putalpha(mask)
    canvas = Image.new("RGBA", (w+pad*2, h+pad*2), (0, 0, 0, 0))
    sh = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([pad, pad+10, pad+w, pad+h+10], radius=radius, fill=(0,0,0,alpha))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(blur)))
    canvas.alpha_composite(card, (pad, pad))
    return canvas

def wrap(draw, text, font, maxw):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if draw.textlength(t, font=font) <= maxw:
            cur = t
        else:
            lines.append(cur); cur = w
    if cur:
        lines.append(cur)
    return lines

def card(out, shot_path, title, subtitle, W=1280, H=720):
    bg = gradient(W, H).convert("RGBA")
    d = ImageDraw.Draw(bg)
    shot = trim_transparent(Image.open(shot_path))
    r = min(int(W*0.52)/shot.width, (H-150)/shot.height)
    shot = shot.resize((int(shot.width*r), int(shot.height*r)), Image.LANCZOS)
    framed = rounded_shadow(shot)
    bg.alpha_composite(framed, (int(W*0.05)-40, (H-framed.height)//2))
    logo = Image.open(LOGO).convert("RGBA").resize((104, 104), Image.LANCZOS)
    tx = int(W*0.60)
    bg.alpha_composite(logo, (tx, 60))
    d.text((tx+120, 78), "Whatly", font=_font(True, 52), fill=(255, 255, 255))
    tfont, sfont = _font(True, 62), _font(False, 30)
    y = 250
    for ln in wrap(d, title, tfont, W-tx-70):
        d.text((tx, y), ln, font=tfont, fill=(255, 255, 255)); y += 74
    d.rounded_rectangle([tx, y+8, tx+120, y+16], radius=4, fill=ACCENT); y += 42
    for ln in wrap(d, subtitle, sfont, W-tx-70):
        d.text((tx, y), ln, font=sfont, fill=(200, 240, 235)); y += 40
    bg.convert("RGB").save(out); print("  wrote", os.path.relpath(out, REPO))

def banner(out, W=1280, H=380):
    bg = gradient(W, H).convert("RGBA")
    d = ImageDraw.Draw(bg)
    lsz, gap, tf = 150, 28, _font(True, 96)
    tw = d.textlength("Whatly", font=tf)
    x0 = (W - (lsz + gap + tw)) // 2
    cy = int(H*0.42)
    bg.alpha_composite(Image.open(LOGO).convert("RGBA").resize((lsz, lsz), Image.LANCZOS), (int(x0), int(cy-lsz/2)))
    tb = d.textbbox((0, 0), "Whatly", font=tf)
    d.text((x0+lsz+gap, cy-(tb[3]-tb[1])/2-tb[1]), "Whatly", font=tf, fill=(255, 255, 255))
    st, sf = "A feature-rich desktop client for WhatsApp Web", _font(False, 34)
    d.text(((W-d.textlength(st, font=sf))//2, int(H*0.72)), st, font=sf, fill=(205, 242, 237))
    bg.convert("RGB").save(out); print("  wrote", os.path.relpath(out, REPO))

def section_banner(out, title, subtitle="", W=1280, H=150):
    """A slim teal strip to head a text-heavy section: the small logo, a bold
    section title and an optional subtitle, on the brand gradient."""
    bg = gradient(W, H).convert("RGBA")
    d = ImageDraw.Draw(bg)
    lsz = 84
    bg.alpha_composite(Image.open(LOGO).convert("RGBA").resize((lsz, lsz), Image.LANCZOS),
                       (48, (H-lsz)//2))
    tx = 48 + lsz + 26
    tf = _font(True, 52)
    tb = d.textbbox((0, 0), title, font=tf)
    ty = (H - (tb[3]-tb[1]))//2 - tb[1] if not subtitle else int(H*0.28)
    d.text((tx, ty), title, font=tf, fill=(255, 255, 255))
    if subtitle:
        d.text((tx, int(H*0.60)), subtitle, font=_font(False, 26), fill=(200, 240, 235))
    d.rounded_rectangle([tx, H-20, tx+140, H-14], radius=3, fill=ACCENT)
    bg.convert("RGB").save(out); print("  wrote", os.path.relpath(out, REPO))

def installers_panel(out, ss=2):
    """A dark panel listing every packaging format Whatly ships in, each as a
    coloured badge + name + the one-line way to install it."""
    W, H = 660*ss, 720*ss
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30*ss, 26*ss), "Available everywhere", font=_font(True, 30*ss), fill=(235, 245, 243))
    d.text((30*ss, 72*ss), "one codebase, packaged for every desktop",
           font=_font(False, 19*ss), fill=(150, 200, 190))
    items = [
        ("Snap",            (233, 84, 32),  "snap",     "snap install whatly"),
        ("Flatpak",         (74, 144, 217), "flatpak",  "flatpak install whatly.flatpak"),
        ("AppImage",        (46, 125, 154), "appimage", "./Whatly-x86_64.AppImage"),
        ("Debian / Ubuntu", (215, 10, 83),  "debian",   "dpkg-buildpackage -b"),
        ("Fedora / COPR",   (81, 162, 218), "fedora",   "rpmbuild -ba whatly.spec"),
        ("Arch (AUR)",      (23, 147, 209), "arch",     "yay -S whatsie-git"),
        ("Windows",         (0, 120, 214),  "windows",  "whatly.exe (from CI)"),
    ]
    logodir = os.path.join(REPO, "tools/logos")
    top, pad = 122*ss, 30*ss
    rowh = (H - top - pad) // len(items)
    for i, (name, col, logo, hint) in enumerate(items):
        y = top + i*rowh
        d.rounded_rectangle([pad, y, W-pad, y+rowh-14*ss], radius=16*ss, fill=(15, 20, 22))
        bsz = rowh - 14*ss - 26*ss; bx, by = pad+16*ss, y+13*ss
        d.rounded_rectangle([bx, by, bx+bsz, by+bsz], radius=13*ss, fill=col)
        lg = Image.open(os.path.join(logodir, logo + ".png")).convert("RGBA")
        gs = int(bsz*0.62); lg = lg.resize((gs, gs), Image.LANCZOS)
        im.paste(lg, (bx+(bsz-gs)//2, by+(bsz-gs)//2), lg)
        tx = bx + bsz + 22*ss
        d.text((tx, y+14*ss), name, font=_font(True, 24*ss), fill=(230, 240, 238))
        d.text((tx, y+14*ss+34*ss), hint, font=_font(False, 17*ss), fill=(140, 178, 172))
    im.resize((W//ss, H//ss), Image.LANCZOS).save(out)

import colorsys
# Chat themes (name, hue) — mirrors src/chattheme.cpp. None = WhatsApp green.
THEMES = [("WhatsApp", None), ("Barbie pink", 335), ("Dusty rose", 345), ("Lavender", 270),
          ("Violet", 292), ("Sky blue", 205), ("Deep ocean", 222), ("Teal", 186), ("Mint", 152),
          ("Coral", 8), ("Peach", 24), ("Gold", 44), ("Crimson", 352), ("Graphite", 220)]

def _theme_color(h):
    if h is None:
        return (37, 211, 102)             # WhatsApp green
    l, s = (0.32, 0.10) if h == 220 else (0.55, 0.62)   # graphite is a grey
    return tuple(int(x*255) for x in colorsys.hls_to_rgb(h/360, l, s))

def themes_panel(out, W=660, H=720, ss=2):
    W, H = W*ss, H*ss
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30*ss, 24*ss), "Chat themes", font=_font(True, 30*ss), fill=(235, 245, 243))
    pad, top, cols, rows = 30*ss, 90*ss, 2, 7
    cw = (W-pad*2-20*ss)//cols; ch = (H-top-pad)//rows
    for i, (name, h) in enumerate(THEMES):
        x = pad + (i % cols)*(cw+20*ss); y = top + (i//cols)*ch
        d.rounded_rectangle([x, y, x+cw, y+ch-12*ss], radius=14*ss, fill=(15, 20, 22))
        sw = 54*ss; sy = y+(ch-12*ss-sw)//2
        d.rounded_rectangle([x+14*ss, sy, x+14*ss+sw, sy+sw], radius=12*ss, fill=_theme_color(h))
        d.text((x+14*ss+sw+16*ss, y+(ch-12*ss)//2-16*ss), name, font=_font(True, 21*ss), fill=(225, 235, 233))
    im.resize((W//ss, H//ss), Image.LANCZOS).save(out)

def tray_panel(out, ss=2):
    """A dark 'panel' strip showing the real tray icons: colour idle and colour
    with unread badges."""
    W, H = 640*ss, 340*ss
    im = Image.new("RGB", (W, H), (30, 33, 36)); d = ImageDraw.Draw(im)
    ndir = os.path.join(REPO, "src/icons/app/notification")
    def load(name, size):
        return Image.open(os.path.join(ndir, name)).convert("RGBA").resize((size, size), Image.LANCZOS)
    d.text((30*ss, 26*ss), "System tray", font=_font(True, 28*ss), fill=(235, 245, 243))
    d.text((30*ss, 74*ss), "colour badge · monochrome option · connection status",
           font=_font(False, 19*ss), fill=(150, 200, 190))
    strip_top = 150*ss; strip_h = 130*ss
    d.rounded_rectangle([24*ss, strip_top, W-24*ss, strip_top+strip_h], radius=16*ss, fill=(16, 18, 20))
    isz = 60*ss; y = strip_top + 22*ss
    items = [("whatly-notify.png", "idle"), ("whatly-notify-3.png", "3 unread"),
             ("whatly-notify-10.png", "10+")]
    gap = W//(len(items)+1)
    lf = _font(False, 17*ss)
    for i, (name, label) in enumerate(items):
        x = gap*(i+1) - isz//2
        im.paste(load(name, isz), (x, y), load(name, isz))
        d.text((x+isz//2-d.textlength(label, font=lf)//2, y+isz+10*ss), label, font=lf, fill=(180, 190, 190))
    im.resize((W//ss, H//ss), Image.LANCZOS).save(out)

SHORTCUTS = [("Ctrl", "N", "New chat"), ("Ctrl", "P", "Settings"),
             ("Ctrl", "T", "Toggle light/dark"), ("Ctrl", "L", "Lock the app"),
             ("Ctrl", "W", "Minimize to tray"), ("Ctrl", "Q", "Quit"),
             ("", "F5", "Reload"), ("", "F11", "Fullscreen")]

def _keycap(d, x, y, text, f):
    w = max(38, d.textlength(text, font=f) + 22)
    d.rounded_rectangle([x, y, x+w, y+40], radius=8, fill=(50, 58, 60), outline=(90, 100, 100), width=2)
    tb = d.textbbox((0, 0), text, font=f)
    d.text((x+(w-(tb[2]-tb[0]))/2-tb[0], y+(40-(tb[3]-tb[1]))/2-tb[1]), text, font=f, fill=(230, 240, 238))
    return w

def shortcuts_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Keyboard shortcuts", font=_font(True, 30), fill=(235, 245, 243))
    kf, af = _font(True, 20), _font(False, 22)
    y = 100; rh = (H-y-30)//len(SHORTCUTS)
    for mod, key, action in SHORTCUTS:
        x = 34
        if mod:
            x += _keycap(d, x, y+8, mod, kf) + 8
            d.text((x, y+14), "+", font=af, fill=(150, 170, 168)); x += 22
        _keycap(d, x, y+8, key, kf)
        d.text((300, y+16), action, font=af, fill=(220, 232, 230))
        y += rh
    im.save(out)

def watchdog_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    cx = W//2
    # a simple reload glyph
    import math
    for a in range(-40, 250):
        r = math.radians(a)
        d.ellipse([cx+180*math.cos(r)-9, 250+180*math.sin(r)-9, cx+180*math.cos(r)+9, 250+180*math.sin(r)+9], fill=(45, 212, 191))
    ah = math.radians(250)
    tip = (cx+180*math.cos(ah), 250+180*math.sin(ah))
    d.polygon([(tip[0]-4, tip[1]-34), (tip[0]+34, tip[1]-4), (tip[0]-30, tip[1]+24)], fill=(45, 212, 191))
    d.text((cx-90, 224), "auto", font=_font(True, 44), fill=(235, 245, 243))
    for i, ln in enumerate(["Connection watchdog", "Reloads WhatsApp Web when its",
                            "WebSocket dies or freezes —", "capped at 3 tries per hang."]):
        f = _font(True, 30) if i == 0 else _font(False, 24)
        d.text((cx-d.textlength(ln, font=f)/2, 470+i*44), ln, font=f, fill=(220, 232, 230) if i else (235, 245, 243))
    im.save(out)

def _check(d, x, y, s, col):
    d.line([(x, y+s*0.55), (x+s*0.38, y+s*0.9), (x+s, y+s*0.15)], fill=col, width=max(2, s//7), joint="curve")

def spellcheck_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Spell check", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "Check against several languages at once", font=_font(False, 20), fill=(150, 200, 190))
    langs = [("en_US", "English (US)", True), ("es_ES", "Spanish", True), ("fr_FR", "French", True),
             ("de_DE", "German", False), ("it_IT", "Italian", False), ("pt_BR", "Portuguese", False),
             ("nl_NL", "Dutch", False), ("pl_PL", "Polish", False)]
    y = 130; rh = (H-y-30)//len(langs)
    for code, name, on in langs:
        d.rounded_rectangle([30, y, W-30, y+rh-12], radius=12, fill=(15, 20, 22))
        box = 26
        bx, by = 52, y+(rh-12-box)//2
        d.rounded_rectangle([bx, by, bx+box, by+box], radius=6,
                            fill=(45, 212, 191) if on else (30, 38, 40),
                            outline=(90, 110, 108), width=2)
        if on:
            _check(d, bx+5, by+4, box-10, (12, 30, 28))
        d.text((bx+box+22, y+(rh-12)//2-20), code, font=_font(True, 22), fill=(225, 235, 233))
        d.text((bx+box+140, y+(rh-12)//2-18), name, font=_font(False, 20), fill=(150, 170, 168))
        y += rh
    im.save(out)

def notification_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Native notifications", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "Through your desktop, on the right screen", font=_font(False, 20), fill=(150, 200, 190))
    # a notification toast mock
    tx, ty, tw, th = 40, 160, W-80, 150
    d.rounded_rectangle([tx, ty, tx+tw, ty+th], radius=18, fill=(38, 44, 48))
    av = 88
    logo = Image.open(LOGO).convert("RGBA").resize((av, av), Image.LANCZOS)
    im.paste(logo, (tx+24, ty+(th-av)//2), logo)
    d.text((tx+24+av+22, ty+30), "Whatly", font=_font(True, 26), fill=(240, 248, 246))
    d.text((tx+24+av+22, ty+70), "New message from a chat", font=_font(False, 22), fill=(200, 214, 212))
    d.text((tx+24+av+22, ty+104), "Tap to open the conversation", font=_font(False, 19), fill=(150, 168, 166))
    # second, muted, behind
    d.rounded_rectangle([tx+30, ty+th+22, tx+tw-30, ty+th+22+70], radius=14, fill=(30, 36, 40))
    d.ellipse([tx+52, ty+th+38, tx+52+38, ty+th+38+38], fill=(45, 212, 191))
    d.text((tx+52+54, ty+th+40), "Delivered to the notification service", font=_font(False, 18), fill=(150, 168, 166))
    im.save(out)

def _window(d, x, y, w, h, title, accent=(45, 212, 191)):
    d.rounded_rectangle([x, y, x+w, y+h], radius=14, fill=(20, 26, 28), outline=(60, 72, 74), width=2)
    d.rounded_rectangle([x, y, x+w, y+38], radius=14, fill=(34, 42, 44))
    d.rectangle([x, y+24, x+w, y+38], fill=(34, 42, 44))
    for i, c in enumerate([(80, 90, 92)]*3):
        d.ellipse([x+16+i*20, y+14, x+26+i*20, y+24], fill=c)
    d.text((x+w/2-d.textlength(title, font=_font(False, 16))/2, y+11), title, font=_font(False, 16), fill=(200, 214, 212))

def accounts_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Multiple accounts", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "Tabs in one window, or separate --profile windows", font=_font(False, 20), fill=(150, 200, 190))
    _window(d, 40, 130, W-80, H-170, "Whatly")
    # tab strip
    tx, ty = 60, 186
    tabs = [("Personal", True, None), ("Work", False, "3"), ("+", False, None)]
    for name, active, badge in tabs:
        tw = 150 if name != "+" else 54
        d.rounded_rectangle([tx, ty, tx+tw, ty+46], radius=10,
                            fill=(45, 212, 191) if active else (30, 38, 40))
        d.text((tx+20, ty+11), name, font=_font(True, 20),
               fill=(12, 30, 28) if active else (210, 222, 220))
        if badge:
            d.ellipse([tx+tw-30, ty+8, tx+tw-8, ty+30], fill=(225, 29, 29))
            d.text((tx+tw-24, ty+9), badge, font=_font(True, 16), fill=(255, 255, 255))
        tx += tw + 12
    for i in range(5):
        yy = 270 + i*70
        d.ellipse([70, yy, 118, yy+48], fill=(40, 48, 50))
        d.rounded_rectangle([135, yy+8, 480, yy+20], radius=6, fill=(48, 58, 60))
        d.rounded_rectangle([135, yy+30, 380, yy+40], radius=5, fill=(36, 44, 46))
    im.save(out)

def profiles_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Separate profiles", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "whatly --profile=<name> — a window of its own", font=_font(False, 20), fill=(150, 200, 190))
    # two offset windows, each its own session
    def mini(x, y, title, badge_col):
        _window(d, x, y, 330, 300, title)
        d.rounded_rectangle([x+24, y+64, x+130, y+96], radius=8, fill=badge_col)
        d.text((x+40, y+70), "Chats", font=_font(True, 18), fill=(12, 30, 28))
        for i in range(3):
            yy = y+120+i*46
            d.ellipse([x+24, yy, x+58, yy+34], fill=(40, 48, 50))
            d.rounded_rectangle([x+70, yy+6, x+300, yy+16], radius=5, fill=(48, 58, 60))
            d.rounded_rectangle([x+70, yy+24, x+240, yy+32], radius=4, fill=(36, 44, 46))
    mini(40, 150, "Whatly — Personal", (45, 212, 191))
    mini(290, 280, "Whatly — Work", (250, 204, 21))
    im.save(out)

def sidebar_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Buttons in the sidebar", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "Toggle theme and blur without opening Settings", font=_font(False, 20), fill=(150, 200, 190))
    railx, railw = 250, 96
    d.rounded_rectangle([railx, 140, railx+railw, H-40], radius=20, fill=(16, 20, 22))
    cx = railx+railw//2
    for i in range(3):
        d.ellipse([cx-20, 175+i*70, cx+20, 215+i*70], outline=(90, 100, 100), width=3)
    # highlighted theme + blur buttons near the bottom
    for j, glyph in enumerate(["moon", "eye"]):
        yy = H-190 + j*66
        d.ellipse([cx-26, yy-26, cx+26, yy+26], fill=(30, 40, 42), outline=(45, 212, 191), width=3)
        if glyph == "moon":
            d.ellipse([cx-13, yy-13, cx+13, yy+13], fill=(235, 235, 210))
            d.ellipse([cx-4, yy-15, cx+18, yy+9], fill=(30, 40, 42))
        else:
            d.ellipse([cx-16, yy-9, cx+16, yy+9], outline=(150, 230, 220), width=3)
            d.ellipse([cx-6, yy-6, cx+6, yy+6], fill=(150, 230, 220))
        d.line([(cx+40, yy), (cx+120, yy)], fill=(45, 212, 191), width=3)
        d.text((cx+130, yy-14), "Theme" if glyph == "moon" else "Blur", font=_font(True, 22), fill=(220, 232, 230))
    im.save(out)

def wallpaper_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    d.text((30, 26), "Chat wallpaper", font=_font(True, 30), fill=(235, 245, 243))
    d.text((30, 70), "Your own image behind the messages", font=_font(False, 20), fill=(150, 200, 190))
    # wallpaper area with a soft gradient + dots
    wp = Image.new("RGB", (W-80, H-170), (0, 0, 0))
    wd = ImageDraw.Draw(wp)
    for y in range(wp.height):
        wd.line([(0, y), (wp.width, y)], fill=lerp((15, 60, 55), (40, 100, 92), y/wp.height))
    for gx in range(0, wp.width, 60):
        for gy in range(0, wp.height, 60):
            wd.ellipse([gx+20, gy+20, gx+30, gy+30], fill=(255, 255, 255))
    wp = Image.blend(wp, Image.new("RGB", wp.size, (20, 40, 38)), 0.35)
    im.paste(wp, (40, 130))
    d = ImageDraw.Draw(im)
    bub = [(70, 190, 330, 250, (44, 60, 62), "l"), (250, 280, 540, 350, (28, 120, 100), "r"),
           (70, 380, 300, 430, (44, 60, 62), "l"), (200, 460, 540, 540, (28, 120, 100), "r"),
           (70, 570, 360, 620, (44, 60, 62), "l")]
    for x1, y1, x2, y2, c, side in bub:
        d.rounded_rectangle([x1, y1, x2, y2], radius=16, fill=c)
    im.save(out)

def windows_panel(out):
    W, H = 660, 720
    im = Image.new("RGB", (W, H), (24, 32, 34)); d = ImageDraw.Draw(im)
    logo = Image.open(LOGO).convert("RGBA").resize((150, 150), Image.LANCZOS)
    im.paste(logo, (W//2-75, 170), logo)
    # windows-style four squares in teal
    bx, by, s, g = W//2-70, 380, 60, 14
    for r in range(2):
        for c in range(2):
            d.rectangle([bx+c*(s+g), by+r*(s+g), bx+c*(s+g)+s, by+r*(s+g)+s], fill=(45, 212, 191))
    for i, ln in enumerate(["Windows 10 & 11", "One codebase — native toasts,", "a proper GUI executable, CI-built."]):
        f = _font(True, 34) if i == 0 else _font(False, 24)
        d.text((W//2-d.textlength(ln, font=f)/2, 540+i*44), ln, font=f, fill=(235, 245, 243) if i == 0 else (200, 220, 216))
    im.save(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=os.path.join(REPO, "tools/shots"))
    ap.add_argument("--out", default=os.path.join(REPO, "docs/img"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    banner(os.path.join(a.out, "banner.png"))
    for slug, title, sub in [
        ("build-linux",   "Build from source", "Linux — Qt 6.10, CMake and Ninja"),
        ("build-windows", "Build on Windows",  "Qt 6.10 MSVC, from the same codebase"),
        ("troubleshooting", "Troubleshooting", "Common fixes for GPU, tray and login issues"),
        ("install",       "Install",           "snap · Flatpak · AppImage · deb · RPM · AUR · Windows"),
        ("cli",           "Command line",      "Drive a running instance from your shell or desktop actions"),
    ]:
        section_banner(os.path.join(a.out, "banner-" + slug + ".png"), title, sub)
    # Chat-themes card is drawn from the theme list, not captured.
    tp = os.path.join(a.shots, "themes.png"); os.makedirs(a.shots, exist_ok=True)
    themes_panel(tp)
    card(os.path.join(a.out, "card-themes.png"), tp, "Fourteen chat themes",
         "Recolour WhatsApp Web itself — pick a hue, keep your photos and avatars untouched.")
    trp = os.path.join(a.shots, "tray.png"); tray_panel(trp)
    card(os.path.join(a.out, "card-tray.png"), trp, "A smarter system tray",
         "An unread-count badge, an optional monochrome icon that matches your panel, and a connection-status dim.")
    scp = os.path.join(a.shots, "shortcuts.png"); shortcuts_panel(scp)
    card(os.path.join(a.out, "card-shortcuts.png"), scp, "Keyboard shortcuts",
         "Drive the app from the keyboard — new chat, settings, theme, lock, reload and more.")
    wdp = os.path.join(a.shots, "watchdog.png"); watchdog_panel(wdp)
    card(os.path.join(a.out, "card-watchdog.png"), wdp, "It reconnects itself",
         "WhatsApp Web's socket can hang on 'Connecting…'. A health probe detects it and reloads, capped at 3 tries.")
    sp = os.path.join(a.shots, "spellcheck.png"); spellcheck_panel(sp)
    card(os.path.join(a.out, "card-spellcheck.png"), sp, "Spell check that works",
         "Chromium dictionaries are shipped, and you can check against several languages at the same time.")
    npn = os.path.join(a.shots, "notifications.png"); notification_panel(npn)
    card(os.path.join(a.out, "card-notifications.png"), npn, "Native notifications",
         "Delivered through the desktop's own notification service, on the screen your window is on.")
    for fn, name, title, sub in [
        (accounts_panel,  "card-accounts.png",  "Multiple accounts",
         "Tabs in one window, or wholly separate windows with --profile — each its own session."),
        (profiles_panel,  "card-profiles.png",  "Separate profiles",
         "whatly --profile=work runs a second account in its own window, session and settings file."),
        (sidebar_panel,   "card-sidebar.png",   "Buttons in the sidebar",
         "Toggle the chat theme and the privacy blur from WhatsApp's own sidebar, without opening Settings."),
        (wallpaper_panel, "card-wallpaper.png", "Chat wallpaper",
         "Put your own image behind the messages, as WhatsApp does on the phone. Stored locally, only visible to you."),
        (windows_panel,   "card-windows.png",   "Runs on Windows too",
         "The same codebase builds a native Windows 10+ app — checked on every push by CI."),
        (installers_panel, "card-installers.png", "Install it anywhere",
         "Snap, Flatpak, AppImage, Debian, Fedora/COPR, Arch and Windows — Flatpak and AppImage are built on every release."),
    ]:
        p = os.path.join(a.shots, name.replace("card-", "").replace(".png", "_panel.png")); fn(p)
        card(os.path.join(a.out, name), p, title, sub)
    jobs = [
        ("chat.png",     "card-chat.png",     "Themes and a privacy blur",
         "Recolour WhatsApp Web with 14 chat themes, and blur the chats until you hover so nobody reads over your shoulder."),
        ("lightdark.png", "card-lightdark.png", "Light and dark",
         "The window chrome follows a light or dark theme — or tracks your desktop's preference, live."),
        ("lock.png",      "card-lock.png",      "Lock the app",
         "Guard the window behind a passcode, with optional auto-locking after a period of inactivity."),
        ("settings.png", "card-settings.png", "Every feature is a setting",
         "Themes, wallpaper, privacy blur, spell-check, tray and more — all toggles."),
        ("about.png",    "card-about.png",    "Report a bug in one click",
         "F1 opens About; the bug report is pre-filled with version, memory and logs."),
        ("scheduled.png", "card-scheduled.png", "Schedule messages",
         "Write a message now and have it sent later — it still goes out on the next launch if the app was closed when it came due."),
        ("main-dark.png", "card-main-dark.png", "Dark theme",
         "The whole of WhatsApp Web in a native window, following a dark theme — shown here with the privacy blur on."),
        ("main-light.png", "card-main-light.png", "Light theme",
         "The same main window in a light theme — the chrome can also track your desktop's light/dark preference, live."),
    ]
    for shot, name, title, sub in jobs:
        p = os.path.join(a.shots, shot)
        if os.path.exists(p):
            card(os.path.join(a.out, name), p, title, sub)
        else:
            print("  skip", name, "(missing", os.path.relpath(p, REPO) + ")")

if __name__ == "__main__":
    main()
