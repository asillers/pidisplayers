import board
import digitalio
import adafruit_rgb_display.gc9a01a as gc9a01a
import busio

spi = board.SPI()  # the one gc9a01a actually uses
print("SPI object:", spi)

try:
    print("MOSI:", spi.mosi)
    print("SCK:", spi.clock)
except Exception as e:
    print("No direct MOSI/SCK attributes — may be bitbang SPI.")