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

namespace tgpt {

void finish_processing(bool cancelled) {
    processing = false;
    Fl::remove_timeout(timer_redraw_cb);
    Fl::remove_timeout(interactive_response_timeout_cb);
    btn_send->activate();

    if (child_pipe != -1) {
        Fl::remove_fd(child_pipe);
        close(child_pipe);
        child_pipe = -1;
    }
    if (child_stdin_pipe != -1) {
        close(child_stdin_pipe);
        child_stdin_pipe = -1;
    }
    if (child_pid > 0) {
        if (cancelled) kill(child_pid, SIGTERM);
        waitpid(child_pid, nullptr, 0);
        child_pid = 0; // waitpid reaps the defunct child
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
            // Append note to response so user knows where output was saved
            current_response_raw += "\n\n[Command output saved to: " + filepath + "]";
        }
    }

    std::string final_response;
    if (cancelled) {
        final_response = sanitize_output(current_response_raw) + "\n*(Cancelled)*";
    } else if (is_interactive_mode || interactive_session_active) {
        final_response = strip_interactive_prompts(sanitize_output(current_response_raw));
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
    interactive_session_active = false;
    is_interactive_mode = false;
    is_code_mode = false;
    is_shell_mode = false;
    current_response_saved = false;

    // Force one last redraw to fix block extraction
    redraw_chat_window();
}

void cancel_cb(Fl_Widget*, void*) {
    if (processing || interactive_session_active) {
        finish_processing(true);
    }
}

// Called when no data received for ~2s in interactive mode
void interactive_response_timeout_cb(void*) {
    if (!interactive_session_active || !processing) return;

    processing = false;
    btn_send->activate();
    Fl::remove_timeout(timer_redraw_cb);

    std::string final_response = strip_interactive_prompts(
        sanitize_output(current_response_raw));

    if (!chat_history.empty()) {
        chat_history.back().response = final_response;
        save_history_entry(chat_history.back().prompt, final_response,
                           chat_history.back().code_mode);
        current_response_saved = true;
    }

    redraw_chat_window();
    // Note: child process and pipes stay alive for follow-up prompts
}

// Shell mode: show Yes/No dialog for command execution
void show_shell_confirm_dialog() {
    std::string sanitized = sanitize_output(current_response_raw);

    // Extract the suggested command (text before the y/n prompt)
    std::string command = sanitized;
    size_t prompt_pos = std::string::npos;
    // Try common prompt patterns
    const char* patterns[] = {"[y/n]", "(y/n)", "[Y/n]", "[y/N]"};
    for (const char* pat : patterns) {
        size_t pos = sanitized.rfind(pat);
        if (pos != std::string::npos) {
            prompt_pos = pos;
            break;
        }
    }
    if (prompt_pos != std::string::npos) {
        command = sanitized.substr(0, prompt_pos);
    }

    // Trim whitespace from command display
    size_t start = command.find_first_not_of(" \t\n\r");
    size_t end = command.find_last_not_of(" \t\n\r");
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

    if (choice == 1 && child_stdin_pipe != -1) {
        // User chose Yes
        shell_save_output = true;
        shell_response_start_pos = current_response_raw.size();
        write(child_stdin_pipe, "y\n", 2);
    } else if (child_stdin_pipe != -1) {
        // User chose No
        write(child_stdin_pipe, "n\n", 2);
    }
}

// Pipe reader for streaming output
void pipe_read_cb(int fd, void*) {
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0) {
        current_response_raw.append(buffer, bytes);
        needs_redraw = true;

        // Shell mode: detect y/n prompt
        if (is_shell_mode && !shell_prompt_shown) {
            std::string sanitized = sanitize_output(current_response_raw);
            bool found_prompt = false;
            const char* patterns[] = {"[y/n]", "(y/n)", "[Y/n]", "[y/N]"};
            for (const char* pat : patterns) {
                if (sanitized.find(pat) != std::string::npos) {
                    found_prompt = true;
                    break;
                }
            }
            if (found_prompt) {
                shell_prompt_shown = true;
                show_shell_confirm_dialog();
            }
        }

        // Interactive mode: reset response timeout on each data chunk
        if (interactive_session_active) {
            Fl::remove_timeout(interactive_response_timeout_cb);
            Fl::add_timeout(2.0, interactive_response_timeout_cb);
        }
    } else if (bytes == 0 || (bytes == -1 && errno != EAGAIN && errno != EINTR)) {
        // Process ended
        if (interactive_session_active) {
            interactive_session_active = false;
            is_interactive_mode = false;
        }
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

    // Detect modes from custom args (only if not resuming an interactive session)
    if (!interactive_session_active) {
        is_code_mode = false;
        is_shell_mode = false;
        is_interactive_mode = false;
        std::istringstream iss(tgpt_custom_args);
        std::string arg;
        while (iss >> arg) {
            if (arg == "-c" || arg == "--code") is_code_mode = true;
            if (arg == "-s" || arg == "--shell") is_shell_mode = true;
            if (arg == "-i" || arg == "--interactive") is_interactive_mode = true;
        }
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

    // If interactive session is active, just send prompt to existing process
    if (interactive_session_active && child_stdin_pipe != -1) {
        std::string msg = prompt + "\n";
        write(child_stdin_pipe, msg.c_str(), msg.size());
        return;
    }

    // --- Start new process ---
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        finish_processing(true);
        return;
    }
    
    // Set non-blocking on read end
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    // Create stdin pipe for -s and -i modes
    int stdin_pipefd[2] = {-1, -1};
    if (is_shell_mode || is_interactive_mode) {
        if (pipe(stdin_pipefd) != 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            finish_processing(true);
            return;
        }
    }

    child_pid = fork();
    if (child_pid == 0) {
        // Child process
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (stdin_pipefd[0] != -1) {
            dup2(stdin_pipefd[0], STDIN_FILENO);
            close(stdin_pipefd[0]);
            close(stdin_pipefd[1]);
        }

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
            std::istringstream iss(tgpt_custom_args);
            std::string arg;
            while (iss >> arg) {
                custom_args_storage.push_back(arg);
            }
            for (auto& st : custom_args_storage) {
                args.push_back((char*)st.c_str());
            }
        }

        // For -i mode, don't pass prompt as CLI arg (it goes via stdin)
        if (!is_interactive_mode) {
            args.push_back((char*)prompt.c_str());
        }
        args.push_back(nullptr);

        execvp("tgpt", args.data());
        _exit(127);
    } else if (child_pid > 0) {
        close(pipefd[1]);
        child_pipe = pipefd[0];
        Fl::add_fd(child_pipe, FL_READ, pipe_read_cb);

        if (stdin_pipefd[1] != -1) {
            close(stdin_pipefd[0]);
            child_stdin_pipe = stdin_pipefd[1];
        }

        // For -i mode, send the first prompt via stdin
        if (is_interactive_mode) {
            interactive_session_active = true;
            std::string msg = prompt + "\n";
            write(child_stdin_pipe, msg.c_str(), msg.size());
        }
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        if (stdin_pipefd[0] != -1) {
            close(stdin_pipefd[0]);
            close(stdin_pipefd[1]);
        }
        finish_processing(true);
    }
}

} // namespace tgpt
