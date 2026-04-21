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
    shell_prompt_shown = false;
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
    std::string command = sanitized;

    // Remove markdown code blocks if present
    size_t code_start = command.find("```");
    if (code_start != std::string::npos) {
        size_t code_end = command.find("```", code_start + 3);
        if (code_end != std::string::npos) {
            command = command.substr(code_start + 3, code_end - code_start - 3);
        }
    }

    // Remove language identifier after opening code block
    size_t lang_end = command.find('\n');
    if (lang_end != std::string::npos && lang_end < 20) {
        std::string first_line = command.substr(0, lang_end);
        if (first_line.find(' ') == std::string::npos && first_line.length() < 15) {
            command = command.substr(lang_end + 1);
        }
    }

    // Trim whitespace and newlines
    size_t start = command.find_first_not_of(" \t\n\r");
    size_t end = command.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        command = command.substr(start, end - start + 1);
    }

    // Remove prompt patterns
    const char* patterns[] = {"[y/n]", "(y/n)", "[Y/n]", "[y/N]", "Enter your choice", "tgpt:", "tgpt -s"};
    for (const char* pat : patterns) {
        size_t pos = command.find(pat);
        if (pos != std::string::npos) {
            command = command.substr(0, pos);
        }
    }

    // Trim again
    start = command.find_first_not_of(" \t\n\r");
    end = command.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        command = command.substr(start, end - start + 1);
    }

    // Temporarily remove pipe handler to prevent re-entrancy during modal dialog
    if (child_pipe != -1) {
        Fl::remove_fd(child_pipe);
    }

    int choice = fl_choice(
        "tgpt suggests running:\n\n%s\n\nExecute this command?",
        "No", "Yes", nullptr, command.c_str());

    // Re-add pipe handler
    if (child_pipe != -1) {
        Fl::add_fd(child_pipe, FL_READ, pipe_read_cb);
    }

    if (choice == 1) {
        shell_save_output = true;
        shell_response_start_pos = current_response_raw.size();

        std::string cmd_with_output = command + " 2>&1";
        FILE* pipe = popen(cmd_with_output.c_str(), "r");
        if (pipe) {
            char buffer[256];
            std::string cmd_output;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                cmd_output += buffer;
            }
            pclose(pipe);

            if (!cmd_output.empty()) {
                current_response_raw += "\n\n--- Command Output ---\n" + cmd_output;
            } else {
                current_response_raw += "\n\n[Command executed successfully (no output)]";
            }
        } else {
            current_response_raw += "\n\n[Failed to execute command]";
        }

        shell_prompt_shown = false;
    } else {
        current_response_raw += "\n\n[Command not executed by user]";
        shell_prompt_shown = false;
    }

    Fl::remove_timeout(timer_redraw_cb);
    processing = false;
    btn_send->activate();
    redraw_chat_window();
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
