#include "omnivoice_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace omnivoice {
namespace {

std::string utf8_encode(uint32_t cp) {
    std::string out;
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    return out;
}

uint32_t utf8_decode_one(const std::string & s, size_t * pos) {
    const unsigned char c = static_cast<unsigned char>(s[*pos]);
    if (c < 0x80) {
        *pos += 1;
        return c;
    }
    if ((c >> 5) == 0x6 && *pos + 1 < s.size()) {
        uint32_t cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[*pos + 1]) & 0x3f);
        *pos += 2;
        return cp;
    }
    if ((c >> 4) == 0xe && *pos + 2 < s.size()) {
        uint32_t cp = ((c & 0x0f) << 12)
            | ((static_cast<unsigned char>(s[*pos + 1]) & 0x3f) << 6)
            | (static_cast<unsigned char>(s[*pos + 2]) & 0x3f);
        *pos += 3;
        return cp;
    }
    if ((c >> 3) == 0x1e && *pos + 3 < s.size()) {
        uint32_t cp = ((c & 0x07) << 18)
            | ((static_cast<unsigned char>(s[*pos + 1]) & 0x3f) << 12)
            | ((static_cast<unsigned char>(s[*pos + 2]) & 0x3f) << 6)
            | (static_cast<unsigned char>(s[*pos + 3]) & 0x3f);
        *pos += 4;
        return cp;
    }
    *pos += 1;
    return 0xfffd;
}

std::vector<std::string> utf8_chars(const std::string & s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t start = pos;
        (void) utf8_decode_one(s, &pos);
        out.push_back(s.substr(start, pos - start));
    }
    return out;
}

std::vector<std::string> byte_encoder_table() {
    std::vector<int> bs;
    for (int i = 33; i <= 126; ++i) bs.push_back(i);
    for (int i = 161; i <= 172; ++i) bs.push_back(i);
    for (int i = 174; i <= 255; ++i) bs.push_back(i);
    std::vector<int> cs = bs;
    int n = 0;
    for (int b = 0; b < 256; ++b) {
        if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
            bs.push_back(b);
            cs.push_back(256 + n);
            ++n;
        }
    }
    std::vector<std::string> table(256);
    for (size_t i = 0; i < bs.size(); ++i) {
        table[static_cast<size_t>(bs[i])] = utf8_encode(static_cast<uint32_t>(cs[i]));
    }
    return table;
}

const std::vector<std::string> & byte_encoder() {
    static const std::vector<std::string> table = byte_encoder_table();
    return table;
}

std::string byte_level_encode(const std::string & text) {
    std::string out;
    const auto & table = byte_encoder();
    for (unsigned char c : text) {
        out += table[c];
    }
    return out;
}

