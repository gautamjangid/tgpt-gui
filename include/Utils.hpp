#pragma once

#include <string>

namespace tgpt {

std::string sanitize_output(const std::string& raw);
std::string escape_html(const std::string& data);
std::string parse_markdown_to_html(const std::string& md, bool clear_blocks = true);

} // namespace tgpt
