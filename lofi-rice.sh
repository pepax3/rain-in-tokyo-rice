#!/bin/bash

# Install packages
sudo pacman -S base-devel xorg xorg-server xorg-xinit libx11 libxft libxinerama webkit2gtk zsh xcompmgr flameshot feh xorg-xrandr dunst clang

# Move background.png to the appropriate location
mv background.png ~/.local/share/background.png

# Install DWM
cd dwm
sudo make clean install
cd ..

# Install dmenu
cd dmenu
sudo make clean install
cd ..

# Install st (Simple Terminal)
cd st
sudo make clean install
cd ..

# Install slstatus
cd slstatus
sudo make clean install
cd ..

# Install Oh My Zsh
sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"

# Additional commands
chmod +x install.sh
./install.sh

# Final message
echo "Your DWM rice is installed. If you would like to support me, you can buy me a coffee at: https://ko-fi.com/pepax3 THANKS BYE!"

