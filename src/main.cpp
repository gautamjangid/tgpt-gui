#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Help_View.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>

#include <ctime>

#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <thread>
#include <atomic>
#include <array>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <cctype>

// ----------------------------------------------------------------------
// Global variables
// ----------------------------------------------------------------------
Fl_Window          *main_window = nullptr;
Fl_Multiline_Input *input_box = nullptr;
Fl_Help_View       *output_box = nullptr;
Fl_Button          *btn_send   = nullptr;
Fl_Button          *btn_copy   = nullptr;
Fl_Button          *btn_next   = nullptr;
Fl_Button          *btn_cancel = nullptr;
Fl_Output          *code_counter = nullptr;

bool needs_redraw = false;

std::string sanitize_output(const std::string& raw) {
    std::string out;
    bool in_ansi = false;
    for (size_t i = 0; i < raw.length(); ++i) {
        if (raw[i] == '\033') {
            in_ansi = true;
            if (i + 1 < raw.length() && raw[i+1] == '[') {
                i++;
                while (i + 1 < raw.length() && !std::isalpha(raw[i+1])) i++;
                if (i + 1 < raw.length()) i++;
            }
            in_ansi = false;
            continue;
        }
        if (in_ansi) continue;

        if (raw[i] == '\r') {
            size_t last_nl = out.find_last_of('\n');
            if (last_nl == std::string::npos) out.clear();
            else out.resize(last_nl + 1);
        } else if (raw[i] == '\b') {
            if (!out.empty() && out.back() != '\n') {
                while (!out.empty() && (out.back() & 0xC0) == 0x80) {
                    out.pop_back();
                }
                if (!out.empty()) out.pop_back();
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

struct CodeBlock {
    std::string lang;
    std::string code;
};

std::vector<CodeBlock> all_code_blocks;
int current_code_idx = -1;

std::atomic<bool> processing{false};
pid_t child_pid = 0;
int child_pipe = -1;
std::string current_response_raw;

// Settings
int history_limit = 5;

// Chat History State
// Chat History State
std::deque<std::pair<std::string, std::string>> chat_history; // {prompt, response}
std::string current_history_filepath = "";
void redraw_chat_window();

// ----------------------------------------------------------------------
// History File Management
// ----------------------------------------------------------------------
std::string get_history_filename_for_today() {
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d-tgpt-chat.html", timeinfo);
    
    std::string filename = std::string(buffer);
    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) homedir = pw->pw_dir;
    }
    if (!homedir) return filename; 
    return std::string(homedir) + "/" + filename;
}

std::string get_active_history_filepath() {
    if (current_history_filepath.empty()) {
        current_history_filepath = get_history_filename_for_today();
    }
    return current_history_filepath;
}

// Forward declarations
std::string escape_html(const std::string& data);
std::string parse_markdown_to_html(const std::string& md, bool clear_blocks = true);

void save_history_entry(const std::string& prompt, const std::string& response) {
    std::string filepath = get_active_history_filepath();
    bool file_exists = (access(filepath.c_str(), F_OK) == 0);
    std::ofstream ofs(filepath, std::ios::app);
    if (ofs.is_open()) {
        if (!file_exists) {
            ofs << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'><title>tgpt chat</title></head>\n<body style=\"font-family: sans-serif; background-color: #ffffff;\">\n";
        }
        
        // Hide the raw data inside HTML comments for robust parsing
        ofs << "<!-- RAW_PROMPT_START\n" << prompt << "\nRAW_PROMPT_END -->\n";
        ofs << "<!-- RAW_RESPONSE_START\n" << response << "\nRAW_RESPONSE_END -->\n";
        
        ofs << "<div style='color:#0000aa; margin-bottom:10px;'><b>You:</b><br>\n";
        ofs << escape_html(prompt) << "\n</div>\n";
        
        ofs << "<div style='color:#00aa00; margin-bottom:20px;'><b>tgpt:</b><br>\n";
        ofs << parse_markdown_to_html(response, false) << "\n</div>\n<hr>\n";
    }
}

// ----------------------------------------------------------------------
// HTML escaping
// ----------------------------------------------------------------------
std::string escape_html(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size() * 1.2);
    for (char c : data) {
        switch (c) {
            case '<':  buffer += "&lt;"; break;
            case '>':  buffer += "&gt;"; break;
            case '&':  buffer += "&amp;"; break;
            default:   buffer += c; break;
        }
    }
    return buffer;
}

// ----------------------------------------------------------------------
// Markdown to HTML (extracts code blocks)
// ----------------------------------------------------------------------
std::string parse_markdown_to_html(const std::string& md, bool clear_blocks) {
    if (clear_blocks) {
        all_code_blocks.clear();
    }
    std::ostringstream html;

    std::istringstream stream(md);
    std::string line;
    bool in_code_block = false;
    std::string current_block_text;
    std::string current_lang;

    while (std::getline(stream, line)) {
        if (line.length() >= 3 && line.substr(0, 3) == "```") {
            in_code_block = !in_code_block;
            if (in_code_block) {
                current_block_text.clear();
                current_lang = line.substr(3); // e.g. "python"
                
                size_t first = current_lang.find_first_not_of(" \t\r\n");
                if (std::string::npos == first) {
                    current_lang = "Code";
                } else {
                    size_t last = current_lang.find_last_not_of(" \t\r\n");
                    current_lang = current_lang.substr(first, (last - first + 1));
                }

                html << "<br><table bgcolor='#2d2d2d' width='100%'><tr><td>"
                     << "<b><font color='#a0a0a0'>" << escape_html(current_lang) << "</font></b><br>"
                     << "<pre><font color='#ffffff'>";
            } else {
                if (clear_blocks) {
                    all_code_blocks.push_back({current_lang, current_block_text});
                }
                html << "</font></pre></td></tr></table><br>";
            }
            continue;
        }

        if (in_code_block) {
            current_block_text += line + "\n";
            html << escape_html(line) << "\n";
        } else {
            // Heading formatting
            if (line.compare(0, 2, "# ") == 0) line = "<h2>" + escape_html(line.substr(2)) + "</h2>";
            else if (line.compare(0, 3, "## ") == 0) line = "<h3>" + escape_html(line.substr(3)) + "</h3>";
            else if (line.compare(0, 4, "### ") == 0) line = "<h4>" + escape_html(line.substr(4)) + "</h4>";
            else {
                line = escape_html(line);
            }
            
            // Bold formatting
            size_t pos = 0;
            while ((pos = line.find("**", pos)) != std::string::npos) {
                size_t end = line.find("**", pos + 2);
                if (end != std::string::npos) {
                    line = line.substr(0, pos) + "<b>" + line.substr(pos + 2, end - (pos + 2)) + "</b>" + line.substr(end + 2);
                    pos += 3;
                } else break;
            }

            // Basic layout
            if (line.empty()) html << "<br>";
            else html << line << "<br>";
        }
    }
    
    // Close unclosed tags if streaming
    if (in_code_block) {
        html << "</font></pre></td></tr></table><br>";
        if (clear_blocks) {
            all_code_blocks.push_back({current_lang, current_block_text});
        }
    }

    return html.str();
}

void update_ui_code_counter() {
    if (!all_code_blocks.empty()) {
        current_code_idx = 0;
        static char buf[64];
        snprintf(buf, sizeof(buf), "%s (1/%d)", all_code_blocks[0].lang.c_str(), (int)all_code_blocks.size());
        code_counter->value(buf);
    } else {
        current_code_idx = -1;
        code_counter->value("0");
    }
}

// Update the entire output box based on chat history + current typing response
void redraw_chat_window() {
    int current_top = output_box->topline();
    std::ostringstream full_html;
    // FLTK 1.3 doesn't support CSS styles well, so we use traditional HTML tags
    full_html << "<html><body><font face='sans-serif' size='4'>";
    
    // 1. Current processing msg (if any) is the latest, so it goes at the absolute top
    if (processing && !current_response_raw.empty()) {
        full_html << "<font color='#00aa00'><b>tgpt (typing...):</b></font><br>";
        full_html << parse_markdown_to_html(sanitize_output(current_response_raw), true) << "<br><hr><br>";
    } else if (processing) {
        full_html << "<font color='#00aa00'><b>tgpt:</b> <i>thinking...</i></font><br><br><hr><br>";
    }

    // 2. Chat history in reverse order
    for (auto it = chat_history.rbegin(); it != chat_history.rend(); ++it) {
        const auto& msg = *it;
        
        full_html << "<font color='#0000aa'><b>You:</b></font><br>";
        full_html << escape_html(msg.first) << "<br><br>";
        
        full_html << "<font color='#00aa00'><b>tgpt:</b></font><br>";
        // Do not extract blocks for history renders to avoid messing up the current blocks
        full_html << parse_markdown_to_html(msg.second, false) << "<br>";
        
        // Add separator if it's not the last loaded historical message
        if (std::next(it) != chat_history.rend()) {
            full_html << "<br><hr><br>";
        }
    }

    full_html << "</font></body></html>";
    output_box->value(full_html.str().c_str());
    
    // Attempt to maintain scroll position
    if (processing && current_top > 0) {
        output_box->topline(current_top);
    }
    
    update_ui_code_counter();
}

// Load limited history
void load_history_cb(Fl_Widget*, void*) {
    std::ifstream ifs(get_active_history_filepath());
    chat_history.clear();
    
    if (!ifs.is_open()) {
        redraw_chat_window();
        return;
    }

    std::vector<std::pair<std::string, std::string>> all_entries;
    std::string line, current_prompt, current_response;
    int state = 0; // 0=none, 1=user(txt), 2=tgpt(txt), 3=user(html), 4=tgpt(html)

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "[USER]") {
            if (state == 2 || state == 1) {
                if(state == 2) all_entries.push_back({current_prompt, current_response});
                current_prompt.clear();
                current_response.clear();
            }
            state = 1;
        } else if (line == "[TGPT]") {
            state = 2;
        } else if (line == "<!-- RAW_PROMPT_START") {
            if (state == 4 || state == 3) {
                if(state == 4) all_entries.push_back({current_prompt, current_response});
                current_prompt.clear();
                current_response.clear();
            }
            state = 3;
        } else if (line == "RAW_PROMPT_END -->") {
            state = 0;
        } else if (line == "<!-- RAW_RESPONSE_START") {
            state = 4;
        } else if (line == "RAW_RESPONSE_END -->") {
            all_entries.push_back({current_prompt, current_response});
            current_prompt.clear();
            current_response.clear();
            state = 0;
        } else {
            if (state == 1 || state == 3) {
                if (!current_prompt.empty()) current_prompt += "\n";
                current_prompt += line;
            } else if (state == 2 || state == 4) {
                if (!current_response.empty()) current_response += "\n";
                current_response += line;
            }
        }
    }
    if ((state == 2 || state == 4) && !current_prompt.empty()) {
        all_entries.push_back({current_prompt, current_response});
    }

    // Keep only last N entries based on history_limit
    int start_idx = (int)all_entries.size() > history_limit ? (int)all_entries.size() - history_limit : 0;
    for (int i = start_idx; i < (int)all_entries.size(); ++i) {
        chat_history.push_back(all_entries[i]);
    }
    
    redraw_chat_window();
}

