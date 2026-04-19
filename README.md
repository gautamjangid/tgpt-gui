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

### Copy Functionality
The application's **Copy Selected** button at the bottom provides a seamless dual-action mode:
- **Highlighted Text:** If you manually highlight any text in the chat view with your mouse, clicking the button instantly copies that selection to your clipboard (equivalent to pressing `Ctrl+C`).
- **Code Blocks:** If nothing is selected, you can use the arrow buttons to cycle through all recognized backend code blocks in the current view and copy them natively with a single click.

## How to use tgpt-cli Settings

`tgpt-gui` provides an integrated interface to configure the underlying AI runtime natively directly from the GUI (found under **Settings -> tgpt-cli**).

*   **Provider:** Determines which backend service handles your chat queries. Free providers (such as `duckduckgo`, `phind`, `ollama`) are immediately accessible and require no extra setup. If you select one of these, the Model and API Key inputs will deactivate since they aren't needed.
*   **Model:** Specifies the exact neural network to query. This is typically only required if you use a top-tier provider like `openai`.
*   **API Key:** An authenticated token required for premium endpoints like OpenAI or Anthropic. Leave this empty if you are using free providers.
*   **Extra Args:** Space-separated CLI flags passed directly to tgpt. Supports `-c`, `-s`, `-i` and other flags (see Advanced CLI Modes below).

*(Hint: If you're unsure where to begin, simply leave all settings blank to default securely to the baseline free provider!)*

## Advanced CLI Modes

tgpt-gui fully supports tgpt's advanced CLI modes. Set these flags in **Settings → tgpt-cli → Extra Args**.

### Code Mode (`-c`)

Optimized for generating code. Output is rendered in a dark-themed code block with **preserved indentation and whitespace**.

```
Extra Args: -c
Prompt: "write python code to draw a circle"
```

The generated code will appear in a styled `<pre>` block instead of being treated as markdown.

### Shell Mode (`-s`)

Generates shell commands from natural language. When tgpt suggests a command, a **popup dialog** will appear showing the command and asking for confirmation.

```
Extra Args: -s
Prompt: "list all files in current directory"
```

- If you click **Yes**: the command executes immediately and the output is displayed directly in the chat window.
- If you click **No**: the command is not executed and a notification appears in the chat.

**How it works:**
1. You ask a question like "how to update my system?"
2. tgpt generates a suggested command
3. A popup dialog appears showing the command with **Yes**/**No** buttons
4. On **Yes**: The command runs and output appears in chat
5. On **No**: Command is skipped

> **Note:** Shell mode uses a custom confirmation dialog instead of tgpt's interactive mode for better reliability and to prevent UI issues.

### Interactive Mode (`-i`)

Starts a persistent conversation session where tgpt maintains context between prompts.

```
Extra Args: -i
```

- The first prompt starts the tgpt interactive process.
- Subsequent prompts are sent to the **same running process**, preserving conversation context.
- The session stays alive until you click **Cancel** or **Clear Chat**.
- Response completion is dynamically auto-detected via intelligent prompt-scraping (`>>>`) and Native PTY (Pseudo-Terminal) integration to ensure lightning-fast, buffer-free streaming.

## Uninstallation

To completely remove the application and its dependencies, run the provided uninstaller script:

```bash
chmod +x scripts/uninstall.sh
./scripts/uninstall.sh
```

You'll be prompted to optionally clear out chat history (`~/.tgpt-gui`) and remove underlying dependencies (like FLTK or the core `tgpt` CLI) if you don't use them anywhere else within your system.