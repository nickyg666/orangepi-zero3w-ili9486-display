#!/bin/bash
# Reset GNOME text scaling to readable defaults for the 960x640 LCD
DISPLAY=:1 XAUTHORITY=/run/user/1000/gdm/Xauthority gsettings set org.gnome.desktop.interface font-name "Cantarell 11"
DISPLAY=:1 XAUTHORITY=/run/user/1000/gdm/Xauthority gsettings set org.gnome.desktop.interface text-scaling-factor 1.0
DISPLAY=:1 XAUTHORITY=/run/user/1000/gdm/Xauthority gsettings set org.gnome.desktop.interface scaling-factor 1
