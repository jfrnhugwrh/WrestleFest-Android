#include "sml_parser.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace wrestlefest {
namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trimmed(const std::string &s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Splits "U16|Id" / "S16A24|FrameArray" into type and name.
void splitTag(const std::string &tag, SmlType *type, int *count, std::string *name, std::string *raw_type) {
    const std::size_t bar = tag.find('|');
    std::string lhs = trimmed(bar == std::string::npos ? tag : tag.substr(0, bar));
    *name = bar == std::string::npos ? std::string() : trimmed(tag.substr(bar + 1));
    *raw_type = lhs;
    *count = 1;

    // Strip a trailing array length, e.g. F32A2, S16A24, F32A5.
    std::string base = lhs;
    std::size_t i = base.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(base[i - 1]))) --i;
    if (i > 0 && i < base.size() && (base[i - 1] == 'A' || base[i - 1] == 'a')) {
        *count = std::atoi(base.c_str() + i);
        if (*count <= 0) *count = 1;
        base = base.substr(0, i - 1);
    }

    if (base == "U8") {
        *type = SmlType::U8;
    } else if (base == "U16") {
        *type = SmlType::U16;
    } else if (base == "U32") {
        *type = SmlType::U32;
    } else if (base == "S8") {
        *type = SmlType::S8;
    } else if (base == "S16") {
        *type = SmlType::S16;
    } else if (base == "S32") {
        *type = SmlType::S32;
    } else if (base == "F32") {
        *type = SmlType::F32;
    } else if (base == "STRING" || base == "STR") {
        *type = SmlType::Str;
    } else {
        *type = SmlType::Other;
    }
}

void parseStringValue(const std::string &raw, std::string *out) {
    std::string s = trimmed(raw);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    // The tables escape an apostrophe by doubling it ("Jake ''The Snake''").
    std::string unescaped;
    unescaped.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
            unescaped.push_back('\'');
            ++i;
        } else {
            unescaped.push_back(s[i]);
        }
    }
    *out = unescaped;
}

struct Token {
    bool tag = false;
    std::string text;
};

class Lexer {
public:
    Lexer(const char *data, std::size_t size) : data_(data), size_(size) {}

    // Allows a single token of lookahead.
    void pushBack(const Token &token) { pushed_ = token; has_pushed_ = true; }

    bool next(Token *token) {
        if (has_pushed_) {
            *token = pushed_;
            has_pushed_ = false;
            return true;
        }
        // Skip whitespace between tokens only when it precedes a tag; raw values
        // keep their content and are trimmed by the consumers.
        while (pos_ < size_ && isSpace(data_[pos_])) ++pos_;
        if (pos_ >= size_) return false;
        if (data_[pos_] == '<') {
            const std::size_t close = find('>', pos_ + 1);
            if (close == std::string::npos) {
                pos_ = size_;
                return false;
            }
            token->tag = true;
            token->text.assign(data_ + pos_ + 1, close - pos_ - 1);
            pos_ = close + 1;
            return true;
        }
        // Braces delimit records, so they are always their own token - otherwise
        // "<ARRAY|X> 117 {" would swallow the opening brace into the count.
        if (data_[pos_] == '{' || data_[pos_] == '}') {
            token->tag = false;
            token->text.assign(1, data_[pos_]);
            ++pos_;
            return true;
        }
        const std::size_t next_tag = findFirstOf("<{}", pos_);
        const std::size_t end = next_tag == std::string::npos ? size_ : next_tag;
        token->tag = false;
        token->text.assign(data_ + pos_, end - pos_);
        pos_ = end;
        return true;
    }

private:
    std::size_t find(char c, std::size_t from) const {
        for (std::size_t i = from; i < size_; ++i) {
            if (data_[i] == c) return i;
        }
        return std::string::npos;
    }

    std::size_t findFirstOf(const char *chars, std::size_t from) const {
        for (std::size_t i = from; i < size_; ++i) {
            for (const char *c = chars; *c != '\0'; ++c) {
                if (data_[i] == *c) return i;
            }
        }
        return std::string::npos;
    }

    const char *data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    Token pushed_;
    bool has_pushed_ = false;
};

