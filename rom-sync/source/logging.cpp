#include "logging.hpp"

#include <cstdio>

void Logging::log(const std::string& message)
{
    std::printf("%s\n", message.c_str());
}
