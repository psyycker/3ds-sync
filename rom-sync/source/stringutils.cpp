#include "stringutils.hpp"

#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

std::u16string StringUtils::UTF8toUTF16(const char* src)
{
    const uint8_t* in = (const uint8_t*)src;
    ssize_t units     = utf8_to_utf16(nullptr, in, 0);
    if (units < 0) {
        return u"";
    }
    std::u16string dst(units, u'\0');
    utf8_to_utf16((uint16_t*)dst.data(), in, units + 1);
    return dst;
}

std::string StringUtils::UTF16toUTF8(const std::u16string& src)
{
    const uint16_t* in = (const uint16_t*)src.c_str();
    ssize_t units      = utf16_to_utf8(nullptr, in, 0);
    if (units < 0) {
        return "";
    }
    std::string dst(units, '\0');
    utf16_to_utf8((uint8_t*)dst.data(), in, units + 1);
    return dst;
}

std::u16string StringUtils::removeForbiddenCharacters(std::u16string src)
{
    static const std::u16string illegalChars = StringUtils::UTF8toUTF16(".,!\\/:?*\"<>|");
    for (size_t i = 0; i < src.length(); i++) {
        if (illegalChars.find(src[i]) != std::string::npos) {
            src[i] = ' ';
        }
    }

    size_t end = src.find_last_not_of(u' ');
    if (end == std::u16string::npos) {
        src.clear();
    }
    else {
        src.erase(end + 1);
    }

    return src;
}

// One decoder and one glyph-width cache for every text-measuring path.
namespace {
    std::map<u16, charWidthInfo_s*> widthCache;
    std::queue<u16> widthCacheOrder;
    constexpr size_t WIDTH_CACHE_CAP = 1000;

    u16 nextCodepoint(const std::string& s, size_t i, int& extraBytes)
    {
        u16 codepoint = 0xFFFF;
        extraBytes    = 0;
        if (s[i] & 0x80 && s[i] & 0x40 && s[i] & 0x20 && !(s[i] & 0x10) && i + 2 < s.size()) {
            codepoint  = s[i] & 0x0F;
            codepoint  = codepoint << 6 | (s[i + 1] & 0x3F);
            codepoint  = codepoint << 6 | (s[i + 2] & 0x3F);
            extraBytes = 2;
        }
        else if (s[i] & 0x80 && s[i] & 0x40 && !(s[i] & 0x20) && i + 1 < s.size()) {
            codepoint  = s[i] & 0x1F;
            codepoint  = codepoint << 6 | (s[i + 1] & 0x3F);
            extraBytes = 1;
        }
        else if (!(s[i] & 0x80)) {
            codepoint = s[i];
        }
        return codepoint;
    }

    float glyphWidth(u16 codepoint, float scaleX)
    {
        auto width = widthCache.find(codepoint);
        if (width != widthCache.end()) {
            return width->second->charWidth * scaleX;
        }
        widthCache.insert_or_assign(codepoint, fontGetCharWidthInfo(NULL, fontGlyphIndexFromCodePoint(NULL, codepoint)));
        widthCacheOrder.push(codepoint);
        if (widthCache.size() > WIDTH_CACHE_CAP) {
            widthCache.erase(widthCacheOrder.front());
            widthCacheOrder.pop();
        }
        return widthCache[codepoint]->charWidth * scaleX;
    }
}

std::string StringUtils::splitWord(const std::string& text, float scaleX, float maxWidth)
{
    std::string word = text;
    if (StringUtils::textWidth(word, scaleX) > maxWidth) {
        float currentWidth = 0.0f;
        for (size_t i = 0; i < word.size(); i++) {
            int extraBytes;
            u16 codepoint   = nextCodepoint(word, i, extraBytes);
            float charWidth = glyphWidth(codepoint, scaleX);
            currentWidth += charWidth;
            if (currentWidth > maxWidth) {
                word.insert(i, 1, '\n');
                currentWidth = charWidth;
            }
            i += extraBytes;
        }
    }
    return word;
}

float StringUtils::textWidth(const std::string& text, float scaleX)
{
    float ret        = 0.0f;
    float largestRet = 0.0f;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') {
            largestRet = std::max(largestRet, ret);
            ret        = 0.0f;
            continue;
        }
        int extraBytes;
        u16 codepoint = nextCodepoint(text, i, extraBytes);
        i += extraBytes;
        ret += glyphWidth(codepoint, scaleX);
    }
    return std::max(largestRet, ret);
}

float StringUtils::textWidth(const C2D_Text& text, float scaleX)
{
    return ceilf(text.width * scaleX);
}

std::string StringUtils::wrap(const std::string& text, float scaleX, float maxWidth)
{
    if (textWidth(text, scaleX) <= maxWidth) {
        return text;
    }
    std::string dst, line, word;
    dst = line = word = "";

    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        word += *it;
        if (*it == ' ') {
            if (StringUtils::textWidth(line + word, scaleX) <= maxWidth) {
                line += word;
            }
            else {
                if (StringUtils::textWidth(word, scaleX) > maxWidth) {
                    line += word;
                    line = StringUtils::splitWord(line, scaleX, maxWidth);
                    word = line.substr(line.find('\n') + 1, std::string::npos);
                    line = line.substr(0, line.find('\n'));
                }
                if (line[line.size() - 1] == ' ') {
                    dst += line.substr(0, line.size() - 1) + '\n';
                }
                else {
                    dst += line + '\n';
                }
                line = word;
            }
            word = "";
        }
    }

    if (StringUtils::textWidth(line + word, scaleX) <= maxWidth) {
        dst += line + word;
    }
    else {
        if (StringUtils::textWidth(word, scaleX) > maxWidth) {
            line += word;
            line = StringUtils::splitWord(line, scaleX, maxWidth);
            word = line.substr(line.find('\n') + 1, std::string::npos);
            line = line.substr(0, line.find('\n'));
        }
        if (line[line.size() - 1] == ' ') {
            dst += line.substr(0, line.size() - 1) + '\n' + word;
        }
        else {
            dst += line + '\n' + word;
        }
    }
    return dst;
}

float StringUtils::textHeight(const std::string& text, float scaleY)
{
    size_t n = std::count(text.begin(), text.end(), '\n') + 1;
    return ceilf(scaleY * fontGetInfo(NULL)->lineFeed * n);
}

std::string StringUtils::humanBytes(u64 bytes)
{
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        return StringUtils::format("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024ull * 1024ull) {
        return StringUtils::format("%.1f MB", bytes / (1024.0 * 1024.0));
    }
    if (bytes >= 1024ull) {
        return StringUtils::format("%.1f KB", bytes / 1024.0);
    }
    return StringUtils::format("%llu B", bytes);
}
