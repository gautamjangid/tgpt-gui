#include "Utils.hpp"
#include "Globals.hpp"

#include <cctype>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <cstdlib>

namespace tgpt {

void ensure_directory_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
#ifdef _WIN32
        mkdir(path.c_str());
#else
        mkdir(path.c_str(), 0700);
#endif
    }
}

std::string get_app_dir() {
    const char *homedir = getenv("HOME");
    if (!homedir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) homedir = pw->pw_dir;
    }
    if (!homedir) return ".tgpt-gui"; // Fallback to local
    
    std::string dir = std::string(homedir) + "/.tgpt-gui";
    ensure_directory_exists(dir);
    return dir;
}

std::string get_chats_dir() {
    std::string dir = get_app_dir() + "/chats";
    ensure_directory_exists(dir);
    return dir;
}

std::string get_settings_dir() {
    std::string dir = get_app_dir() + "/settings";
    ensure_directory_exists(dir);
    return dir;
}

std::string sanitize_output(const std::string& raw) {
    std::string out;
    bool in_ansi = false;
    for (size_t i = 0; i < raw.length(); ++i) {
        if (raw[i] == '\033') {
            in_ansi = true;
            if (i + 1 < raw.length() && raw[i+1] == '[') {
                i++;
                while (i + 1 < raw.length() && !std::isalpha(raw[i+1])) i++;
                if (i + 1 < raw.length()) i++;
            }
            in_ansi = false;
            continue;
        }
        if (in_ansi) continue;

        if (raw[i] == '\r') {
            size_t last_nl = out.find_last_of('\n');
            if (last_nl == std::string::npos) out.clear();
            else out.resize(last_nl + 1);
        } else if (raw[i] == '\b') {
            if (!out.empty() && out.back() != '\n') {
                while (!out.empty() && (out.back() & 0xC0) == 0x80) {
                    out.pop_back();
                }
                if (!out.empty()) out.pop_back();
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

std::string escape_html(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size() * 1.2);
    for (char c : data) {
        switch (c) {
            case '<':  buffer += "&lt;"; break;
            case '>':  buffer += "&gt;"; break;
            case '&':  buffer += "&amp;"; break;
            default:   buffer += c; break;
        }
    }
    return buffer;
}

std::string parse_markdown_to_html(const std::string& md, bool clear_blocks) {
    if (clear_blocks) {
        all_code_blocks.clear();
    }
    std::ostringstream html;

    std::istringstream stream(md);
    std::string line;
    bool in_code_block = false;
    std::string current_block_text;
    std::string current_lang;

    while (std::getline(stream, line)) {
        if (line.length() >= 3 && line.substr(0, 3) == "```") {
            in_code_block = !in_code_block;
            if (in_code_block) {
                current_block_text.clear();
                current_lang = line.substr(3); 
                
                size_t first = current_lang.find_first_not_of(" \t\r\n");
                if (std::string::npos == first) {
                    current_lang = "Code";
                } else {
                    size_t last = current_lang.find_last_not_of(" \t\r\n");
                    current_lang = current_lang.substr(first, (last - first + 1));
                }

                html << "<br><table bgcolor='#2d2d2d' width='100%'><tr><td>"
                     << "<b><font color='#a0a0a0'>" << escape_html(current_lang) << "</font></b><br>"
                     << "<pre><font color='#ffffff'>";
            } else {
                if (clear_blocks) {
                    all_code_blocks.push_back({current_lang, current_block_text});
                }
                html << "</font></pre></td></tr></table><br>";
            }
            continue;
        }

        if (in_code_block) {
            current_block_text += line + "\n";
            html << escape_html(line) << "\n";
        } else {
            // Heading formatting
            if (line.compare(0, 2, "# ") == 0) line = "<h2>" + escape_html(line.substr(2)) + "</h2>";
            else if (line.compare(0, 3, "## ") == 0) line = "<h3>" + escape_html(line.substr(3)) + "</h3>";
            else if (line.compare(0, 4, "### ") == 0) line = "<h4>" + escape_html(line.substr(4)) + "</h4>";
            else {
                line = escape_html(line);
            }
            
            // Bold formatting
            size_t pos = 0;
            while ((pos = line.find("**", pos)) != std::string::npos) {
                size_t end = line.find("**", pos + 2);
                if (end != std::string::npos) {
                    line = line.substr(0, pos) + "<b>" + line.substr(pos + 2, end - (pos + 2)) + "</b>" + line.substr(end + 2);
                    pos += 3;
                } else break;
            }

            // Basic layout
            if (line.empty()) html << "<br>";
            else html << line << "<br>";
        }
    }
    
    // Close unclosed tags if streaming
    if (in_code_block) {
        html << "</font></pre></td></tr></table><br>";
        if (clear_blocks) {
            all_code_blocks.push_back({current_lang, current_block_text});
        }
    }

    return html.str();
}

std::string render_raw_code_html(const std::string& code) {
    std::ostringstream html;
    html << "<br><table bgcolor='#2d2d2d' width='100%'><tr><td>"
         << "<b><font color='#a0a0a0'>Code Output</font></b><br>"
         << "<pre><font color='#ffffff'>"
         << escape_html(code)
         << "</font></pre></td></tr></table><br>";
    return html.str();
}

std::string strip_interactive_prompts(const std::string& text) {
    std::string result = text;

    // Common tgpt interactive prompt patterns (at end of output)
    const char* prompts[] = {">>> ", "╰─> ", ">> ", ">>> "};
    for (const char* p : prompts) {
        std::string ps(p);
        // Remove trailing prompt
        while (result.size() >= ps.size() &&
               result.compare(result.size() - ps.size(), ps.size(), ps) == 0) {
            result.resize(result.size() - ps.size());
        }
    }

    // Trim trailing whitespace/newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

} // namespace tgpt
