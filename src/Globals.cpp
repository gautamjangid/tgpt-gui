#include "Globals.hpp"

namespace tgpt {

// UI Components
Fl_Window          *main_window = nullptr;
Fl_Multiline_Input *input_box = nullptr;
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

} // namespace tgpt
