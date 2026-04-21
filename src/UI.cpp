#include "UI.hpp"
#include "Globals.hpp"
#include "Utils.hpp"
#include "History.hpp"
#include "Process.hpp"

#include <pwd.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>

namespace tgpt {

// ─── Dialog tracking ────────────────────────────────────────────────────────
// At most one "settings" window and one "secondary" (about/license/updates)
// dialog is allowed open simultaneously.
static Fl_Window* g_settings_win  = nullptr;
static Fl_Window* g_secondary_win = nullptr;

static void close_secondary_dialog() {
    if (g_secondary_win) {
        g_secondary_win->hide();
        delete g_secondary_win;
        g_secondary_win = nullptr;
    }
    // Also close settings dialog if open
    if (g_settings_win) {
        g_settings_win->hide();
        // Note: g_settings_win is deleted in settings_save_cb or window close callback
    }
}

// OK / Close callback for all secondary dialogs — also clears g_secondary_win
static void secondary_close_cb(Fl_Widget*, void* v) {
    Fl_Window* win = (Fl_Window*)v;
    if (g_secondary_win == win) g_secondary_win = nullptr;
    win->hide();
    delete win;
}

// ─── Code-counter helper ────────────────────────────────────────────────────

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

// ─── Right-click handler (Copy All) ────────────────────────────────────────

static int help_view_handler(int ev) {
    if (ev == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
        if (Fl::event_inside(output_box)) {
            Fl_Menu_Item rclick_menu[] = {
                {"&Copy All", 0, nullptr, nullptr, 0},
                {nullptr}
            };
            const Fl_Menu_Item* picked = rclick_menu->popup(Fl::event_x_root(), Fl::event_y_root());
            if (picked && strcmp(picked->label(), "&Copy All") == 0) {
                std::string copy_text;
                for (const auto& msg : chat_history) {
                    copy_text += "You:\n" + msg.prompt + "\n\n";
                    copy_text += "tgpt:\n" + msg.response + "\n\n";
                }
                if (processing && !current_response_raw.empty()) {
                    copy_text += "tgpt:\n" + sanitize_output(current_response_raw) + "\n\n";
                }
                Fl::focus(main_window);
                static std::string safe_clip_buffer;
                safe_clip_buffer = copy_text;
                Fl::copy(safe_clip_buffer.c_str(), (int)safe_clip_buffer.length(), 1);
            }
            return 1;
        }
    }
    return 0;
}

// ─── Chat window rendering ──────────────────────────────────────────────────

void redraw_chat_window() {
    static bool handler_added = false;
    if (!handler_added) {
        Fl::add_handler(help_view_handler);
        handler_added = true;
    }
    int current_top = output_box->topline();
    std::ostringstream full_html;
    full_html << "<html><body><font face='sans-serif' size='4'>";

    if (processing && !current_response_raw.empty()) {
        full_html << "<font color='#00aa00'><b>tgpt (typing...):</b></font><br>";
        if (is_code_mode) {
            all_code_blocks.clear();
            all_code_blocks.push_back({"Code", sanitize_output(current_response_raw)});
            full_html << render_raw_code_html(sanitize_output(current_response_raw)) << "<br><hr><br>";
        } else {
            full_html << parse_markdown_to_html(sanitize_output(current_response_raw), true) << "<br><hr><br>";
        }
    } else if (processing) {
        full_html << "<font color='#00aa00'><b>tgpt:</b> <i>thinking...</i></font><br><br><hr><br>";
    }

    for (auto it = chat_history.rbegin(); it != chat_history.rend(); ++it) {
        const auto& msg = *it;
        bool is_latest = (!processing && it == chat_history.rbegin());
        if (is_latest && msg.code_mode) {
            all_code_blocks.clear();
            all_code_blocks.push_back({"Code", msg.response});
        }

        full_html << "<font color='#0000aa'><b>You:</b></font><br>";
        full_html << escape_html(msg.prompt) << "<br><br>";
        full_html << "<font color='#00aa00'><b>tgpt:</b></font><br>";
        if (msg.code_mode) {
            full_html << render_raw_code_html(msg.response) << "<br>";
        } else {
            full_html << parse_markdown_to_html(msg.response, is_latest) << "<br>";
        }
        if (std::next(it) != chat_history.rend()) {
            full_html << "<br><hr><br>";
        }
    }

    full_html << "</font></body></html>";
    output_box->value(full_html.str().c_str());

    if (processing && current_top > 0) {
        output_box->topline(current_top);
    }
    update_ui_code_counter();
}

// ─── History callbacks ──────────────────────────────────────────────────────

void load_history_cb(Fl_Widget*, void*) {
    std::ifstream ifs(get_active_history_filepath());
    chat_history.clear();

    if (!ifs.is_open()) {
        redraw_chat_window();
        return;
    }

    std::vector<ChatEntry> all_entries;
    std::string line, current_prompt, current_response;
    bool current_code_mode = false;
    int state = 0;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "[USER]") {
            if (state == 2 || state == 1) {
                if (state == 2) {
                    ChatEntry e; e.prompt = current_prompt; e.response = current_response; e.code_mode = current_code_mode;
                    all_entries.push_back(e);
                }
                current_prompt.clear(); current_response.clear(); current_code_mode = false;
            }
            state = 1;
        } else if (line == "[TGPT]") {
            state = 2;
        } else if (line == "<!-- RAW_PROMPT_START") {
            if (state == 4 || state == 3) {
                if (state == 4) {
                    ChatEntry e; e.prompt = current_prompt; e.response = current_response; e.code_mode = current_code_mode;
                    all_entries.push_back(e);
                }
                current_prompt.clear(); current_response.clear(); current_code_mode = false;
            }
            state = 3;
        } else if (line == "RAW_PROMPT_END -->") {
            state = 0;
        } else if (line == "<!-- MODE:code -->") {
            current_code_mode = true;
        } else if (line == "<!-- RAW_RESPONSE_START") {
            state = 4;
        } else if (line == "RAW_RESPONSE_END -->") {
            ChatEntry e; e.prompt = current_prompt; e.response = current_response; e.code_mode = current_code_mode;
            all_entries.push_back(e);
            current_prompt.clear(); current_response.clear(); current_code_mode = false;
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
        ChatEntry e; e.prompt = current_prompt; e.response = current_response; e.code_mode = current_code_mode;
        all_entries.push_back(e);
    }

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
    fnfc.directory(get_chats_dir().c_str());
    if (fnfc.show() == 0) {
        current_history_filepath = fnfc.filename();
        load_history_cb(nullptr, nullptr);
    }
}

