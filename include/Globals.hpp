#pragma once

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

#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <sys/types.h>

namespace tgpt {

struct CodeBlock {
    std::string lang;
    std::string code;
};

class ChatInput : public Fl_Multiline_Input {
public:
    ChatInput(int X, int Y, int W, int H, const char* L=0) : Fl_Multiline_Input(X,Y,W,H,L) {}
    int handle(int e) override;
};

// UI Components
extern Fl_Window          *main_window;
extern ChatInput          *input_box;
extern Fl_Help_View       *output_box;
extern Fl_Button          *btn_send;
extern Fl_Button          *btn_copy;
extern Fl_Button          *btn_next;
extern Fl_Button          *btn_cancel;
extern Fl_Output          *code_counter;

// State Flags & Processing Info
extern bool needs_redraw;
extern std::atomic<bool> processing;
extern pid_t child_pid;
extern int child_pipe;
extern std::string current_response_raw;

// Code Block State
extern std::vector<CodeBlock> all_code_blocks;
extern int current_code_idx;

// Chat History State
extern int history_limit;
extern std::deque<std::pair<std::string, std::string>> chat_history; // {prompt, response}
extern std::string current_history_filepath;

// Settings State
extern const std::string APP_VERSION;
extern std::string tgpt_provider;
extern std::string tgpt_model;
extern std::string tgpt_api_key;
extern std::string tgpt_custom_args;

void load_settings();
void save_settings();

} // namespace tgpt
