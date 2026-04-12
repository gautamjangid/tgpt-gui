#include "UI.hpp"
#include "Globals.hpp"
#include "Utils.hpp"
#include "History.hpp"

#include <pwd.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace tgpt {

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


void limit_cb(Fl_Widget* w, void* v) {
    history_limit = (int)(intptr_t)v;
    load_history_cb(nullptr, nullptr);
}

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

} // namespace tgpt
