# ILI9486 SPI Display Support for Orange Pi Zero 3W

Completely broken AI-SLOP garbage implementation of ILI9486 3.5" SPI TFT display (480x320) with XPT2046 touchscreen for Orange Pi Zero 3W (A733/sun60iw2p1).
don't worry, when it refuses to boot after you enable the dtbo, you're totally fucked.
they didn't write any troubleshooting for that. maybe you could just disable it but it probably broke the kernel with the modules too, so roll those back as well 


## Features

- ✅ TinyDRM kernel module (ili9486 + drm_mipi_dbi)
- ✅ Device tree overlay for SPI3 configuration
- ✅ Framebuffer support at 30fps
- ✅ X11 configuration with fbdev driver
- ✅ Touch calibration for XPT2046
- ✅ Auto-load on boot
- ✅ 40 MHz SPI speed (ILI9486 maximum)

## Hardware Requirements

- Orange Pi Zero 3W (A733)
- Waveshare 3.5" SPI TFT display (ILI9486 controller)
- XPT2046 resistive touchscreen (included with display)

## Pin Mapping

| Function | GPIO | Pin | Description |
|----------|------|-----|-------------|
| SPI3-CS0 | PE0 | 24 | Display chip select |
| SPI3-CLK | PE1 | 23 | SPI clock |
| SPI3-MOSI | PE2 | 19 | Master out slave in |
| SPI3-MISO | PE3 | 21 | Master in slave out |
| SPI3-CS1 | PE4 | 26 | Touch chip select |
| Reset | PE5 | - | Display reset (active low) |
| DC | PE6 | - | Data/Command select |
| Touch IRQ | PK23 | 22 | Touch interrupt |

**Note**: Backlight is on by default (no GPIO control needed).

## Installation

### 1. Copy Files

```bash
# Device tree overlay
sudo cp sun60i-a733-spi-lcd.dtbo /boot/dtb-6.6.98-sun60iw2/allwinner/overlay/

# Kernel modules
sudo mkdir -p /lib/modules/6.6.98-sun60iw2/kernel/drivers/gpu/drm/tiny/
sudo cp drm_mipi_dbi.ko ili9486.ko /lib/modules/6.6.98-sun60iw2/kernel/drivers/gpu/drm/tiny/
sudo depmod -a

# X11 configuration
sudo cp 20-ili9486.conf /etc/X11/xorg.conf.d/
sudo cp 99-calibration.conf /etc/X11/xorg.conf.d/

# Auto-load configuration
sudo cp ili9486.conf /etc/modules-load.d/
```

### 2. Update Boot Configuration

Edit `/boot/orangepiEnv.txt` and add `spi-lcd` to overlays:

```
overlays=spi3-cs0-cs1-spidev spi-lcd uart2
```

### 3. Reboot

```bash
sudo reboot
```

## Verification

After reboot, verify the installation:

```bash
# Check modules are loaded
lsmod | grep ili9486

# Check framebuffer exists
ls -la /dev/fb*

# Check DRM connectors
cat /sys/class/drm/*/status

# Check kernel messages
dmesg | grep -i ili9486
```

Expected output:
- `/dev/fb1` should exist for SPI display
- `ili9486` and `drm_mipi_dbi` modules should be loaded
- DRM should show SPI-1 connector

## Usage

### Start X11 on SPI Display

```bash
startx
```

### Test Framebuffer

```bash
# Write test pattern
sudo dd if=/dev/urandom of=/dev/fb1 bs=1 count=$((320*480*2))
```

### Mirror HDMI to SPI Display

```bash
# Copy HDMI framebuffer to SPI display in real-time
while true; do
    dd if=/dev/fb0 of=/dev/fb1 bs=4096 2>/dev/null
    sleep 0.033
done
```

## Technical Details

### Display Configuration

- **Resolution**: 320x480 (after 270° rotation)
- **Color Depth**: 16-bit (RGB565)
- **Refresh Rate**: 30 Hz
- **SPI Speed**: 40 MHz
- **Rotation**: 270° (landscape mode)

