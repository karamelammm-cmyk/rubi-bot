"""Captcha solver watcher using Gemini 2.5 Flash vision.

Watches for captcha_solve.flag written by the bot, crops the captcha region
from the screenshot, asks Gemini which image matches the target letters,
writes the answer (1-6) to captcha_answer.txt for the bot to send.

Usage:
    set GEMINI_API_KEY=...
    python captcha_solver.py
"""
import os, sys, time, io
from google import genai
from google.genai import types
from PIL import Image

KEY_PATHS = [
    r"C:\ProgramData\mdata\gemini_key.txt",
]
FLAG_PATH = r"C:\ProgramData\mdata\captcha_solve.flag"
ANSWER_PATH = r"C:\ProgramData\mdata\captcha_answer.txt"
LOG_PATH = r"C:\ProgramData\mdata\captcha_solver.log"

MODEL = "gemini-2.5-flash"


def load_key():
    k = os.environ.get("GEMINI_API_KEY", "").strip()
    if k:
        return k
    for p in KEY_PATHS:
        if os.path.exists(p):
            with open(p, "r") as f:
                k = f.read().strip()
            if k:
                return k
    return None


def log(msg):
    ts = time.strftime("%H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line)
    try:
        with open(LOG_PATH, "a") as f:
            f.write(line + "\n")
    except Exception:
        pass


def parse_flag(path):
    d = {}
    with open(path, "r") as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                d[k] = v
    return d


def solve(client, flag):
    ss = flag.get("SCREENSHOT", "")
    target = flag.get("TARGET", "").strip()
    if not target or not ss or not os.path.exists(ss):
        return None, "missing ss or target"

    img = Image.open(ss)

    # Compute captcha bbox from the 6 IMG positions (adds margin for aesthetic)
    xs = []
    ys = []
    for i in range(1, 7):
        try:
            x = int(flag[f"IMG_{i}_X"])
            y = int(flag[f"IMG_{i}_Y"])
            w = int(flag[f"IMG_{i}_W"])
            h = int(flag[f"IMG_{i}_H"])
        except (KeyError, ValueError):
            continue
        if x <= 0 or y <= 0:
            continue
        xs.extend([x, x + w])
        ys.extend([y, y + h])

    if xs and ys:
        pad = 12
        crop_box = (
            max(0, min(xs) - pad),
            max(0, min(ys) - pad),
            min(img.width, max(xs) + pad),
            min(img.height, max(ys) + pad),
        )
        img = img.crop(crop_box)
        log(f"Cropped to captcha region: {crop_box} -> {img.size}")

    buf = io.BytesIO()
    img.save(buf, format="PNG")
    img_bytes = buf.getvalue()

    prompt = (
        f"This is a Metin2 captcha. 6 small tile images are arranged in a 2x3 grid "
        f"(top row positions 1,2,3 left-to-right; bottom row positions 4,5,6 left-to-right). "
        f"Each tile shows 1-2 letters/digits in stylized fonts on a colored background. "
        f"The target text is '{target}'. "
        f"Which position (1-6) EXACTLY matches the target '{target}'? "
        f"Respond with ONLY the digit 1 2 3 4 5 or 6, nothing else."
    )

    t0 = time.time()
    resp = client.models.generate_content(
        model=MODEL,
        contents=[prompt, types.Part.from_bytes(data=img_bytes, mime_type="image/png")],
    )
    dt = time.time() - t0
    raw = (resp.text or "").strip()
    digit = None
    for c in raw:
        if c in "123456":
            digit = int(c)
            break
    return digit, f"model={MODEL} latency={dt:.2f}s raw={raw!r}"


def main():
    key = load_key()
    if not key:
        log("ERROR: GEMINI_API_KEY not set and no gemini_key.txt file found")
        log(f"  Set env var GEMINI_API_KEY or create {KEY_PATHS[0]}")
        sys.exit(1)

    client = genai.Client(api_key=key)
    log(f"Captcha solver started (model={MODEL})")
    log(f"Watching: {FLAG_PATH}")

    last_mtime = 0
    while True:
        try:
            if os.path.exists(FLAG_PATH):
                mtime = os.path.getmtime(FLAG_PATH)
                if mtime != last_mtime:
                    last_mtime = mtime
                    try:
                        flag = parse_flag(FLAG_PATH)
                        target = flag.get("TARGET", "?")
                        log(f"CAPTCHA detected target='{target}'")
                        answer, info = solve(client, flag)
                        log(f"  Gemini: {info}")
                        if answer is not None:
                            with open(ANSWER_PATH, "w") as f:
                                f.write(f"{answer}\n")
                            log(f"  Answer written: {answer}")
                            # Flag removal is handled by bot after packet send,
                            # but if solver tries before bot picks up, the flag
                            # mtime check above prevents re-solving the same captcha.
                        else:
                            log(f"  [!] Could not extract digit from response")
                    except Exception as e:
                        log(f"  ERROR solving: {type(e).__name__}: {e}")
            time.sleep(0.1)  # 100ms poll
        except KeyboardInterrupt:
            log("Solver stopped (Ctrl+C)")
            break
        except Exception as e:
            log(f"Outer loop error: {e}")
            time.sleep(1)


if __name__ == "__main__":
    main()
