#pragma once

#include <FL/Fl_Widget.H>

namespace tgpt {

void update_ui_code_counter();
void redraw_chat_window();

void load_history_cb(Fl_Widget*, void*);
void load_chat_file_cb(Fl_Widget*, void*);
void close_chat_cb(Fl_Widget*, void*);
void exit_cb(Fl_Widget*, void*);
void settings_cb(Fl_Widget*, void*);
void about_cb(Fl_Widget*, void*);
void updates_cb(Fl_Widget*, void*);

void copy_block_by_index(int idx);
void copy_cb(Fl_Widget*, void*);
void next_code_cb(Fl_Widget*, void*);
void timer_redraw_cb(void*);

void limit_cb(Fl_Widget* w, void* v);
int global_handler(int event);

} // namespace tgpt
