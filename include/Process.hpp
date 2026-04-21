#pragma once

#include <FL/Fl_Widget.H>

namespace tgpt {

void finish_processing(bool cancelled = false);
void pipe_read_cb(int fd, void*);
void cancel_cb(Fl_Widget*, void*);
void send_cb(Fl_Widget*, void*);
void show_shell_confirm_dialog();

} // namespace tgpt