void close_chat_cb(Fl_Widget*, void*) {
    if (processing) {
        finish_processing(true);
    }
    chat_history.clear();
    current_history_filepath = get_history_filename_for_today();
    redraw_chat_window();
}

void exit_cb(Fl_Widget*, void*) {
    exit(0);
}

// ─── Settings dialog ────────────────────────────────────────────────────────

struct SettingsDialogData {
    Fl_Window*       win;
    Fl_Input_Choice* inp_provider;
    Fl_Input_Choice* inp_model;
    Fl_Input*        inp_apikey;
    Fl_Input*        inp_args;
};

// Callback for when settings window is closed (not saved)
static void settings_close_cb(Fl_Widget*, void* v) {
    SettingsDialogData* data = (SettingsDialogData*)v;
    g_settings_win = nullptr;   // clear tracker before deleting
    data->win->hide();
    delete data->win;
    delete data;
}

void settings_save_cb(Fl_Widget*, void* v) {
    SettingsDialogData* data = (SettingsDialogData*)v;
    tgpt_provider    = data->inp_provider->value();
    tgpt_model       = data->inp_model->value();
    tgpt_api_key     = data->inp_apikey->value();
    tgpt_custom_args = data->inp_args->value();
    tgpt::save_settings();
    g_settings_win = nullptr;   // clear tracker before deleting
    data->win->hide();
    delete data->win;
    delete data;
}

void provider_changed_cb(Fl_Widget*, void* v) {
    SettingsDialogData* data = (SettingsDialogData*)v;
    std::string val = data->inp_provider->value();
    if (val == "koboldai" || val == "pollinations" || val == "sky") {
        data->inp_model->deactivate();
        data->inp_apikey->deactivate();
    } else {
        data->inp_model->activate();
        data->inp_apikey->activate();
    }
}

