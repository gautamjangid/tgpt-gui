#include "History.hpp"
#include "Globals.hpp"
#include "Utils.hpp"

#include <ctime>
#include <fstream>
#include <pwd.h>
#include <unistd.h>
#include <cstdlib>

namespace tgpt {

std::string get_history_filename_for_today() {
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d-tgpt-chat.html", timeinfo);
    
    std::string filename = std::string(buffer);
    return get_chats_dir() + "/" + filename;
}

std::string get_active_history_filepath() {
    if (current_history_filepath.empty()) {
        current_history_filepath = get_history_filename_for_today();
    }
    return current_history_filepath;
}

void save_history_entry(const std::string& prompt, const std::string& response) {
    std::string filepath = get_active_history_filepath();
    bool file_exists = (access(filepath.c_str(), F_OK) == 0);
    std::ofstream ofs(filepath, std::ios::app);
    if (ofs.is_open()) {
        if (!file_exists) {
            ofs << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'><title>tgpt chat</title></head>\n<body style=\"font-family: sans-serif; background-color: #ffffff;\">\n";
        }
        
        // Hide the raw data inside HTML comments for robust parsing
        ofs << "<!-- RAW_PROMPT_START\n" << prompt << "\nRAW_PROMPT_END -->\n";
        ofs << "<!-- RAW_RESPONSE_START\n" << response << "\nRAW_RESPONSE_END -->\n";
        
        ofs << "<div style='color:#0000aa; margin-bottom:10px;'><b>You:</b><br>\n";
        ofs << escape_html(prompt) << "\n</div>\n";
        
        ofs << "<div style='color:#00aa00; margin-bottom:20px;'><b>tgpt:</b><br>\n";
        ofs << parse_markdown_to_html(response, false) << "\n</div>\n<hr>\n";
    }
}

} // namespace tgpt
