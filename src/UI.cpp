#include "UI.hpp"
#include "Globals.hpp"
#include "Utils.hpp"
#include "History.hpp"

#include <pwd.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>

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
    full_html << "<html><body><font face='sans-serif,FreeSans,Noto Sans,Lohit Devanagari,Nirmala UI' size='4'>";
    
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
    
    std::string chats_dir = get_chats_dir();
    fnfc.directory(chats_dir.c_str());

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

struct SettingsDialogData {
    Fl_Window* win;
    Fl_Input_Choice* inp_provider;
    Fl_Input_Choice* inp_model;
    Fl_Input* inp_apikey;
    Fl_Input* inp_args;
};

void settings_save_cb(Fl_Widget*, void* v) {
    SettingsDialogData* data = (SettingsDialogData*)v;
    tgpt_provider = data->inp_provider->value();
    tgpt_model    = data->inp_model->value();
    tgpt_api_key  = data->inp_apikey->value();
    tgpt_custom_args = data->inp_args->value();
    tgpt::save_settings();
    data->win->hide();
    delete data->win;
    delete data;
}

void provider_changed_cb(Fl_Widget*, void* v) {
    SettingsDialogData* data = (SettingsDialogData*)v;
    std::string val = data->inp_provider->value();
    if (val == "phind" || val == "duckduckgo" || val == "koboldai" || val == "blackbox" || val == "perplexity" || val == "ollama") {
        data->inp_model->deactivate();
        data->inp_apikey->deactivate();
    } else {
        data->inp_model->activate();
        data->inp_apikey->activate();
    }
}

void settings_cb(Fl_Widget*, void*) {
    SettingsDialogData* data = new SettingsDialogData();
    data->win = new Fl_Window(400, 310, "tgpt-cli Settings");
    
    data->inp_provider = new Fl_Input_Choice(120, 20, 260, 30, "Provider:");
    data->inp_provider->add("openai");
    data->inp_provider->add("phind");
    data->inp_provider->add("koboldai");
    data->inp_provider->add("ollama");
    data->inp_provider->add("groq");
    data->inp_provider->add("blackbox");
    data->inp_provider->add("duckduckgo");
    data->inp_provider->add("perplexity");
    data->inp_provider->value(tgpt_provider.c_str());
    data->inp_provider->callback(provider_changed_cb, data);
    data->inp_provider->when(FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED);
    
    data->inp_model = new Fl_Input_Choice(120, 60, 260, 30, "Model:");
    data->inp_model->add("gpt-4o");
    data->inp_model->add("claude-3-opus");
    data->inp_model->add("llama-3");
    data->inp_model->add("mixtral-8x7b");
    data->inp_model->value(tgpt_model.c_str());
    
    data->inp_apikey = new Fl_Input(120, 100, 260, 30, "API Key:");
    data->inp_apikey->value(tgpt_api_key.c_str());
    
    data->inp_args = new Fl_Input(120, 140, 260, 30, "Extra Args:");
    data->inp_args->tooltip("Space-separated args (e.g. -i -m)");
    data->inp_args->value(tgpt_custom_args.c_str());
    
    Fl_Help_View* hv = new Fl_Help_View(10, 180, 380, 80);
    hv->value("<font size='3' face='sans-serif'><b>Hint:</b> Free providers like 'duckduckgo' or 'phind' do NOT require an API Key or Model selection. Providers like 'openai' require an API Key. Leave Provider blank for default.</font>");
    hv->box(FL_FLAT_BOX);
    hv->color(data->win->color());
    
    Fl_Button* btn_save = new Fl_Button(150, 270, 100, 30, "Save");
    btn_save->callback(settings_save_cb, data);
    
    provider_changed_cb(data->inp_provider, data); // initialize toggle state

    data->win->end();
    int center_x = main_window->x() + (main_window->w() - 400) / 2;
    int center_y = main_window->y() + (main_window->h() - 310) / 2;
    data->win->position(center_x, center_y);
    data->win->show();
}

static void close_win_cb(Fl_Widget* w, void* v) {
    Fl_Window* win = (Fl_Window*)v;
    win->hide();
    delete win;
}