void settings_cb(Fl_Widget*, void*) {
    // Close any open secondary dialog (about/license/updates) first
    close_secondary_dialog();
    
    // If settings is already open, just bring it to front
    if (g_settings_win) {
        g_settings_win->show();
        return;
    }

    SettingsDialogData* data = new SettingsDialogData();
    data->win = new Fl_Window(420, 390, "tgpt-cli Settings");
    data->win->callback(settings_close_cb, data);
    g_settings_win = data->win;

    data->inp_provider = new Fl_Input_Choice(120, 20, 280, 30, "Provider:");
    data->inp_provider->add("sky");
    data->inp_provider->add("pollinations");
    data->inp_provider->add("koboldai");
    data->inp_provider->add("openai");
    data->inp_provider->add("gemini");
    data->inp_provider->add("ollama");
    data->inp_provider->add("groq");
    data->inp_provider->add("deepseek");
    data->inp_provider->add("duckduckgo");
    data->inp_provider->add("blackbox");
    data->inp_provider->add("perplexity");
    data->inp_provider->value(tgpt_provider.c_str());
    data->inp_provider->callback(provider_changed_cb, data);
    data->inp_provider->when(FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED);

    data->inp_model = new Fl_Input_Choice(120, 60, 280, 30, "Model:");
    data->inp_model->add("gpt-4o");
    data->inp_model->add("claude-3-opus");
    data->inp_model->add("llama-3");
    data->inp_model->add("mixtral-8x7b");
    data->inp_model->value(tgpt_model.c_str());

    data->inp_apikey = new Fl_Input(120, 100, 280, 30, "API Key:");
    data->inp_apikey->value(tgpt_api_key.c_str());

    data->inp_args = new Fl_Input(120, 140, 280, 30, "Extra Args:");
    data->inp_args->tooltip("Space-separated flags: -c (code), -s (shell), -f (search), -q (quiet)");
    data->inp_args->value(tgpt_custom_args.c_str());

    // Expanded hint — now includes Extra Args mode descriptions
    Fl_Help_View* hv = new Fl_Help_View(10, 180, 400, 170);
    hv->value(
        "<font size='3' face='sans-serif'>"
        "<b>Provider Hint:</b> Free providers (phind/default, koboldai, pollinations, sky, "
        "duckduckgo) need no API Key. Premium providers (openai, gemini, groq, deepseek) "
        "require an API Key. Leave blank to use the default provider.<br><br>"
        "<b>Extra Args modes:</b><br>"
        "&bull; <b>-c</b> &mdash; Code Mode: output in formatted code block<br>"
        "&bull; <b>-s</b> &mdash; Shell Mode: suggests commands with Yes/No dialog<br>"
        "&bull; <b>-f</b> &mdash; Search Mode: web search via Google API<br>"
        "&nbsp;&nbsp;&nbsp;<i>(requires env: TGPT_GOOGLE_API_KEY + TGPT_GOOGLE_SEARCH_ENGINE_ID)</i><br>"
        "&bull; <b>-q</b> &mdash; Quiet Mode: suppresses spinners and progress output<br><br>"
        "<b>Unsupported modes:</b><br>"
        "&bull; <b>-i, -is</b> &mdash; Interactive modes use bubbletea's raw PTY which is "
        "incompatible with GUI process piping and are not supported."
        "</font>"
    );
    hv->box(FL_FLAT_BOX);
    hv->color(data->win->color());

    Fl_Button* btn_save = new Fl_Button(160, 355, 100, 28, "Save");
    btn_save->callback(settings_save_cb, data);

    provider_changed_cb(data->inp_provider, data);


    data->win->end();

    int cx = main_window->x() + (main_window->w() - 420) / 2;

    int cy = main_window->y() + (main_window->h() - 390) / 2;

    data->win->position(cx, cy);

    data->win->show();

}


// ─── About dialog ───────────────────────────────────────────────────────────

