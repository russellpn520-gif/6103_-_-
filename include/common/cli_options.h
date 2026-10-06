/**
 * @file cli_options.h
 * @brief Helpers for parsing "--name=value" command-line options and numeric
 *        user input, with range checking.
 */

#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <string>
#include <cstdlib>
#include <cerrno>

namespace cli {

/**
 * @brief If arg starts with prefix (e.g. "--port="), store the rest in value.
 */
inline bool matchOption(const std::string& arg, const std::string& prefix, std::string& value) {
    if (arg.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    value = arg.substr(prefix.size());
    return true;
}

/**
 * @brief Parse a whole number in [min, max]; the whole text must be a number.
 */
inline bool parseInteger(const std::string& text, long long min, long long max, long long& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || value < min || value > max) {
        return false;
    }
    out = value;
    return true;
}

/**
 * @brief Parse a real number in [min, max]; the whole text must be a number.
 */
inline bool parseReal(const std::string& text, double min, double max, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    double value = std::strtod(text.c_str(), &end);
    if (errno != 0 || *end != '\0' || !(value >= min && value <= max)) {
        return false;
    }
    out = value;
    return true;
}

} // namespace cli

#endif // CLI_OPTIONS_H
