#include "cspromator/game_state.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cspromator {
namespace {

struct JsonObject;
struct JsonArray;

struct JsonValue {
    using ObjectPtr = std::shared_ptr<JsonObject>;
    using ArrayPtr = std::shared_ptr<JsonArray>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, ObjectPtr, ArrayPtr>;

    Storage value;

    const JsonValue* get(std::string_view key) const;
};

struct JsonObject {
    std::map<std::string, JsonValue, std::less<>> values;
};

struct JsonArray {
    std::vector<JsonValue> values;
};

const JsonValue* JsonValue::get(std::string_view key) const {
    const auto* object = std::get_if<ObjectPtr>(&value);
    if (!object || !*object) {
        return nullptr;
    }
    const auto it = (*object)->values.find(key);
    return it == (*object)->values.end() ? nullptr : &it->second;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue parse() {
        skip_ws();
        JsonValue value = parse_value();
        skip_ws();
        if (pos_ != text_.size()) {
            fail("trailing JSON data");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(message);
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            ++pos_;
        }
    }

    bool consume(char expected) {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    JsonValue parse_value() {
        skip_ws();
        if (pos_ >= text_.size()) {
            fail("unexpected end of JSON");
        }
        switch (text_[pos_]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return JsonValue{parse_string()};
            case 't': return parse_literal("true", JsonValue{true});
            case 'f': return parse_literal("false", JsonValue{false});
            case 'n': return parse_literal("null", JsonValue{std::monostate{}});
            default:
                if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
                    return parse_number();
                }
                fail("invalid JSON value");
        }
    }

    JsonValue parse_object() {
        if (!consume('{')) {
            fail("expected object");
        }
        auto object = std::make_shared<JsonObject>();
        skip_ws();
        if (consume('}')) {
            return JsonValue{object};
        }

        for (;;) {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                fail("expected JSON object key");
            }
            std::string key = parse_string();
            if (!consume(':')) {
                fail("expected colon after JSON key");
            }
            object->values.insert_or_assign(std::move(key), parse_value());
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                fail("expected comma in JSON object");
            }
        }
        return JsonValue{object};
    }

    JsonValue parse_array() {
        if (!consume('[')) {
            fail("expected array");
        }
        auto array = std::make_shared<JsonArray>();
        skip_ws();
        if (consume(']')) {
            return JsonValue{array};
        }
        for (;;) {
            array->values.push_back(parse_value());
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                fail("expected comma in JSON array");
            }
        }
        return JsonValue{array};
    }

    static int hex_value(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
        if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
        return -1;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::uint32_t parse_hex4() {
        if (pos_ + 4 > text_.size()) {
            fail("short unicode escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int nibble = hex_value(text_[pos_++]);
            if (nibble < 0) {
                fail("invalid unicode escape");
            }
            value = (value << 4) | static_cast<std::uint32_t>(nibble);
        }
        return value;
    }

    std::string parse_string() {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            fail("expected JSON string");
        }
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') {
                return out;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                fail("control character in JSON string");
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) {
                fail("short JSON escape");
            }
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = parse_hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        const std::uint32_t low = parse_hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            fail("invalid unicode surrogate pair");
                        }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        fail("unpaired unicode surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("invalid JSON escape");
            }
        }
        fail("unterminated JSON string");
    }

    JsonValue parse_number() {
        const std::size_t begin = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) fail("invalid JSON number");

        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            if (text_[pos_] < '1' || text_[pos_] > '9') fail("invalid JSON number");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }

        bool floating = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            floating = true;
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') fail("invalid fraction");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            floating = true;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') fail("invalid exponent");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }

        const auto token = text_.substr(begin, pos_ - begin);
        if (!floating) {
            std::int64_t integer{};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), integer);
            if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) {
                return JsonValue{integer};
            }
        }

        std::string temp(token);
        char* end = nullptr;
        const double number = std::strtod(temp.c_str(), &end);
        if (!end || end != temp.c_str() + temp.size() || !std::isfinite(number)) {
            fail("invalid JSON number");
        }
        return JsonValue{number};
    }

    JsonValue parse_literal(std::string_view literal, JsonValue value) {
        if (text_.substr(pos_, literal.size()) != literal) {
            fail("invalid JSON literal");
        }
        pos_ += literal.size();
        return value;
    }

    std::string_view text_;
    std::size_t pos_{};
};

