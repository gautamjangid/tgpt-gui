#include "Globals.hpp"
#include "UI.hpp"
#include "Process.hpp"
#include "History.hpp"

#include <FL/Fl.H>

using namespace tgpt;

int main(int argc, char **argv) {
    tgpt::load_settings();

#ifdef _WIN32
    // FLTK bypasses Windows font-linking, so we must pick a font that explicitly 
    // contains Hindi/Devanagari glyphs + English. Nirmala UI is the Windows standard for this.
    Fl::set_font(FL_HELVETICA, "Nirmala UI");
#else
    // On lightweight distros like AntiX, the default "sans" alias might not link to 
    // a Hindi font by default. Fontconfig naturally accepts comma-separated fallbacks!
    Fl::set_font(FL_HELVETICA, "sans,FreeSans,Noto Sans,Lohit Devanagari,Nirmala UI");
#endif

    main_window = new Fl_Window(800, 630, "tgpt Lightweight GUI");
    
    Fl_Menu_Bar *menubar = new Fl_Menu_Bar(0, 0, 800, 25);
    menubar->add("File/Load Chat", 0, load_chat_file_cb);
    menubar->add("File/Close Chat", 0, close_chat_cb);
    menubar->add("File/Exit", 0, exit_cb);
    menubar->add("Settings/tgpt-cli", 0, settings_cb);
    menubar->add("History/Last 5 Chats",  0, limit_cb, (void*)(intptr_t)5);
    menubar->add("History/Last 10 Chats", 0, limit_cb, (void*)(intptr_t)10);
    menubar->add("History/Last 20 Chats", 0, limit_cb, (void*)(intptr_t)20);
    menubar->add("Help/About", 0, about_cb);
    menubar->add("Help/Updates", 0, updates_cb);

    // Output area
    output_box = new Fl_Help_View(10, 35, 780, 420);
    output_box->textfont(FL_HELVETICA);

    // Input area
    input_box = new ChatInput(10, 465, 580, 90);
    input_box->textfont(FL_HELVETICA);
    input_box->wrap(1);
    input_box->callback(send_cb);

    btn_send = new Fl_Button(600, 465, 140, 35, "Send");
    btn_send->callback(send_cb);

    btn_cancel = new Fl_Button(600, 505, 140, 25, "Cancel");
    btn_cancel->callback(cancel_cb);

    Fl_Button *btn_clear = new Fl_Button(600, 535, 140, 25, "Clear Chat");
    btn_clear->callback(close_chat_cb);

    Fl_Output *lbl = new Fl_Output(600, 570, 50, 25, "Block:");
    lbl->box(FL_NO_BOX);
    code_counter = new Fl_Output(650, 570, 80, 25);
    code_counter->value("0");

    btn_prev = new Fl_Button(730, 570, 25, 25, "@<-");
    btn_prev->callback(prev_code_cb);
    btn_prev->tooltip("Previous code block");

    btn_next = new Fl_Button(755, 570, 25, 25, "@->");
    btn_next->callback(next_code_cb);
    btn_next->tooltip("Next code block");

    btn_copy = new Fl_Button(600, 600, 180, 25, "Copy Selected");
    btn_copy->callback(copy_cb);

    main_window->resizable(output_box);
    main_window->end();
    
    Fl::add_handler(global_handler);

    main_window->show(argc, argv);

    return Fl::run();
}
