#pragma once

#include <format>
#include <string>

// Minimal stand-in for checkpoint's Logging namespace (common/logging.hpp) -
// same call surface (the framework files ported from checkpoint call
// Logging::error/info with std::format args), but no file logging, no HTTP
// log endpoints, no crash handler: rom-sync just writes to the console.
namespace Logging {
    void log(const std::string& message);

    inline void trace(const std::string& message) { log(message); }
    inline void debug(const std::string& message) { log(message); }
    inline void info(const std::string& message) { log(message); }
    inline void warning(const std::string& message) { log(message); }
    inline void error(const std::string& message) { log(message); }

    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        log(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        log(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        log(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        log(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        log(std::format(fmt, std::forward<Args>(args)...));
    }
}
