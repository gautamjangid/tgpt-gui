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
#include <cstring>

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

struct ChatEntry {
    std::string prompt;
    std::string response;
    bool code_mode = false;
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
extern Fl_Button          *btn_prev;
extern Fl_Button          *btn_next;
extern Fl_Button          *btn_cancel;
extern Fl_Output          *code_counter;

// State Flags & Processing Info
extern bool needs_redraw;
extern std::atomic<bool> processing;
extern pid_t child_pid;
extern int child_pipe;
extern int child_stdin_pipe;
extern int child_pty_master;
extern std::string current_response_raw;

// Mode Flags (detected from custom args per request)
extern bool is_code_mode;
extern bool is_shell_mode;
extern bool is_interactive_mode;
extern bool interactive_session_active;

// Shell mode state
extern bool shell_prompt_shown;
extern bool shell_save_output;
extern size_t shell_response_start_pos;

// Interactive mode state
extern bool current_response_saved;

// Code Block State
extern std::vector<CodeBlock> all_code_blocks;
extern int current_code_idx;

// Chat History State
extern int history_limit;
extern std::deque<ChatEntry> chat_history;
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
