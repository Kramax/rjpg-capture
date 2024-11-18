#ifndef __RJPG_CAPTURE_HPP
#define __RJPG_CAPTURE_HPP

#include <source_location>
#include <string>

#include <stdarg.h>  // For va_start, etc.
#include <memory>    // For std::unique_ptr

inline std::string string_format(const std::string fmt_str, ...) 
{
    int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
    std::unique_ptr<char[]> formatted;
    va_list ap;
    while(1) {
        formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
        strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
    return std::string(formatted.get());
}

#ifdef HAS_SOURCE_LOCATION // sadly rpi debian is not up to date and is missing this
#define ReportError(fmt, ...) ReportErrorImpl(std::source_location::current(), string_format(fmt, __VA_ARGS__))
#define LogDeb(fmt, ...) LogDebImp(std::source_location::current(), string_format(fmt, __VA_ARGS__))
#define LogError(fmt, ...) LogErrorImpl(std::source_location::current(), string_format(fmt, __VA_ARGS__))

void ReportErrorImpl(const std::source_location location, const std::string &msg);
void LogErrorImpl(const std::source_location location, const std::string &msg);
void LogDebImp(const std::source_location location, const std::string &msg);
#else
#define ReportError(fmt, ...) ReportErrorImpl(string_format(fmt, __VA_ARGS__))
#define LogDeb(fmt, ...) LogDebImp(string_format(fmt, __VA_ARGS__))
#define LogError(fmt, ...) LogErrorImpl(string_format(fmt, __VA_ARGS__))

void ReportErrorImpl(const std::string &msg);
void LogErrorImpl(const std::string &msg);
void LogDebImp(const std::string &msg);

#endif

#endif