void about_cb(Fl_Widget*, void*) {
    // Close any open dialogs first
    close_secondary_dialog();
    if (g_settings_win) {
        g_settings_win->hide();
    }

    Fl_Window* win = new Fl_Window(490, 350, "About tgpt GUI");
    g_secondary_win = win;

    Fl_Help_View* hv = new Fl_Help_View(10, 10, 470, 300);
    std::string html =
        "<font face='sans-serif'><center>"
        "<b>tgpt Lightweight GUI &nbsp;v" + APP_VERSION + "</b><br>"
        "<font size='3'>A high-performance, low-resource graphical interface for the <b>tgpt</b> AI terminal tool.<br>"
        "Optimized for legacy hardware and minimal RAM environments.</font><br><br>"
        "<font size='3'><b>Author:</b> Gautam Jangid &nbsp;|&nbsp; <b>License:</b> MIT</font><br><br>"
        "</center>"
        "<b>Features</b><br>"
        "&bull; Real-time streaming output (typing effect)<br>"
        "&bull; Markdown + code-block rendering with copy<br>"
        "&bull; Chat history saved to disk, loaded on demand<br>"
        "&bull; Shell command confirmation dialog<br>"
        "&bull; Clipboard integration (Copy Text &amp; Copy Block)<br>"
        "&bull; Low memory footprint (&lt;30 MB typical)<br><br>"
        "<b>Supported CLI Modes</b> &mdash; set in <i>Settings &rarr; Extra Args</i><br>"
        "&bull; <b>-c</b> &nbsp;Code Mode &mdash; output in formatted, copyable code block<br>"
        "&bull; <b>-s</b> &nbsp;Shell Mode &mdash; suggests shell commands with Yes/No confirmation<br>"
        "&bull; <b>-f</b> &nbsp;Search Mode &mdash; web search via Google Custom Search API<br>"
        "&nbsp;&nbsp;&nbsp;<font size='3'><i>Needs env vars: TGPT_GOOGLE_API_KEY &amp; TGPT_GOOGLE_SEARCH_ENGINE_ID<br>"
        "Add to ~/.bashrc: export TGPT_GOOGLE_API_KEY=... and restart the app.</i></font><br>"
        "&bull; <b>-q</b> &nbsp;Quiet Mode &mdash; suppresses progress spinners<br>"
        "<font size='3'><i>Note: Interactive terminal modes (-i, -is) use bubbletea's raw PTY<br>"
        "which is incompatible with GUI process piping and are not supported.</i></font>"
        "</font>";
    hv->value(html.c_str());

    Fl_Button* btn = new Fl_Button(205, 315, 80, 28, "OK");
    btn->callback(secondary_close_cb, win);

    win->end();
    int cx = main_window->x() + (main_window->w() - 490) / 2;
    int cy = main_window->y() + (main_window->h() - 350) / 2;
    win->position(cx, cy);
    win->show();
}

// ─── License dialog ─────────────────────────────────────────────────────────

void license_cb(Fl_Widget*, void*) {
    // Close any open dialogs first
    close_secondary_dialog();
    if (g_settings_win) {
        g_settings_win->hide();
    }

    Fl_Window* win = new Fl_Window(460, 400, "License");
    g_secondary_win = win;

    Fl_Help_View* hv = new Fl_Help_View(10, 10, 440, 340);
    hv->value(
        "<font face='sans-serif'><center><b>MIT License</b><br><br></center>"
        "<div>"
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
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
        "SOFTWARE."
        "</div></font>"
    );

    Fl_Button* btn = new Fl_Button(190, 362, 80, 28, "OK");
    btn->callback(secondary_close_cb, win);

    win->end();
    int cx = main_window->x() + (main_window->w() - 460) / 2;
    int cy = main_window->y() + (main_window->h() - 400) / 2;
    win->position(cx, cy);
    win->show();
}

// ─── Updates dialog ─────────────────────────────────────────────────────────

std::string exec_curl(const char* cmd) {
    char buffer[128];
    std::string result;
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

// Fetch GUI latest version from GitHub
static std::string fetch_remote_gui_version() {
    std::string raw = exec_curl(
        "curl -s https://raw.githubusercontent.com/gautamjangid/tgpt-gui/main/src/Globals.cpp"
        " | grep 'APP_VERSION ='");
    size_t f = raw.find('"');
    if (f == std::string::npos) return "Unknown";
    size_t s = raw.find('"', f + 1);
    if (s == std::string::npos) return "Unknown";
    return raw.substr(f + 1, s - f - 1);
}

// Fetch local tgpt version
static std::string fetch_local_tgpt_version() {
    std::string v = exec_curl("tgpt -v 2>&1");
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' '))
        v.pop_back();
    return v.empty() || v == "error" ? "Not installed / not found" : v;
}

// Fetch latest tgpt version from GitHub API
static std::string fetch_remote_tgpt_version() {
    std::string raw = exec_curl(
        "curl -s https://api.github.com/repos/aandrew-me/tgpt/releases/latest"
        " | grep '\"tag_name\"' | head -1");
    // Format: "tag_name": "v2.7.5",
    size_t p = raw.find("tag_name");
    if (p == std::string::npos) return "Unknown";
    size_t q1 = raw.find('"', p + 9);   // skip past: tag_name": "
    if (q1 == std::string::npos) return "Unknown";
    size_t q2 = raw.find('"', q1 + 1);
    if (q2 == std::string::npos) return "Unknown";
    return raw.substr(q1 + 1, q2 - q1 - 1);
}