bool is_ascii_letter(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

bool is_digit(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

bool is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

bool is_mark(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036f)
        || (cp >= 0x1ab0 && cp <= 0x1aff)
        || (cp >= 0x1dc0 && cp <= 0x1dff)
        || (cp >= 0x20d0 && cp <= 0x20ff)
        || (cp >= 0xfe20 && cp <= 0xfe2f);
}

bool is_punctuation_or_symbol(uint32_t cp) {
    if ((cp >= 0x21 && cp <= 0x2f) || (cp >= 0x3a && cp <= 0x40) || (cp >= 0x5b && cp <= 0x60) || (cp >= 0x7b && cp <= 0x7e)) return true;
    if (cp >= 0x2000 && cp <= 0x206f) return true;
    if (cp >= 0x2190 && cp <= 0x2bff) return true;
    if (cp >= 0x3000 && cp <= 0x303f) return true;
    if (cp >= 0xfe10 && cp <= 0xfe6f) return true;
    if (cp >= 0xff01 && cp <= 0xff0f) return true;
    if (cp >= 0xff1a && cp <= 0xff20) return true;
    if (cp >= 0xff3b && cp <= 0xff40) return true;
    if (cp >= 0xff5b && cp <= 0xff65) return true;
    return false;
}

float script_weight(uint32_t cp) {
    if (cp <= 0x02af) return 1.0f;
    if (cp <= 0x03ff) return 1.0f;
    if (cp <= 0x052f) return 1.0f;
    if (cp <= 0x058f) return 1.0f;
    if (cp <= 0x05ff) return 1.5f;
    if (cp <= 0x077f) return 1.5f;
    if (cp <= 0x089f) return 1.5f;
    if (cp <= 0x08ff) return 1.5f;
    if (cp <= 0x0dff) return 1.8f;
    if (cp <= 0x0eff) return 1.5f;
    if (cp <= 0x0fff) return 1.8f;
    if (cp <= 0x109f) return 1.8f;
    if (cp <= 0x10ff) return 1.0f;
    if (cp <= 0x11ff) return 2.5f;
    if (cp <= 0x139f) return 3.0f;
    if (cp <= 0x13ff) return 1.0f;
    if (cp <= 0x177f) return 1.0f;
    if (cp <= 0x17ff) return 1.8f;
    if (cp <= 0x18ff) return 1.0f;
    if (cp <= 0x19df) return 1.8f;
    if (cp <= 0x19ff) return 1.8f;
    if (cp <= 0x1bff) return 1.8f;
    if (cp <= 0x1c7f) return 1.8f;
    if (cp <= 0x1c8f) return 1.0f;
    if (cp <= 0x1cbf) return 1.0f;
    if (cp <= 0x1cff) return 1.8f;
    if (cp <= 0x1eff) return 1.0f;
    if (cp <= 0x309f) return 2.2f;
    if (cp <= 0x30ff) return 2.2f;
    if (cp <= 0x312f) return 3.0f;
    if (cp <= 0x318f) return 2.5f;
    if (cp <= 0x9fff) return 3.0f;
    if (cp <= 0xa4cf) return 3.0f;
    if (cp <= 0xa69f) return 1.0f;
    if (cp <= 0xa7ff) return 1.0f;
    if (cp <= 0xabff) return 1.8f;
    if (cp <= 0xd7af) return 2.5f;
    if (cp <= 0xfaff) return 3.0f;
    if (cp <= 0xfdff) return 1.5f;
    if (cp <= 0xffef) return 1.0f;
    if (cp > 0x20000) return 3.0f;
    return 1.0f;
}

bool is_letter(uint32_t cp) {
    if (is_ascii_letter(cp)) return true;
    if (cp >= 0x4e00 && cp <= 0x9fff) return true;
    if (cp >= 0x3400 && cp <= 0x4dbf) return true;
    if (cp >= 0x3040 && cp <= 0x30ff) return true;
    if (cp >= 0xac00 && cp <= 0xd7af) return true;
    if (cp >= 0x00c0 && cp <= 0x02af) return true;
    return false;
}

std::string lowercase_ascii(std::string s) {
    for (char & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool starts_with_ci(const std::string & s, size_t pos, const char * value) {
    const size_t n = std::char_traits<char>::length(value);
    if (pos + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[pos + i])) != std::tolower(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::string merge_key(const std::string & a, const std::string & b) {
    return a + "\n" + b;
}

} // namespace

Tokenizer::Tokenizer(std::vector<std::string> tokens, std::vector<std::string> merges)
    : tokens_(std::move(tokens)) {
    for (size_t i = 0; i < tokens_.size(); ++i) {
        token_to_id_[tokens_[i]] = static_cast<int32_t>(i);
        if (tokens_[i].rfind("<|", 0) == 0 && tokens_[i].size() >= 4 && tokens_[i].substr(tokens_[i].size() - 2) == "|>") {
            special_tokens_.push_back(tokens_[i]);
        }
    }
    std::sort(special_tokens_.begin(), special_tokens_.end(), [](const std::string & a, const std::string & b) {
        return a.size() > b.size();
    });

    for (size_t i = 0; i < merges.size(); ++i) {
        const std::string & m = merges[i];
        const size_t p = m.find(' ');
        if (p == std::string::npos) continue;
        merge_rank_[merge_key(m.substr(0, p), m.substr(p + 1))] = static_cast<int>(i);
    }
}

bool Tokenizer::has_token(const std::string & token) const {
    return token_to_id_.find(token) != token_to_id_.end();
}

int32_t Tokenizer::token_id(const std::string & token) const {
    auto it = token_to_id_.find(token);
    if (it == token_to_id_.end()) {
        throw std::runtime_error("missing tokenizer token: " + token);
    }
    return it->second;
}

std::vector<int32_t> Tokenizer::encode(const std::string & text, bool) const {
    std::vector<int32_t> out;
    size_t pos = 0;
    while (pos < text.size()) {
        bool matched = false;
        for (const std::string & sp : special_tokens_) {
            if (sp.empty()) continue;
            if (text.compare(pos, sp.size(), sp) == 0) {
                out.push_back(token_id(sp));
                pos += sp.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;

        size_t next = text.size();
        for (const std::string & sp : special_tokens_) {
            const size_t found = text.find(sp, pos + 1);
            if (found != std::string::npos) next = std::min(next, found);
        }
        auto ids = encode_ordinary(text.substr(pos, next - pos));
        out.insert(out.end(), ids.begin(), ids.end());
        pos = next;
    }
    return out;
}

std::vector<int32_t> Tokenizer::encode_ordinary(const std::string & text) const {
    std::vector<int32_t> out;
    for (const std::string & piece : pretokenize(text)) {
        auto ids = bpe(byte_level_encode(piece));
        out.insert(out.end(), ids.begin(), ids.end());
    }
    return out;
}

std::vector<std::string> Tokenizer::pretokenize(const std::string & text) const {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] == '\'' && (
                starts_with_ci(text, pos, "'s") || starts_with_ci(text, pos, "'t") ||
                starts_with_ci(text, pos, "'re") || starts_with_ci(text, pos, "'ve") ||
                starts_with_ci(text, pos, "'m") || starts_with_ci(text, pos, "'ll") ||
                starts_with_ci(text, pos, "'d"))) {
            size_t n = 2;
            if (starts_with_ci(text, pos, "'re") || starts_with_ci(text, pos, "'ve") || starts_with_ci(text, pos, "'ll")) n = 3;
            out.push_back(text.substr(pos, n));
            pos += n;
            continue;
        }

        size_t start = pos;
        uint32_t cp = utf8_decode_one(text, &pos);
        if (is_space(cp)) {
            size_t scan = pos;
            if (cp == ' ' && scan < text.size()) {
                size_t tmp = scan;
                uint32_t next = utf8_decode_one(text, &tmp);
                if (!is_space(next)) {
                    pos = scan;
                    cp = next;
                    size_t after = tmp;
                    if (is_letter(cp)) {
                        while (after < text.size()) {
                            size_t p = after;
                            uint32_t c = utf8_decode_one(text, &p);
                            if (!is_letter(c)) break;
                            after = p;
                        }
                    } else if (is_digit(cp)) {
                        after = tmp;
                    } else {
                        while (after < text.size()) {
                            size_t p = after;
                            uint32_t c = utf8_decode_one(text, &p);
                            if (is_space(c) || is_letter(c) || is_digit(c)) break;
                            after = p;
                        }
                    }
                    out.push_back(text.substr(start, after - start));
                    pos = after;
                    continue;
                }
            }
            while (pos < text.size()) {
                size_t p = pos;
                uint32_t c = utf8_decode_one(text, &p);
                if (!is_space(c)) break;
                pos = p;
            }
            out.push_back(text.substr(start, pos - start));
        } else if (is_letter(cp)) {
            while (pos < text.size()) {
                size_t p = pos;
                uint32_t c = utf8_decode_one(text, &p);
                if (!is_letter(c)) break;
                pos = p;
            }
            out.push_back(text.substr(start, pos - start));
        } else if (is_digit(cp)) {
            out.push_back(text.substr(start, pos - start));
        } else {
            while (pos < text.size()) {
                size_t p = pos;
                uint32_t c = utf8_decode_one(text, &p);
                if (is_space(c) || is_letter(c) || is_digit(c)) break;
                pos = p;
            }
            out.push_back(text.substr(start, pos - start));
        }
    }
    return out;
}

std::vector<int32_t> Tokenizer::bpe(const std::string & piece) const {
    std::vector<std::string> word = utf8_chars(piece);
    if (word.empty()) return {};

    while (word.size() > 1) {
        int best_rank = std::numeric_limits<int>::max();
        size_t best = static_cast<size_t>(-1);
        for (size_t i = 0; i + 1 < word.size(); ++i) {
            auto it = merge_rank_.find(merge_key(word[i], word[i + 1]));
            if (it != merge_rank_.end() && it->second < best_rank) {
                best_rank = it->second;
                best = i;
            }
        }
        if (best == static_cast<size_t>(-1)) break;
        word[best] += word[best + 1];
        word.erase(word.begin() + static_cast<std::ptrdiff_t>(best + 1));
    }

    std::vector<int32_t> ids;
    ids.reserve(word.size());
    for (const std::string & tok : word) {
        auto it = token_to_id_.find(tok);
        if (it == token_to_id_.end()) {
            throw std::runtime_error("BPE produced token not present in GGUF vocabulary");
        }
        ids.push_back(it->second);
    }
    return ids;
}

bool contains_cjk(const std::string & text) {
    size_t pos = 0;
    while (pos < text.size()) {
        uint32_t cp = utf8_decode_one(text, &pos);
        if ((cp >= 0x4e00 && cp <= 0x9fff) || (cp >= 0x3400 && cp <= 0x4dbf)) return true;
    }
    return false;
}

int utf8_length(const std::string & text) {
    int n = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        (void) utf8_decode_one(text, &pos);
        ++n;
    }
    return n;
}

float text_duration_weight(const std::string & text) {
    float total = 0.0f;
    size_t pos = 0;
    while (pos < text.size()) {
        const uint32_t cp = utf8_decode_one(text, &pos);
        if (cp == 0x0640 || is_mark(cp)) {
            continue;
        } else if (is_space(cp) || cp == 0x3000) {
            total += 0.2f;
        } else if (is_digit(cp) || (cp >= 0xff10 && cp <= 0xff19)) {
            total += 3.5f;
        } else if (is_punctuation_or_symbol(cp)) {
            total += 0.5f;
        } else {
            total += script_weight(cp);
        }
    }
    return total;
}

std::string resolve_language(const std::string & language) {
    std::string key = lowercase_ascii(language);
    if (key.empty() || key == "none") return "";
    if (key == "chinese" || key == "zh" || key == "zho") return "zh";
    if (key == "english" || key == "en" || key == "eng") return "en";
    if (key.size() <= 3) return key;
    return "";
}

std::string resolve_instruct(const std::string & instruct, bool use_zh) {
    if (instruct.empty()) return "";
    std::unordered_map<std::string, std::string> en_to_zh = {
        {"male", "男"}, {"female", "女"}, {"child", "儿童"}, {"teenager", "少年"},
        {"young adult", "青年"}, {"middle-aged", "中年"}, {"elderly", "老年"},
        {"very low pitch", "极低音调"}, {"low pitch", "低音调"}, {"moderate pitch", "中音调"},
        {"high pitch", "高音调"}, {"very high pitch", "极高音调"}, {"whisper", "耳语"},
    };
    std::unordered_map<std::string, std::string> zh_to_en;
    for (const auto & kv : en_to_zh) zh_to_en[kv.second] = kv.first;

    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= instruct.size(); ++i) {
        const bool sep = i == instruct.size() || instruct[i] == ',' || static_cast<unsigned char>(instruct[i]) == 0xef;
        if (!sep) continue;
        std::string item = instruct.substr(start, i - start);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
        item = lowercase_ascii(item);
        if (!item.empty()) parts.push_back(item);
        if (i < instruct.size() && static_cast<unsigned char>(instruct[i]) == 0xef && i + 2 < instruct.size()) i += 2;
        start = i + 1;
    }

    bool has_dialect = false;
    bool has_accent = false;
    for (const std::string & p : parts) {
        if (p.find(" accent") != std::string::npos) has_accent = true;
        if (p.size() >= 3 && p.substr(p.size() - 3) == "话") has_dialect = true;
    }
    if (has_dialect) use_zh = true;
    if (has_accent) use_zh = false;

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string item = parts[i];
        if (use_zh) {
            auto it = en_to_zh.find(item);
            if (it != en_to_zh.end()) item = it->second;
        } else {
            auto it = zh_to_en.find(item);
            if (it != zh_to_en.end()) item = it->second;
        }
        if (i) out += use_zh ? "，" : ", ";
        out += item;
    }
    return out;
}

} // namespace omnivoice
