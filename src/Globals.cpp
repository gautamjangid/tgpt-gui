#include "Globals.hpp"
#include "Utils.hpp"
#include <fstream>

namespace tgpt {

int ChatInput::handle(int e) {
    if (e == FL_KEYBOARD && Fl::event_key() == FL_Enter) {
        if (Fl::event_state() & FL_SHIFT) {
            return Fl_Multiline_Input::handle(e);
        } else {
            this->do_callback();
            return 1;
        }
    }
    return Fl_Multiline_Input::handle(e);
}

// UI Components
Fl_Window          *main_window = nullptr;
ChatInput          *input_box = nullptr;
Fl_Help_View       *output_box = nullptr;
Fl_Button          *btn_send   = nullptr;
Fl_Button          *btn_copy   = nullptr;
Fl_Button          *btn_next   = nullptr;
Fl_Button          *btn_cancel = nullptr;
Fl_Output          *code_counter = nullptr;

// State Flags & Processing Info
bool needs_redraw = false;
std::atomic<bool> processing{false};
pid_t child_pid = 0;
int child_pipe = -1;
std::string current_response_raw;

// Code Block State
std::vector<CodeBlock> all_code_blocks;
int current_code_idx = -1;

// Chat History State
int history_limit = 5;
std::deque<std::pair<std::string, std::string>> chat_history;
std::string current_history_filepath = "";

// Settings State
std::string tgpt_provider = "";
std::string tgpt_model = "";
std::string tgpt_api_key = "";
std::string tgpt_custom_args = "";

void load_settings() {
    std::string filepath = get_settings_dir() + "/config.txt";
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return;
    
    std::string line;
    while (std::getline(ifs, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (key == "provider") tgpt_provider = value;
            else if (key == "model") tgpt_model = value;
            else if (key == "api_key") tgpt_api_key = value;
            else if (key == "custom_args") tgpt_custom_args = value;
        }
    }
}

void save_settings() {
    std::string filepath = get_settings_dir() + "/config.txt";
    std::ofstream ofs(filepath);
    if (ofs.is_open()) {
        ofs << "provider=" << tgpt_provider << "\n";
        ofs << "model=" << tgpt_model << "\n";
        ofs << "api_key=" << tgpt_api_key << "\n";
        ofs << "custom_args=" << tgpt_custom_args << "\n";
    }
}

} // namespace tgpt
