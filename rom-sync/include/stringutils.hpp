#pragma once
#include "common.hpp" // StringUtils::format
#include <citro2d.h>
#include <string>

// Text-measuring/encoding helpers extracted from checkpoint's
// StringUtils (3ds/include/util.hpp + 3ds/source/util.cpp) - just the pieces
// the ported UI framework (ModalChrome, ListPickerOverlay, KeyboardManager,
// TextPool) actually calls. checkpoint's util.hpp pulls in Configuration,
// Archive and half the app to get these; this is the same functions with
// none of that entanglement.
namespace StringUtils {
    std::u16string UTF8toUTF16(const char* src);
    std::string UTF16toUTF8(const std::u16string& src);
    std::u16string removeForbiddenCharacters(std::u16string src);

    std::string splitWord(const std::string& text, float scaleX, float maxWidth);
    float textWidth(const std::string& text, float scaleX);
    float textWidth(const C2D_Text& text, float scaleX);
    std::string wrap(const std::string& text, float scaleX, float maxWidth);
    float textHeight(const std::string& text, float scaleY);
    std::string humanBytes(u64 bytes);
}