bool startsWith(const std::string &s, const char *prefix) {
    const std::size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

bool parseRecordBody(Lexer *lexer, SmlRecord *record, const std::string &array_name,
                     std::string *error);

// Parses "<TYPE|name> value" entries until the record's closing '}'.
bool parseFields(Lexer *lexer, std::vector<SmlField> *fields, std::string *error) {
    (void)error;
    Token token;
    while (lexer->next(&token)) {
        if (!token.tag) {
            const std::string t = trimmed(token.text);
            if (!t.empty() && t[0] == '}') return true;  // end of record
            continue;                                     // '{' or stray value
        }
        const std::string &tag = token.text;
        if (startsWith(tag, "ARRAYEND")) return true;
        if (startsWith(tag, "ARRAY|")) {
            // Nested arrays (master.tyFX shader lists) are skipped: nothing the
            // simulation reads is nested.
            Token count_token;
            if (!lexer->next(&count_token)) return false;
            const int count = std::atoi(count_token.text.c_str());
            for (int i = 0; i < count; ++i) {
                SmlRecord nested;
                std::string nested_error;
                parseRecordBody(lexer, &nested, "", &nested_error);
            }
            continue;
        }

        SmlField field;
        std::string raw_type;
        splitTag(tag, &field.type, &field.count, &field.name, &raw_type);
        Token value;
        if (!lexer->next(&value)) return true;
        if (value.tag) {
            // Empty value: hand the token back and keep going.
            lexer->pushBack(value);
            fields->push_back(std::move(field));
            continue;
        }
        if (field.type == SmlType::Str) {
            parseStringValue(value.text, &field.text);
        } else {
            field.numbers.reserve(static_cast<std::size_t>(field.count));
            const std::string &v = value.text;
            std::size_t i = 0;
            while (i < v.size() && static_cast<int>(field.numbers.size()) < field.count) {
                while (i < v.size() && (isSpace(v[i]) || v[i] == ',')) ++i;
                if (i >= v.size()) break;
                const char *begin = v.c_str() + i;
                char *end = nullptr;
                const double number = std::strtod(begin, &end);
                if (end == begin) break;
                field.numbers.push_back(number);
                i = static_cast<std::size_t>(end - v.c_str());
            }
            while (static_cast<int>(field.numbers.size()) < field.count) {
                field.numbers.push_back(0.0);
            }
        }
        fields->push_back(std::move(field));
    }
    return true;
}

bool parseRecordBody(Lexer *lexer, SmlRecord *record, const std::string &array_name,
                     std::string *error) {
    (void)array_name;
    Token token;
    // Expect '{'
    while (lexer->next(&token)) {
        if (token.tag) {
            if (startsWith(token.text, "ARRAYEND")) return true;
            continue;
        }
        const std::string t = trimmed(token.text);
        if (!t.empty() && t[0] == '{') break;
        if (!t.empty() && t[0] == '}') return true;  // empty record
    }
    return parseFields(lexer, &record->fields, error);
}

}  // namespace

const char *smlTypeName(SmlType type) {
    switch (type) {
        case SmlType::U8: return "U8";
        case SmlType::U16: return "U16";
        case SmlType::U32: return "U32";
        case SmlType::S8: return "S8";
        case SmlType::S16: return "S16";
        case SmlType::S32: return "S32";
        case SmlType::F32: return "F32";
        case SmlType::Str: return "STR";
        default: return "?";
    }
}

const SmlArray *SmlDocument::array(const char *name) const {
    for (const SmlArray &block : arrays) {
        if (block.name == name) return &block;
    }
    return nullptr;
}

const SmlField *SmlRecord::find(const char *name) const {
    for (const SmlField &field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

double SmlRecord::number(const char *name, double fallback) const {
    const SmlField *field = find(name);
    if (field == nullptr || field->numbers.empty()) return fallback;
    return field->numbers[0];
}

int SmlRecord::integer(const char *name, int fallback) const {
    const SmlField *field = find(name);
    if (field == nullptr || field->numbers.empty()) return fallback;
    return static_cast<int>(field->numbers[0]);
}

std::string SmlRecord::string(const char *name) const {
    const SmlField *field = find(name);
    return field == nullptr ? std::string() : field->text;
}

double SmlRecord::numberAt(const char *name, int index, double fallback) const {
    const SmlField *field = find(name);
    if (field == nullptr || index < 0 || index >= static_cast<int>(field->numbers.size())) {
        return fallback;
    }
    return field->numbers[static_cast<std::size_t>(index)];
}

bool SmlDocument::parse(const char *data, std::size_t size, std::string *error) {
    // Documents are reused across files by GameData, so always start clean.
    root_name.clear();
    array_name.clear();
    records.clear();
    arrays.clear();
    Lexer lexer(data, size);
    Token token;

    // <SML V1.1>
    if (!lexer.next(&token) || !token.tag || !startsWith(token.text, "SML")) {
        if (error != nullptr) *error = "missing <SML> header";
        return false;
    }

    // <RootName>
    bool have_root = false;
    while (lexer.next(&token)) {
        if (!token.tag) continue;
        if (startsWith(token.text, "ARRAY|")) {
            // Some files have no root element; treat the array as top level.
            root_name.clear();
            break;
        }
        root_name = trimmed(token.text);
        have_root = true;
        break;
    }
    if (!have_root && root_name.empty() && token.tag && !startsWith(token.text, "ARRAY|")) {
        if (error != nullptr) *error = "missing root element";
        return false;
    }

    // Root level: one or more ARRAY blocks (fonts have Header + CharTable),
    // each terminated by its own ARRAYEND.
    while (lexer.next(&token)) {
        if (!token.tag) continue;
        if (startsWith(token.text, "ARRAY|")) {
            std::size_t bar = token.text.find('|');
            std::string rest = trimmed(token.text.substr(bar + 1));
            const std::size_t space = rest.find(' ');
            array_name = space == std::string::npos ? rest : rest.substr(0, space);
            Token count_token;
            if (!lexer.next(&count_token)) break;
            const int count = std::atoi(trimmed(count_token.text).c_str());
            records.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
            SmlArray parsed_array;
            parsed_array.name = array_name;
            for (int i = 0; i < count; ++i) {
                SmlRecord record;
                if (!parseRecordBody(&lexer, &record, array_name, error)) {
                    if (error != nullptr) *error = "truncated record list";
                    return false;
                }
                parsed_array.records.push_back(std::move(record));
            }
            if (records.empty()) {
                records = parsed_array.records;
            }
            arrays.push_back(std::move(parsed_array));
            continue;
        }
        // ARRAYEND closes the block that was just consumed; keep reading so a
        // second top-level array is not lost.
        if (startsWith(token.text, "ARRAYEND")) continue;
    }
    if (records.empty()) {
        if (error != nullptr) *error = "no array records";
        return false;
    }
    return true;
}

}  // namespace wrestlefest
