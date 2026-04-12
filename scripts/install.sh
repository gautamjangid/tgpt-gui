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

# ----------------------------------------------------------------------
# Check for tgpt backend
# ----------------------------------------------------------------------
if ! command -v curl &> /dev/null; then
    echo "Installing curl..."
    $SUDO apt-get update
    $SUDO apt-get install -y curl
fi

if ! command -v tgpt &> /dev/null; then
    echo "Installing 'tgpt'..."
    curl -sSL https://raw.githubusercontent.com/aandrew-me/tgpt/main/install | bash -s /usr/local/bin
    if ! command -v tgpt &> /dev/null; then
        echo "Failed to install tgpt. Please check your internet connection."
        exit 1
    fi
fi

# ----------------------------------------------------------------------
# Install dependencies & compile
# ----------------------------------------------------------------------
echo "Installing FLTK dependencies..."
$SUDO apt-get update
$SUDO apt-get install -y build-essential libfltk1.3-dev xclip

SOURCE_DIR="../src"
BINARY_NAME="tgpt-gui"

# Ensure we're in the scripts directory or the paths will fail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
cd "$SCRIPT_DIR"

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: $SOURCE_DIR not found."
    exit 1
fi

echo "Compiling sources in $SOURCE_DIR ..."
g++ -std=c++14 -O2 "$SOURCE_DIR"/*.cpp -I../include -o "$BINARY_NAME" $(fltk-config --cxxflags --ldflags) -pthread

echo "Installing $BINARY_NAME to /usr/local/bin ..."
$SUDO mv "$BINARY_NAME" /usr/local/bin/

echo "Creating desktop shortcut..."
$SUDO tee /usr/share/applications/tgpt-gui.desktop > /dev/null <<EOF
[Desktop Entry]
Name=tgpt GUI
Comment=Lightweight AI Chat Interface
Exec=tgpt-gui
Icon=utilities-terminal
Terminal=false
Type=Application
Categories=Utility;Network;
EOF

echo "Installation complete. Run 'tgpt-gui' from terminal or menu."