### Touch Configuration

- **Controller**: XPT2046 (ADS7846 compatible)
- **SPI Speed**: 250 kHz
- **Calibration**: 3950 230 3900 300
- **Settings**: SwapAxes=1, InvertX=1

### Performance

- **Framebuffer Size**: 307 KB (320×480×2 bytes)
- **Transfer Time**: ~61ms at 40 MHz SPI
- **Achieved FPS**: ~16 FPS (SPI bandwidth limited)
- **Target FPS**: 30 FPS (requires partial updates)

## Troubleshooting

### Display Not Working

1. Check SPI device exists:
   ```bash
   ls -la /dev/spidev3.0
   ```

2. Verify overlay is loaded:
   ```bash
   cat /boot/orangepiEnv.txt | grep overlays
   ```

3. Check kernel messages:
   ```bash
   dmesg | grep -iE "ili9486|spi|drm"
   ```

### Touch Not Working

1. Check touch device:
   ```bash
   cat /proc/bus/input/devices | grep -A 5 ADS7846
   ```

2. Recalibrate:
   ```bash
   sudo apt-get install xinput-calibrator
   xinput_calibrator
   ```

### X11 Not Starting

1. Check X11 log:
   ```bash
   cat /var/log/Xorg.0.log
   ```

2. Verify fbdev driver:
   ```bash
   sudo apt-get install xserver-xorg-video-fbdev
   ```

## Building from Source

### Kernel Modules

```bash
# Download kernel source
cd /tmp
curl -sL "https://raw.githubusercontent.com/torvalds/linux/v6.6/drivers/gpu/drm/tiny/ili9486.c" -o ili9486.c
curl -sL "https://raw.githubusercontent.com/torvalds/linux/v6.6/drivers/gpu/drm/drm_mipi_dbi.c" -o drm_mipi_dbi.c

# Create Makefile
cat > Makefile << 'MAKEFILE'
obj-m += drm_mipi_dbi.o ili9486.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
MAKEFILE

# Build
make

# Install
sudo cp *.ko /lib/modules/6.6.98-sun60iw2/kernel/drivers/gpu/drm/tiny/
sudo depmod -a
```

### Device Tree Overlay

```bash
# Preprocess
cpp -nostdinc -I/usr/src/linux-headers-6.6.98-sun60iw2/include \
    -I/usr/src/linux-headers-6.6.98-sun60iw2/arch/arm64/boot/dts \
    -I/usr/src/linux-headers-6.6.98-sun60iw2/arch/arm64/boot/dts/allwinner \
    -undef -D__DTS__ -x assembler-with-cpp \
    sun60i-a733-spi-lcd.dts > spi-lcd.i

# Compile
dtc -O dtb -o sun60i-a733-spi-lcd.dtbo -b 0 -@ - < spi-lcd.i
```

## Files

| File | Purpose |
|------|---------|
| `sun60i-a733-spi-lcd.dts` | Device tree source |
| `sun60i-a733-spi-lcd.dtbo` | Compiled overlay |
| `drm_mipi_dbi.ko` | MIPI DBI helper module |
| `ili9486.ko` | ILI9486 TinyDRM module |
| `20-ili9486.conf` | X11 display configuration |
| `99-calibration.conf` | Touch calibration |
| `ili9486.conf` | Module auto-load config |

## References

- [ILI9486 Datasheet](https://www.waveshare.com/w/upload/3/3c/ILI9486.pdf)
- [XPT2046 Datasheet](https://www.waveshare.com/w/upload/4/4e/XPT2046.pdf)
- [Orange Pi Zero 3W User Manual](http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/service-and-support/Orange-Pi-Zero-3W.html)
- [Linux Kernel v6.6](https://github.com/torvalds/linux/tree/v6.6)

## License

This project is provided as-is for educational and personal use.

## Credits

- ILI9486 TinyDRM driver: Kamlesh Gurudasani
- Orange Pi: Shenzhen Xunlong Software Co., Ltd
- Waveshare: Display hardware manufacturer
