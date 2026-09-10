#pragma once

#include <string>

/**
 * @file StringUtils.hpp
 * @brief Shared string conversion and formatting helpers used across the
 *        project.
 */

namespace StringUtils
{

/**
 * @brief Replace every occurrence of a substring in a string.
 *
 * @param s The string to be modified in place.
 * @param from The substring to search for.
 * @param to The replacement substring.
 */
static void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.length(), to);
        pos += to.length();  // Skip replaced parts to prevent infinite loop
    }
}

/**
 * @brief Convert newline characters into an HTML line break token.
 *
 * @param s The input string to transform.
 * @return A copy of `s` where every `\n` is replaced with `<br>`.
 */
static std::string convertNewlineToBr(std::string s)
{
    replaceAll(s, "\n", "<br>");
    return s;
}

/**
 * @brief Estimate the display width of a UTF-8 string in Excel.
 *
 * This helper approximates the visible column width contribution of a string
 * when rendered in spreadsheet cells. The estimate treats ASCII, 2-byte,
 * 3-byte, and 4-byte UTF-8 sequences with different display multipliers.
 *
 * @param str The input UTF-8 string.
 * @return An approximate Excel display width value.
 */
static double getExcelDisplayWidth(const std::string& str) {
    double width = 0.0;
    size_t i = 0;
    
    while (i < str.length()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        if (c < 0x80) {
            // ASCII characters (English letters, digits, half-width symbols)
            // occupy one byte and render as a width of 1.0.
            width += 1.0;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 characters such as Latin or Greek extensions.
            width += 1.3;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 characters such as common CJK ideographs.
            // In Excel they are approximately equivalent to 1.8 ASCII cells.
            width += 1.8;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8 characters such as emoji or other extended glyphs.
            width += 2.0;
            i += 4;
        } else {
            i += 1; // Skip invalid or malformed continuation bytes.
        }
    }
    
    return width;
}

}   // namespace StringUtils