void run_gui_updater_cb(Fl_Widget*, void* v) {
    const char* cmd =
        "x-terminal-emulator -e \"bash -c 'cd /tmp && rm -rf tgpt-gui && "
        "git clone -b main https://github.com/gautamjangid/tgpt-gui.git && "
        "cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; "
        "echo Press Enter to exit; read'\" || "
        "gnome-terminal -- bash -c 'cd /tmp && rm -rf tgpt-gui && "
        "git clone -b main https://github.com/gautamjangid/tgpt-gui.git && "
        "cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; "
        "echo Press Enter to exit; read' || "
        "xterm -e \"bash -c 'cd /tmp && rm -rf tgpt-gui && "
        "git clone -b main https://github.com/gautamjangid/tgpt-gui.git && "
        "cd tgpt-gui/scripts && chmod +x install.sh && ./install.sh; "
        "echo Press Enter to exit; read'\" &";
    system(cmd);
    Fl_Window* win = (Fl_Window*)v;
    if (g_secondary_win == win) g_secondary_win = nullptr;
    win->hide();
    delete win;
}

void run_tgpt_updater_cb(Fl_Widget*, void*) {
    // Downloads and installs the latest tgpt CLI in a visible terminal
    const char* cmd =
        "x-terminal-emulator -e \"bash -c 'curl -sSL "
        "https://raw.githubusercontent.com/aandrew-me/tgpt/main/install | bash; "
        "echo; echo tgpt update complete! Press Enter to close.; read'\" || "
        "gnome-terminal -- bash -c 'curl -sSL "
        "https://raw.githubusercontent.com/aandrew-me/tgpt/main/install | bash; "
        "echo; echo tgpt update complete! Press Enter to close.; read' || "
        "xterm -e \"bash -c 'curl -sSL "
        "https://raw.githubusercontent.com/aandrew-me/tgpt/main/install | bash; "
        "echo; echo tgpt update complete! Press Enter to close.; read'\" &";
    system(cmd);
    // Keep dialog open so user can dismiss it manually
}

void updates_cb(Fl_Widget*, void*) {
    // Close any open dialogs first
    close_secondary_dialog();
    if (g_settings_win) {
        g_settings_win->hide();
    }

    Fl_Window* win = new Fl_Window(530, 390, "Updates");
    g_secondary_win = win;

    Fl_Help_View* hv = new Fl_Help_View(10, 10, 510, 290);
    hv->value("<font face='sans-serif'>Checking for updates&hellip; Please wait.</font>");

    int cx = main_window->x() + (main_window->w() - 530) / 2;
    int cy = main_window->y() + (main_window->h() - 390) / 2;
    win->position(cx, cy);
    win->show();
    Fl::check();   // Show the "please wait" text before blocking fetches

    // --- Fetch versions ---
    std::string remote_gui  = fetch_remote_gui_version();
    std::string local_tgpt  = fetch_local_tgpt_version();
    std::string remote_tgpt = fetch_remote_tgpt_version();

    // --- Build HTML ---
    bool gui_needs_update  = false;
    bool tgpt_needs_update = false;
    std::string html = "<font face='sans-serif'>";

    // GUI section
    html += "<b>tgpt GUI</b><br>";
    html += "Installed: v" + APP_VERSION + "<br>";
    if (remote_gui == "Unknown") {
        html += "<font color='gray'>Could not fetch latest GUI version (check internet)</font><br>";
    } else if (remote_gui == APP_VERSION) {
        html += "<font color='green'>&check; Up to date (v" + APP_VERSION + ")</font><br>";
    } else {
        html += "<font color='#CC0000'><b>Update available: v" + remote_gui + "</b></font><br>";
        gui_needs_update = true;
    }

    html += "<br><b>tgpt CLI</b><br>";
    html += "Installed: " + local_tgpt + "<br>";
    if (remote_tgpt == "Unknown") {
        html += "<font color='gray'>Could not fetch latest tgpt version (check internet)</font><br>";
    } else {
        // local_tgpt may include the version number somewhere in the string
        std::string local_num = local_tgpt;
        // strip leading "tgpt " if present
        if (local_num.substr(0, 5) == "tgpt ") local_num = local_num.substr(5);
        // remote_tgpt might be "v2.7.5", compare after stripping leading 'v'
        std::string remote_num = remote_tgpt;
        if (!remote_num.empty() && remote_num[0] == 'v') remote_num = remote_num.substr(1);

        html += "Latest: " + remote_tgpt + "<br>";
        if (local_num == remote_num || local_num.find(remote_num) != std::string::npos) {
            html += "<font color='green'>&check; Up to date</font><br>";
        } else {
            html += "<font color='#CC0000'><b>Update available</b></font><br>";
            tgpt_needs_update = true;
        }
    }

    html += "</font>";
    hv->value(html.c_str());

    // --- Buttons (always show tgpt update + close; only add GUI update if needed) ---
    int btn_y = 315;
    int btn_x = 10;

    if (gui_needs_update) {
        Fl_Button* b1 = new Fl_Button(btn_x, btn_y, 140, 28, "Update GUI");
        b1->callback(run_gui_updater_cb, win);
        win->add(b1);
        btn_x += 150;
    }

    Fl_Button* b2 = new Fl_Button(btn_x, btn_y, 155, 28, "Update tgpt CLI");
    b2->callback(run_tgpt_updater_cb, nullptr);
    win->add(b2);

    Fl_Button* b3 = new Fl_Button(430, btn_y, 80, 28, "Close");
    b3->callback(secondary_close_cb, win);
    win->add(b3);

    win->redraw();
}

