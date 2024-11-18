#include <source_location>
#include <format>

#define ReportError(fmt, ...) ReportErrorImpl(std::source_location::current(), std::format(fmt, __VA_ARGS__))
#define LogDeb(fmt, ...) LogDebImp(std::source_location::current(), std::format(fmt, __VA_ARGS__))
#define LogError(fmt, ...) LogErrorImpl(std::source_location::current(), std::format(fmt, __VA_ARGS__))

void ReportErrorImpl(const std::source_location location, const std::string &msg);
void LogErrorImpl(const std::source_location location, const std::string &msg);
void LogDebImp(const std::source_location location, const std::string &msg);
