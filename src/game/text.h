#pragma once
#include "platform/types.h"

#include "compiled/warning_off.h"
#include <sstream>
#include <string>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
{
// "a|b" -> {"a", "b"}: one entry per dialog page.
inline std::vector<std::string> SplitPages(const std::string &text)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : text)
    {
        if (c == '|')
        {
            out.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
    {
        out.push_back(current);
    }
    return out;
}

// Word-wraps to at most `width` characters per line; '\n' forces a break.
inline std::vector<std::string> Wrap(const std::string &text, const size_t width)
{
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph, '\n'))
    {
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word)
        {
            if (!line.empty() && line.size() + 1 + word.size() > width)
            {
                lines.push_back(line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
        }
        lines.push_back(line);
    }
    return lines;
}
} // namespace crittercap
