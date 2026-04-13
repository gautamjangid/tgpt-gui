#include "Process.hpp"
#include "Globals.hpp"
#include "Utils.hpp"
#include "History.hpp"
#include "UI.hpp" // for redraw_chat_window, timer_redraw_cb

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <vector>
#include <sstream>

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
        child_pid = 0; // waitpid reaps the defunct child
    }
    
    std::string final_response = cancelled ? sanitize_output(current_response_raw) + "\n*(Cancelled)*" : sanitize_output(current_response_raw);
    if (!chat_history.empty()) {
        chat_history.back().second = final_response;
        save_history_entry(chat_history.back().first, final_response);
    }

    // Force one last redraw to fix block extraction
    redraw_chat_window();
}

void cancel_cb(Fl_Widget*, void*) {
    if (processing) {
        finish_processing(true);
    }
}

// Pipe reader for streaming output
void pipe_read_cb(int fd, void*) {
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0) {
        current_response_raw.append(buffer, bytes);
        needs_redraw = true;
    } else if (bytes == 0 || (bytes == -1 && errno != EAGAIN && errno != EINTR)) {
        finish_processing(false);
    }
}

void send_cb(Fl_Widget*, void*) {
    if (processing) return;

    std::string prompt = input_box->value();
    if (prompt.empty()) return;

    // Manage history size
    if ((int)chat_history.size() >= history_limit) {
        // Keep elements within bounds before new insertion
        if (chat_history.size() > 0) chat_history.pop_front();
    }
    chat_history.push_back({prompt, ""});

    input_box->value("");
    current_response_raw.clear();
    processing = true;
    btn_send->deactivate();

    redraw_chat_window();
    Fl::check();

    needs_redraw = true;
    Fl::add_timeout(0.15, timer_redraw_cb);

    // Start process
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        finish_processing(true);
        return;
    }
    
    // Set non-blocking on read end
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    child_pid = fork();
    if (child_pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

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

        args.push_back((char*)prompt.c_str());
        args.push_back(nullptr);

        execvp("tgpt", args.data());
        _exit(127);
    } else if (child_pid > 0) {
        close(pipefd[1]);
        child_pipe = pipefd[0];
        Fl::add_fd(child_pipe, FL_READ, pipe_read_cb);
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        finish_processing(true);
    }
}

} // namespace tgpt
