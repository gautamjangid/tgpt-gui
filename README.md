# tgpt GUI

A lightweight, high-performance Graphical User Interface for the `tgpt` terminal tool. This application provides a streaming "typing" effect, chat history management, and is specifically optimized for legacy hardware and low-resource environments.

## Supported Systems

This application is designed to run on almost any Linux distribution featuring the X Window System. It is primarily optimized for lightweight and legacy distributions like **AntiX Linux**, but works flawlessly on Ubuntu, Debian, Arch Linux, Linux Mint, and others.

## Prerequisites

The installation script will automatically handle most prerequisites, but ensures you have internet access. The following components are required and will be installed if missing:

- `curl` (to fetch the tgpt backend)
- `tgpt` (the terminal AI backend)
- `build-essential` (for compiling C++ code)
- `libfltk1.3-dev` (FLTK library for the GUI interface)
- `xclip` (for clipboard support)

## Installation & Compilation

The repository includes an installer script that simplifies the entire setup process. It will install necessary dependencies, download the `tgpt` backend if not found, compile the GUI application, and create a desktop shortcut.

1. Navigate to the project root directory.
2. Give the installation script executable permissions:
   ```bash
   chmod +x scripts/install.sh
   ```
3. Run the script:
   ```bash
   ./scripts/install.sh
   ```

## Manual Compilation (Optional)

If you prefer to compile the application manually without the installer, ensure you have the prerequisites installed, then run the following compilation command from the `scripts` directory:

```bash
g++ -std=c++14 -O2 ../src/main.cpp -o tgpt-gui $(fltk-config --cxxflags --ldflags) -pthread
```

## Usage

Once installed, you can launch the application by:
1. Finding "**tgpt GUI**" in your application menu/launcher.
2. Or by running the following command in your terminal:
   ```bash
   tgpt-gui
   ```

## Uninstallation

To completely remove the application and its dependencies, run the provided uninstaller script:

```bash
chmod +x scripts/uninstall.sh
./scripts/uninstall.sh
```

You'll be prompted to optionally clear out chat history (`~/.tgpt-gui`) and remove underlying dependencies (like FLTK or the core `tgpt` CLI) if you don't use them anywhere else within your system.