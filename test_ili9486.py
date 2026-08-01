#!/usr/bin/env python3
import spidev
import subprocess
import time

LCD_DC = 355   # PL3, wPi 10, physical pin 18
LCD_RST = 96   # PD0, wPi 13, physical pin 22
SPI_BUS = 3
SPI_DEVICE = 1  # Using CE1 on SPI3

def gpio_set(gpio, val):
    if val:
        subprocess.run(["gpio", "-g", "write", str(gpio), "1"], check=True)
    else:
        subprocess.run(["gpio", "-g", "write", str(gpio), "0"], check=True)

def gpio_mode(gpio, mode):
    subprocess.run(["gpio", "-g", "mode", str(gpio), mode], check=True)

def cmd(spi, c):
    gpio_set(LCD_DC, 0)
    spi.xfer2([c])

def data(spi, d):
    gpio_set(LCD_DC, 1)
    spi.xfer2(d)

def ili9486_init(spi):
    gpio_mode(LCD_DC, "out")
    gpio_mode(LCD_RST, "out")

    # Reset
    gpio_set(LCD_RST, 0)
    time.sleep(0.01)
    gpio_set(LCD_RST, 1)
    time.sleep(0.15)

    # SW reset
    cmd(spi, 0x01)
    time.sleep(0.15)

    # Exit sleep
    cmd(spi, 0x11)
    time.sleep(0.12)

    # Pixel format: 16-bit RGB565
    cmd(spi, 0x3A)
    data(spi, [0x55])

    # Memory access control - MADCTL: BGR, row/col swap per ILI9486 datasheet
    cmd(spi, 0x36)
    data(spi, [0x08])

    # Display inversion ON
    cmd(spi, 0x21)

    # Display on
    cmd(spi, 0x29)
    time.sleep(0.02)

def set_window(spi, x0, y0, x1, y1):
    cmd(spi, 0x2A)
    data(spi, [(x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF])

    cmd(spi, 0x2B)
    data(spi, [(y0 >> 8) & 0xFF, y0 & 0xFF, (y1 >> 8) & 0xFF, y1 & 0xFF])

    cmd(spi, 0x2C)

def fill_rect(spi, x0, y0, x1, y1, r, g, b):
    color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
    hi = (color >> 8) & 0xFF
    lo = color & 0xFF
    px = bytes([hi, lo])
    pixels = px * ((x1 - x0 + 1) * (y1 - y0 + 1))
    set_window(spi, x0, y0, x1, y1)
    total = len(pixels)
    sent = 0
    while sent < total:
        batch = min(4096, total - sent)
        data(spi, list(pixels[sent:sent + batch]))
        sent += batch

def draw_color_bars(spi, width=320, height=480):
    n = 8
    band = height // n
    colors = [(255, 0, 0), (255, 165, 0), (255, 255, 0), (0, 255, 0),
              (0, 0, 255), (0xF0, 0x30, 0xFF), (0xFF, 0xFF, 0xFF), (0, 0, 0)]
    for i in range(n):
        y0 = i * band
        y1 = (i + 1) * band - 1 if i < n - 1 else height - 1
        r, g, b = colors[i]
        fill_rect(spi, 0, y0, width - 1, y1, r, g, b)
        print(f"  bar {i+1}/{n}: RGB({r},{g},{b})")

def main():
    spi = spidev.SpiDev()
    spi.open(SPI_BUS, SPI_DEVICE)
    spi.max_speed_hz = 32000000
    spi.mode = 0
    spi.bits_per_word = 8
    print(f"Opened spidev{SPI_BUS}.{SPI_DEVICE}")

    print("Resetting display...")
    ili9486_init(spi)
    print("ILI9486 initialized!")

    print("Drawing color bars...")
    draw_color_bars(spi)
    print("Done! Display should show 8 color bars.")

    spi.close()

if __name__ == "__main__":
    main()
