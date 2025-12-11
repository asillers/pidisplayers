#!/usr/bin/python3
import time, random, threading, os, sys
import board, digitalio
from PIL import Image, ImageDraw, ImageFont
from threading import Lock

spi_lock = Lock()

#
# ====== GC9A01 SETUP ======
#

import adafruit_rgb_display.gc9a01a as gc9a01a

spi = board.SPI()

gc9_cs = digitalio.DigitalInOut(board.D7)
gc9_dc = digitalio.DigitalInOut(board.D5)
gc9_rst = digitalio.DigitalInOut(board.D6)

gc9 = gc9a01a.GC9A01A(
    spi,
    cs=gc9_cs,
    dc=gc9_dc,
    rst=gc9_rst,
    baudrate=40_000_000)

gc9_w, gc9_h = gc9.width, gc9.height
gc9_img = Image.new("RGB", (gc9_w, gc9_h))
gc9_draw = ImageDraw.Draw(gc9_img)

unicode_font = ImageFont.truetype("DejaVuSans.ttf", 90)
symbols = ["★", "♥", "●", "☀", "☠"]

#
# ====== E-PAPER SETUP ======
#

libdir = os.path.join(os.path.dirname(__file__), "lib")
if os.path.exists(libdir):
    sys.path.append(libdir)

from waveshare_epd import epd2in9_V2
epd = epd2in9_V2.EPD()

epd.init()
epd.Clear(0xFF)

epd_width, epd_height = epd.width, epd.height

base = Image.new("1", (epd_width, epd_height), 255)
with spi_lock:
    epd.display_Base(epd.getbuffer(base))

running = True

#
# ====== EPD THREAD ======
#


def epd_loop():
    while running:
        img = Image.new("1", (epd_width, epd_height), 255)
        draw = ImageDraw.Draw(img)

        x = random.randint(0, epd_width - 40)
        y = random.randint(0, epd_height - 40)
        symbol = random.choice(symbols)

        draw.text((x, y), symbol, font=unicode_font, fill=0)

        with spi_lock:
            epd.display_Partial(epd.getbuffer(img))

        time.sleep(0.1)


thread = threading.Thread(target=epd_loop)
thread.start()

print("Dual display running. CTRL+C to quit.")

#
# ====== MAIN LOOP (GC9) ======
#

try:
    while True:
        x = random.randint(0, gc9_w - 100)
        y = random.randint(0, gc9_h - 100)
        symbol = random.choice(symbols)

        gc9_draw.text(
            (x, y),
            symbol,
            font=unicode_font,
            fill=(random.randrange(256), random.randrange(256),
                  random.randrange(256)))

        with spi_lock:
            gc9.image(gc9_img)

        time.sleep(0.01)

except KeyboardInterrupt:
    running = False
    thread.join()
    print("Stopped.")