void load_chat_file_cb(Fl_Widget*, void*) {
    Fl_Native_File_Chooser fnfc;
    fnfc.title("Load Chat File");
    fnfc.type(Fl_Native_File_Chooser::BROWSE_FILE);
    fnfc.filter("HTML Files\t*.html\nText Files\t*.txt\nAll Files\t*");
    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) homedir = pw->pw_dir;
    }
    if (homedir) fnfc.directory(homedir);

    if ( fnfc.show() == 0 ) {
        current_history_filepath = fnfc.filename();
        load_history_cb(nullptr, nullptr);
    }
}

void close_chat_cb(Fl_Widget*, void*) {
    chat_history.clear();
    current_history_filepath = get_history_filename_for_today();
    redraw_chat_window();
}

void exit_cb(Fl_Widget*, void*) {
    exit(0);
}

// ----------------------------------------------------------------------
// Copy selected code block
// ----------------------------------------------------------------------
void copy_block_by_index(int idx) {
    if (idx < 0 || idx >= (int)all_code_blocks.size()) return;

    const std::string& code = all_code_blocks[idx].code;

    Fl::copy(code.c_str(), (int)code.size(), 1);

    btn_copy->label("Copied block!");
    Fl::add_timeout(1.5, [](void* v) {
        ((Fl_Button*)v)->label("Copy Selected");
    }, btn_copy);
}

