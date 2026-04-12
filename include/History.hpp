#pragma once

#include <string>

namespace tgpt {

std::string get_history_filename_for_today();
std::string get_active_history_filepath();
void save_history_entry(const std::string& prompt, const std::string& response);

} // namespace tgpt