void about_cb(Fl_Widget*, void*) {
    Fl_Window* win = new Fl_Window(450, 350, "About / License");
    Fl_Help_View* hv = new Fl_Help_View(10, 10, 430, 280);
    hv->value(("<font face='sans-serif'><b>tgpt Lightweight GUI v" + APP_VERSION + "</b><br>"
              "Author: Gautam Jangid<br><br>"
              "<b>MIT License</b><br><br>"
              "Copyright (c) 2024 Gautam Jangid<br><br>"
              "Permission is hereby granted, free of charge, to any person obtaining a copy "
              "of this software and associated documentation files (the \"Software\"), to deal "
              "in the Software without restriction, including without limitation the rights "
              "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
              "copies of the Software, and to permit persons to whom the Software is "
              "furnished to do so, subject to the following conditions:<br><br>"
              "The above copyright notice and this permission notice shall be included in all "
              "copies or substantial portions of the Software.<br><br>"
              "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
              "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
              "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."
              "</font>").c_str());
    Fl_Button* btn = new Fl_Button(185, 300, 80, 30, "OK");
    btn->callback(close_win_cb, win);
    win->end();
    
    int center_x = main_window->x() + (main_window->w() - 450) / 2;
    int center_y = main_window->y() + (main_window->h() - 350) / 2;
    win->position(center_x, center_y);
    win->show();
}

std::string exec_curl(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "error";
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        return "error";
    }
    pclose(pipe);
    return result;
}

void run_updater_cb(Fl_Widget*, void* v) {
    const char* cmd = "x-terminal-emulator -e \"bash -c 'cd /tmp && rm -rf tgpt-gui && git clone -b main https://github.com/gautamjangid/tgpt-gui.git && cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; echo Press Enter to exit; read'\" || "
                      "gnome-terminal -- bash -c 'cd /tmp && rm -rf tgpt-gui && git clone -b main https://github.com/gautamjangid/tgpt-gui.git && cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; echo Press Enter to exit; read' || "
                      "xterm -e \"bash -c 'cd /tmp && rm -rf tgpt-gui && git clone -b main https://github.com/gautamjangid/tgpt-gui.git && cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; echo Press Enter to exit; read'\" &";
    system(cmd);
    Fl_Window* win = (Fl_Window*)v;
    win->hide();
    delete win;
}

void updates_cb(Fl_Widget*, void*) {
    Fl_Window* win = new Fl_Window(500, 300, "Updates");
    Fl_Help_View* hv = new Fl_Help_View(10, 10, 480, 230);
    hv->value("<font face='sans-serif'>Checking for updates... please wait.</font>");
    
    int center_x = main_window->x() + (main_window->w() - 500) / 2;
    int center_y = main_window->y() + (main_window->h() - 300) / 2;
    win->position(center_x, center_y);
    win->show();
    Fl::check(); 
    
    std::string raw = exec_curl("curl -s https://raw.githubusercontent.com/gautamjangid/tgpt-gui/main/src/Globals.cpp | grep 'APP_VERSION ='");
    
    std::string remote_version = "Unknown";
    size_t first = raw.find('"');
    if (first != std::string::npos) {
        size_t second = raw.find('"', first + 1);
        if (second != std::string::npos) {
            remote_version = raw.substr(first + 1, second - first - 1);
        }
    }
    
    std::string html;
    bool needs_update = false;
    
    if (remote_version == "Unknown") {
        html = "<font color='red' face='sans-serif'><b>Failed to fetch latest version info. Please check your internet connection.</b></font><br><br>";
    } else if (remote_version == APP_VERSION) {
        html = "<font color='green' face='sans-serif'><b>Already Up to Date!</b></font><br><br><font face='sans-serif'>Current version: v" + APP_VERSION + "</font>";
    } else {
        html = "<font color='#CC0000' face='sans-serif'><b>New version available: v" + remote_version + "</b></font><br>"
               "<font face='sans-serif'>Current version: v" + APP_VERSION + "</font><br><br>"
               "<font face='sans-serif'>To update, click the 'Update Now' button below, which will download and compile the newest version inside a separate terminal.</font>";
        needs_update = true;
    }
    
    hv->value(html.c_str());
    
    if (needs_update) {
        Fl_Button* btn_upd = new Fl_Button(140, 250, 100, 30, "Update Now");
        btn_upd->callback(run_updater_cb, win);
        Fl_Button* btn_close = new Fl_Button(260, 250, 80, 30, "Cancel");
        btn_close->callback(close_win_cb, win);
        win->add(btn_upd);
        win->add(btn_close);
    } else {
        Fl_Button* btn = new Fl_Button(210, 250, 80, 30, "OK");
        btn->callback(close_win_cb, win);
        win->add(btn);
    }
    
    win->redraw();
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