void copy_cb(Fl_Widget*, void*) {
    if (current_code_idx != -1) {
        copy_block_by_index(current_code_idx);
    }
}

// ----------------------------------------------------------------------
// Cycle to next code block
// ----------------------------------------------------------------------
void next_code_cb(Fl_Widget*, void*) {
    if (all_code_blocks.size() < 2) return;
    current_code_idx = (current_code_idx + 1) % all_code_blocks.size();
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s (%d/%d)", all_code_blocks[current_code_idx].lang.c_str(), current_code_idx + 1, (int)all_code_blocks.size());
    code_counter->value(buf);
}

void timer_redraw_cb(void*) {
    if (processing || needs_redraw) {
        if (needs_redraw) redraw_chat_window();
        needs_redraw = false;
        Fl::repeat_timeout(0.15, timer_redraw_cb);
    }
}

// ----------------------------------------------------------------------
// Main Application Logic
// ----------------------------------------------------------------------
void finish_processing(bool cancelled=false) {
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
        execlp("tgpt", "tgpt", prompt.c_str(), (char*)nullptr);
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

// ----------------------------------------------------------------------
// Settings Callbacks
// ----------------------------------------------------------------------
void limit_cb(Fl_Widget* w, void* v) {
    history_limit = (int)(intptr_t)v;
    load_history_cb(nullptr, nullptr);
}

// Global Event Handler for Alt+Number
int global_handler(int event) {
    if (event == FL_KEYBOARD || event == FL_SHORTCUT) {
        int key = Fl::event_key();
        int state = Fl::event_state();
        if ((state & FL_ALT) && key >= '1' && key <= '9') {
            int idx = (key - '1'); // Alt+1 is index 0
            copy_block_by_index(idx);
            return 1; // handled
        }
    }
    return 0;
}

// ----------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------
int main(int argc, char **argv) {
#ifdef _WIN32
    Fl::set_font(FL_HELVETICA, "Segoe UI");
#else
    Fl::set_font(FL_HELVETICA, "Arial");
#endif

    main_window = new Fl_Window(750, 630, "tgpt Lightweight GUI");
    
    Fl_Menu_Bar *menubar = new Fl_Menu_Bar(0, 0, 750, 25);
    menubar->add("File/Load Chat", 0, load_chat_file_cb);
    menubar->add("File/Close Chat", 0, close_chat_cb);
    menubar->add("File/Exit", 0, exit_cb);
    menubar->add("History/Last 5 Chats",  0, limit_cb, (void*)(intptr_t)5);
    menubar->add("History/Last 10 Chats", 0, limit_cb, (void*)(intptr_t)10);
    menubar->add("History/Last 20 Chats", 0, limit_cb, (void*)(intptr_t)20);

    // Output area
    output_box = new Fl_Help_View(10, 35, 730, 420);

    // Input area
    input_box = new Fl_Multiline_Input(10, 465, 580, 90);
    input_box->wrap(1);
    input_box->when(FL_WHEN_ENTER_KEY_ALWAYS);
    input_box->callback([](Fl_Widget*, void*) {
        if (Fl::event_key() == FL_Enter && Fl::event_ctrl())
            send_cb(nullptr, nullptr);
    });

    btn_send = new Fl_Button(600, 465, 140, 35, "Send");
    btn_send->callback(send_cb);

    btn_cancel = new Fl_Button(600, 505, 140, 25, "Cancel");
    btn_cancel->callback(cancel_cb);

    Fl_Button *btn_clear = new Fl_Button(600, 535, 140, 25, "Clear Chat");
    btn_clear->callback(close_chat_cb);

    Fl_Output *lbl = new Fl_Output(600, 570, 50, 25, "Block:");
    lbl->box(FL_NO_BOX);
    code_counter = new Fl_Output(650, 570, 100, 25);
    code_counter->value("0");

    btn_next = new Fl_Button(755, 570, 25, 25, "@->");
    btn_next->callback(next_code_cb);
    btn_next->tooltip("Next code block");

    btn_copy = new Fl_Button(600, 600, 140, 25, "Copy Selected");
    btn_copy->callback(copy_cb);

    main_window->resizable(output_box);
    main_window->end();
    
    Fl::add_handler(global_handler);

    main_window->show(argc, argv);

    return Fl::run();
}
