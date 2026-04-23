#!/bin/bash
# Standalone installation script for tgpt-gui
# This script downloads, compiles, and installs tgpt-gui in one go.

set -e

REPO_URL="https://github.com/gautamjangid/tgpt-gui.git"
TMP_DIR="/tmp/tgpt-gui-install-$(date +%s)"

echo "Starting standalone installation of tgpt-gui..."

# Handle sudo
if ! command -v sudo &> /dev/null; then
    SUDO="su -c"
else
    SUDO="sudo"
fi

# Ensure git is installed
if ! command -v git &> /dev/null; then
    echo "git is required but not installed. Installing git..."
    $SUDO apt-get update
    $SUDO apt-get install -y git
fi

# Clone repository
echo "Cloning repository to temporary directory..."
git clone "$REPO_URL" "$TMP_DIR"

# Run the inner installer
echo "Running installation..."
cd "$TMP_DIR"
chmod +x scripts/install.sh
./scripts/install.sh

# Clean up
echo "Cleaning up..."
cd /
rm -rf "$TMP_DIR"

echo "Standalone installation completed successfully. \n Type 'tgpt-gui' in terminal & press enter to start the application."