std::optional<JsonValue> parse_json(std::string_view body) {
    try {
        return JsonParser(body).parse();
    } catch (...) {
        return std::nullopt;
    }
}

const JsonValue* find_path(const JsonValue& root,
                           std::initializer_list<const char*> path) {
    const JsonValue* current = &root;
    for (const char* key : path) {
        current = current->get(key);
        if (!current) {
            return nullptr;
        }
    }
    return current;
}

std::optional<std::string> read_string(const JsonValue& root,
                                       std::initializer_list<const char*> path) {
    const auto* value = find_path(root, path);
    if (!value) return std::nullopt;
    const auto* string = std::get_if<std::string>(&value->value);
    return string ? std::optional<std::string>(*string) : std::nullopt;
}

std::optional<int> read_int(const JsonValue& root,
                            std::initializer_list<const char*> path) {
    const auto* value = find_path(root, path);
    if (!value) return std::nullopt;
    if (const auto* integer = std::get_if<std::int64_t>(&value->value)) {
        return static_cast<int>(*integer);
    }
    return std::nullopt;
}

std::optional<std::int64_t> read_i64(const JsonValue& root,
                                     std::initializer_list<const char*> path) {
    const auto* value = find_path(root, path);
    if (!value) return std::nullopt;
    if (const auto* integer = std::get_if<std::int64_t>(&value->value)) {
        return *integer;
    }
    return std::nullopt;
}

} // namespace

NormalizedGameState normalize_gsi(std::string_view json_body,
                                  std::uint64_t sequence,
                                  std::uint64_t relative_us) {
    NormalizedGameState state{};
    state.sequence = sequence;
    state.relative_us = relative_us;

    const auto root = parse_json(json_body);
    if (!root || !std::get_if<JsonValue::ObjectPtr>(&root->value)) {
        return state;
    }
    state.payload_valid = true;

    state.provider_steamid = read_string(*root, {"provider", "steamid"});
    state.provider_timestamp = read_i64(*root, {"provider", "timestamp"});

    state.map_name = read_string(*root, {"map", "name"});
    state.map_mode = read_string(*root, {"map", "mode"});
    state.map_phase = read_string(*root, {"map", "phase"});
    state.map_round = read_int(*root, {"map", "round"});
    state.ct_score = read_int(*root, {"map", "team_ct", "score"});
    state.t_score = read_int(*root, {"map", "team_t", "score"});

    state.round_phase = read_string(*root, {"round", "phase"});
    state.round_winner = read_string(*root, {"round", "win_team"});
    state.bomb_state = read_string(*root, {"round", "bomb"});

    state.observed_steamid = read_string(*root, {"player", "steamid"});
    state.local_player_valid = state.provider_steamid && state.observed_steamid &&
                               *state.provider_steamid == *state.observed_steamid;

    state.player_team = read_string(*root, {"player", "team"});
    state.player_activity = read_string(*root, {"player", "activity"});
    if (state.local_player_valid) {
        state.health = read_int(*root, {"player", "state", "health"});
        state.armor = read_int(*root, {"player", "state", "armor"});
        state.money = read_int(*root, {"player", "state", "money"});
        state.equipment_value = read_int(*root, {"player", "state", "equip_value"});
        state.flashed = read_int(*root, {"player", "state", "flashed"});
        state.smoked = read_int(*root, {"player", "state", "smoked"});
        state.burning = read_int(*root, {"player", "state", "burning"});
        state.round_kills = read_int(*root, {"player", "state", "round_kills"});
        state.round_headshot_kills = read_int(*root, {"player", "state", "round_killhs"});
        state.kills = read_int(*root, {"player", "match_stats", "kills"});
        state.assists = read_int(*root, {"player", "match_stats", "assists"});
        state.deaths = read_int(*root, {"player", "match_stats", "deaths"});
        state.mvps = read_int(*root, {"player", "match_stats", "mvps"});
        state.score = read_int(*root, {"player", "match_stats", "score"});
    }

    return state;
}

} // namespace cspromator
