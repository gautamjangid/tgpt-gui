#include "Process.hpp"
#include "Globals.hpp"
#include "Utils.hpp"
#include "History.hpp"
#include "UI.hpp" // for redraw_chat_window, timer_redraw_cb

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <ctime>
#include <vector>
#include <sstream>
#include <fstream>
#include <FL/fl_ask.H>
#include <stdlib.h>

namespace tgpt {

void finish_processing(bool cancelled) {
    processing = false;
    Fl::remove_timeout(timer_redraw_cb);
    btn_send->activate();

    if (child_pipe != -1) {
        Fl::remove_fd(child_pipe);
        close(child_pipe);
        child_pipe = -1;
    }
    if (child_pid > 0) {
        if (cancelled) kill(child_pid, SIGTERM);
        waitpid(child_pid, nullptr, 0);
        child_pid = 0;
    }

    // Handle shell mode output saving to file
    if (is_shell_mode && shell_save_output && !cancelled) {
        std::string cmd_output = sanitize_output(
            current_response_raw.substr(
                std::min(shell_response_start_pos, current_response_raw.size())));
        if (!cmd_output.empty()) {
            std::string output_dir = get_app_dir() + "/shell_output";
            ensure_directory_exists(output_dir);
            time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            char fname[128];
            strftime(fname, sizeof(fname), "%Y%m%d_%H%M%S_output.txt", t);
            std::string filepath = output_dir + "/" + fname;
            std::ofstream ofs(filepath);
            if (ofs.is_open()) {
                ofs << cmd_output;
            }
            current_response_raw += "\n\n[Command output saved to: " + filepath + "]";
        }
    }

    std::string final_response;
    if (cancelled) {
        final_response = sanitize_output(current_response_raw) + "\n*(Cancelled)*";
    } else {
        final_response = sanitize_output(current_response_raw);
    }

    if (!chat_history.empty() && !current_response_saved) {
        chat_history.back().response = final_response;
        save_history_entry(chat_history.back().prompt, final_response,
                           chat_history.back().code_mode);
    }

    // Reset mode flags
    // Note: Don't reset shell_prompt_shown here as it causes double dialog in -s mode
    shell_save_output = false;
    shell_response_start_pos = 0;
    is_code_mode = false;
    is_shell_mode = false;
    current_response_saved = false;

    // Force one last redraw to fix block extraction
    redraw_chat_window();
}

void cancel_cb(Fl_Widget*, void*) {
    if (processing) {
        finish_processing(true);
    }
}

// Shell mode: show Yes/No dialog for command execution
void show_shell_confirm_dialog() {
    std::string sanitized = sanitize_output(current_response_raw);
    std::string command   = sanitized;

    // ── Step 1: Extract command from markdown code block if present ──
    bool found_code_block = false;
    size_t code_start = command.find("```");
    if (code_start != std::string::npos) {
        size_t code_end = command.find("```", code_start + 3);
        if (code_end != std::string::npos) {
            command = command.substr(code_start + 3, code_end - code_start - 3);
            found_code_block = true;
        }
    }

    if (found_code_block) {
        // Remove language tag on first line (e.g. "bash\n")
        size_t lang_end = command.find('\n');
        if (lang_end != std::string::npos && lang_end < 20) {
            std::string first = command.substr(0, lang_end);
            if (first.find(' ') == std::string::npos && first.length() < 15)
                command = command.substr(lang_end + 1);
        }
    } else {
        // No code block — strip noise and pick the last meaningful command line.
        // Remove [y/n] prompt text and anything after it.
        const char* yesno_patterns[] = {
            "Execute shell command?", "[y/n]", "(y/n)", "[Y/n]", "[y/N]",
            "Enter your choice", nullptr
        };
        for (int i = 0; yesno_patterns[i]; ++i) {
            size_t pos = command.find(yesno_patterns[i]);
            if (pos != std::string::npos) { command = command.substr(0, pos); break; }
        }

        // Walk lines and keep the last line that looks like a real shell command.
        // Skip: empty lines, non-ASCII first char (spinner glyphs), and noise words.
        std::istringstream ss(command);
        std::string ln, best;
        while (std::getline(ss, ln)) {
            size_t ns = ln.find_first_not_of(" \t\r");
            if (ns == std::string::npos) continue;
            ln = ln.substr(ns);
            if (ln.empty()) continue;
            if ((unsigned char)ln[0] > 127) continue;   // Unicode spinner / box char
            if (ln.find("Loading")  != std::string::npos) continue;
            if (ln.find("Thinking") != std::string::npos) continue;
            if (ln.find("tgpt")     != std::string::npos) continue;
            best = ln;   // last valid line wins
        }
        if (!best.empty()) command = best;
    }

    // ── Step 2: Final trim + remove leftover prompt artifacts ──
    auto trim_str = [](std::string& s) {
        size_t f = s.find_first_not_of(" \t\n\r");
        size_t l = s.find_last_not_of(" \t\n\r");
        s = (f == std::string::npos) ? "" : s.substr(f, l - f + 1);
    };
    trim_str(command);
    for (const char* pat : {"[y/n]","(y/n)","[Y/n]","[y/N]","tgpt:","tgpt -s"}) {
        size_t pos = command.find(pat);
        if (pos != std::string::npos) command = command.substr(0, pos);
    }
    trim_str(command);

    // ── Step 3: Remove pipe fd handler — we will NOT re-add it.
    //    After the dialog we call finish_processing() which kills the child
    //    process and closes the fd, so no further pipe_read_cb events fire.
    if (child_pipe != -1) {
        Fl::remove_fd(child_pipe);
    }
    Fl::remove_timeout(shell_mode_timeout_cb);

    // Set shell_prompt_shown to true to prevent re-triggering
    shell_prompt_shown = true;

    // ── Step 4: Show confirmation dialog ──
    int choice = fl_choice(
        "tgpt suggests running:\n\n%s\n\nExecute this command?",
        "No", "Yes", nullptr, command.c_str());

    // ── Step 5: Handle choice ──
    if (choice == 1) {
        shell_save_output        = true;
        shell_response_start_pos = current_response_raw.size();

        std::string cmd_with_output = command + " 2>&1";
        FILE* fp = popen(cmd_with_output.c_str(), "r");
        if (fp) {
            char buffer[256];
            std::string cmd_output;
            while (fgets(buffer, sizeof(buffer), fp) != nullptr)
                cmd_output += buffer;
            pclose(fp);

            current_response_raw += cmd_output.empty()
                ? "\n\n[Command executed successfully (no output)]"
                : "\n\n--- Command Output ---\n" + cmd_output;
        } else {
            current_response_raw += "\n\n[Failed to execute command]";
        }
    } else {
        shell_save_output = false;
        current_response_raw += "\n\n[Command not executed by user]";
    }

    // ── Step 6: Finish — kills child, closes pipe, saves history, redraws ──
    finish_processing(false);
}

// Shell mode timeout - show dialog after receiving some output
void shell_mode_timeout_cb(void*) {
    if (is_shell_mode && !shell_prompt_shown && processing && !current_response_raw.empty()) {
        shell_prompt_shown = true;
        show_shell_confirm_dialog();
    }
}

// Pipe reader for streaming output
void pipe_read_cb(int fd, void*) {
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0) {
        current_response_raw.append(buffer, bytes);
        needs_redraw = true;

        // Shell mode: detect command and show confirmation dialog
        if (is_shell_mode && !shell_prompt_shown) {
            std::string sanitized = sanitize_output(current_response_raw);
            if (sanitized.length() > 100 ||
                (sanitized.length() > 30 && sanitized.find("tgpt") != std::string::npos)) {
                shell_prompt_shown = true;
                Fl::remove_timeout(shell_mode_timeout_cb);
                show_shell_confirm_dialog();
            }
        }
    } else if (bytes == 0 || (bytes == -1 && errno != EAGAIN && errno != EINTR)) {
        // Process ended
        finish_processing(false);
    }
}

