#!/bin/bash
# Self-fix Windows line endings
if grep -q $'\r' "$0"; then
    echo "Converting script to Unix format..."
    sed -i 's/\r$//' "$0"
    exec "$0" "$@"
fi

set -e  # Exit on error

# ----------------------------------------------------------------------
# Handle sudo / su
# ----------------------------------------------------------------------
if ! command -v sudo &> /dev/null; then
    SUDO="su -c"
else
    SUDO="sudo"
fi

echo "Uninstalling tgpt-gui..."

# Remove main binaries and files
if [ -f /usr/local/bin/tgpt-gui ]; then
    $SUDO rm -f /usr/local/bin/tgpt-gui
    echo "Removed /usr/local/bin/tgpt-gui"
fi

if [ -f /usr/share/applications/tgpt-gui.desktop ]; then
    $SUDO rm -f /usr/share/applications/tgpt-gui.desktop
    echo "Removed /usr/share/applications/tgpt-gui.desktop"
fi

# Ask about config and chats
read -p "Do you want to clear your chat history & configuration? (~/.tgpt-gui) [y/N]: " rm_config
if [[ "$rm_config" =~ ^[Yy]$ ]]; then
    rm -rf ~/.tgpt-gui
    echo "Removed ~/.tgpt-gui"
fi

# Ask about system dependencies
read -p "Do you want to uninstall system dependencies (tgpt, FLTK)? Warning: This may affect other software. [y/N]: " rm_deps
if [[ "$rm_deps" =~ ^[Yy]$ ]]; then
    echo "Removing tgpt..."
    $SUDO rm -f /usr/local/bin/tgpt
    
    echo "Removing FLTK dependencies..."
    $SUDO apt-get remove -y libfltk1.3-dev
    $SUDO apt-get autoremove -y
fi

echo "Uninstallation complete!"