// ─── Code block copy ────────────────────────────────────────────────────────

void copy_block_by_index(int idx) {
    if (idx < 0 || idx >= (int)all_code_blocks.size()) return;
    const std::string& code = all_code_blocks[idx].code;
    Fl::focus(main_window);
    Fl::copy(code.c_str(), (int)code.size(), 1);
    btn_copy->label("Copied block!");
    Fl::add_timeout(1.5, [](void* v) {
        ((Fl_Button*)v)->label("Copy Block");
    }, btn_copy);
}

class DummyPaster : public Fl_Box {
public:
    DummyPaster() : Fl_Box(0,0,0,0) {}
    int handle(int e) override {
        if (e == FL_PASTE && Fl::event_length() > 0) {
            Fl::copy(Fl::event_text(), Fl::event_length(), 1);
            return 1;
        }
        return 0;
    }
};
static DummyPaster* dummy_paster = nullptr;

void copy_text_cb(Fl_Widget* w, void*) {
    if (!dummy_paster) dummy_paster = new DummyPaster();
    Fl::paste(*dummy_paster, 0);
    Fl_Button* btn = (Fl_Button*)w;
    btn->label("Copied!");
    Fl::add_timeout(1.5, [](void* v) { ((Fl_Button*)v)->label("Copy Text"); }, btn);
}

void copy_cb(Fl_Widget*, void*) {
    if (current_code_idx != -1) {
        copy_block_by_index(current_code_idx);
    }
}

void prev_code_cb(Fl_Widget*, void*) {
    if (all_code_blocks.size() < 2) return;
    current_code_idx = (current_code_idx - 1 + all_code_blocks.size()) % all_code_blocks.size();
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s (%d/%d)",
             all_code_blocks[current_code_idx].lang.c_str(),
             current_code_idx + 1, (int)all_code_blocks.size());
    code_counter->value(buf);
}

void next_code_cb(Fl_Widget*, void*) {
    if (all_code_blocks.size() < 2) return;
    current_code_idx = (current_code_idx + 1) % all_code_blocks.size();
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s (%d/%d)",
             all_code_blocks[current_code_idx].lang.c_str(),
             current_code_idx + 1, (int)all_code_blocks.size());
    code_counter->value(buf);
}

// ─── Timer & keyboard handler ────────────────────────────────────────────────

void timer_redraw_cb(void*) {
    if (processing || needs_redraw) {
        if (needs_redraw) redraw_chat_window();
        needs_redraw = false;
        Fl::repeat_timeout(0.15, timer_redraw_cb);
    }
}

void limit_cb(Fl_Widget*, void* v) {
    history_limit = (int)(intptr_t)v;
    load_history_cb(nullptr, nullptr);
}

int global_handler(int event) {
    if (event == FL_KEYBOARD || event == FL_SHORTCUT) {
        int key   = Fl::event_key();
        int state = Fl::event_state();
        if ((state & FL_ALT) && key >= '1' && key <= '9') {
            copy_block_by_index(key - '1');
            return 1;
        }
    }
    return 0;
}

} // namespace tgpt