void send_cb(Fl_Widget*, void*) {
    if (processing) return;

    std::string prompt = input_box->value();
    if (prompt.empty()) return;

    // Manage history size
    if ((int)chat_history.size() >= history_limit) {
        if (chat_history.size() > 0) chat_history.pop_front();
    }

    // Detect modes from custom args
    is_code_mode = false;
    is_shell_mode = false;
    std::istringstream iss(tgpt_custom_args);
    std::string arg;
    while (iss >> arg) {
        if (arg == "-c" || arg == "--code") is_code_mode = true;
        if (arg == "-s" || arg == "--shell") is_shell_mode = true;
    }

    // Create history entry
    ChatEntry entry;
    entry.prompt = prompt;
    entry.code_mode = is_code_mode;
    chat_history.push_back(entry);

    input_box->value("");
    current_response_raw.clear();
    current_response_saved = false;
    shell_prompt_shown = false;
    shell_save_output = false;
    shell_response_start_pos = 0;
    processing = true;
    btn_send->deactivate();

    redraw_chat_window();
    Fl::check();

    needs_redraw = true;
    Fl::add_timeout(0.15, timer_redraw_cb);

    // -------------------------------------------------------------------
    // Launch a new tgpt child process
    // -------------------------------------------------------------------
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        finish_processing(true);
        return;
    }

    // Non-blocking read end
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    if (is_shell_mode) {
        Fl::add_timeout(3.0, shell_mode_timeout_cb);
    }

    child_pid = fork();

    if (child_pid == 0) {
        // ----------------------------------------------------------------
        // CHILD PROCESS
        // ----------------------------------------------------------------
        // stdout/stderr → pipe to parent
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (is_shell_mode) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull != -1) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        // Build argv
        std::vector<char*> args;
        args.push_back((char*)"tgpt");
        if (!tgpt_provider.empty()) {
            args.push_back((char*)"--provider");
            args.push_back((char*)tgpt_provider.c_str());
        }
        if (!tgpt_model.empty()) {
            args.push_back((char*)"--model");
            args.push_back((char*)tgpt_model.c_str());
        }
        if (!tgpt_api_key.empty()) {
            args.push_back((char*)"--key");
            args.push_back((char*)tgpt_api_key.c_str());
        }

        std::vector<std::string> custom_args_storage;
        if (!tgpt_custom_args.empty()) {
            std::istringstream iss2(tgpt_custom_args);
            std::string a;
            while (iss2 >> a) {
                custom_args_storage.push_back(a);
            }
            for (auto& st : custom_args_storage) {
                args.push_back((char*)st.c_str());
            }
        }

        args.push_back((char*)prompt.c_str());
        args.push_back(nullptr);

        execvp("tgpt", args.data());
        _exit(127);

    } else if (child_pid > 0) {
        // ----------------------------------------------------------------
        // PARENT PROCESS — set up fd watching
        // ----------------------------------------------------------------
        close(pipefd[1]);
        child_pipe = pipefd[0];
        Fl::add_fd(child_pipe, FL_READ, pipe_read_cb);

    } else {
        // fork() failed
        close(pipefd[0]);
        close(pipefd[1]);
        finish_processing(true);
    }
}

} // namespace tgpt
