#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Core/ScriptParser.hpp — .script format parser built on SDL3pp_dataScripts
//
// Architecture:
//   1. GameScriptDocument  (extends SDL::DataDocument)
//        Parses the custom .script format and builds a SDL::ObjectDataNode
//        tree. Registered with SDL::DataScriptFactory for ".script"/".conf".
//
//   2. Bridge  (detail::BridgeValue / detail::BridgeSection)
//        Converts the SDL DataNode tree to ScriptSection / ScriptValue, the
//        lightweight, game-friendly typed-value API used by MapLoader,
//        EntityBuilder and GameState.
//
// Public surface:
//   core::ParseConfFile(path) → ScriptSectionPtr
//   core::ScriptValue  — IsNull/IsSection/IsRect + As…/Get(key)
//   core::ScriptSection — count/operator[]/find/range-for
// ─────────────────────────────────────────────────────────────────────────────

#include <SDL3pp/SDL3pp_dataScripts.h>

#include <cctype>
#include <fstream>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace core {

struct ScriptSection;
using ScriptSectionPtr = std::shared_ptr<ScriptSection>;

/// Axis-aligned rectangle stored as four floats.
struct ScriptRect { float x = 0.f, y = 0.f, w = 0.f, h = 0.f; };

// ─────────────────────────────────────────────────────────────────────────────
// ScriptValue — typed wrapper over a DataNode value
// ─────────────────────────────────────────────────────────────────────────────

class ScriptValue {
public:
    using Var = std::variant<
        std::monostate,     // Null
        bool,
        int,
        float,
        std::string,
        ScriptRect,
        ScriptSectionPtr
    >;

    ScriptValue()                          noexcept = default;
    explicit ScriptValue(bool v)           noexcept : m_v(v) {}
    explicit ScriptValue(int  v)           noexcept : m_v(v) {}
    explicit ScriptValue(float v)          noexcept : m_v(v) {}
    explicit ScriptValue(std::string v)             : m_v(std::move(v)) {}
    explicit ScriptValue(ScriptRect v)     noexcept : m_v(v) {}
    explicit ScriptValue(ScriptSectionPtr v)        : m_v(std::move(v)) {}

    [[nodiscard]] bool IsNull()    const noexcept { return std::holds_alternative<std::monostate>(m_v); }
    [[nodiscard]] bool IsBool()    const noexcept { return std::holds_alternative<bool>(m_v); }
    [[nodiscard]] bool IsInt()     const noexcept { return std::holds_alternative<int>(m_v); }
    [[nodiscard]] bool IsFloat()   const noexcept { return std::holds_alternative<float>(m_v)
                                                        || std::holds_alternative<int>(m_v); }
    [[nodiscard]] bool IsString()  const noexcept { return std::holds_alternative<std::string>(m_v); }
    [[nodiscard]] bool IsRect()    const noexcept { return std::holds_alternative<ScriptRect>(m_v); }
    [[nodiscard]] bool IsSection() const noexcept {
        if (auto* p = std::get_if<ScriptSectionPtr>(&m_v)) return *p != nullptr;
        return false;
    }

    [[nodiscard]] int AsInt(int def = 0) const noexcept {
        if (auto* p = std::get_if<int>(&m_v))   return *p;
        if (auto* p = std::get_if<float>(&m_v)) return static_cast<int>(*p);
        if (auto* p = std::get_if<bool>(&m_v))  return *p ? 1 : 0;
        return def;
    }
    [[nodiscard]] float AsFloat(float def = 0.f) const noexcept {
        if (auto* p = std::get_if<float>(&m_v)) return *p;
        if (auto* p = std::get_if<int>(&m_v))   return static_cast<float>(*p);
        if (auto* p = std::get_if<bool>(&m_v))  return *p ? 1.f : 0.f;
        return def;
    }
    [[nodiscard]] bool AsBool(bool def = false) const noexcept {
        if (auto* p = std::get_if<bool>(&m_v))  return *p;
        if (auto* p = std::get_if<int>(&m_v))   return *p != 0;
        if (auto* p = std::get_if<float>(&m_v)) return *p != 0.f;
        return def;
    }
    [[nodiscard]] std::string AsString(std::string def = "") const {
        if (auto* p = std::get_if<std::string>(&m_v)) return *p;
        return def;
    }
    [[nodiscard]] ScriptRect AsRect() const noexcept {
        if (auto* p = std::get_if<ScriptRect>(&m_v)) return *p;
        return {};
    }
    [[nodiscard]] ScriptSectionPtr AsSection() const noexcept {
        if (auto* p = std::get_if<ScriptSectionPtr>(&m_v)) return *p;
        return nullptr;
    }

    /// Navigate a sub-key when this value is a section.
    [[nodiscard]] ScriptValue Get(const std::string& key) const noexcept;

private:
    Var m_v;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScriptSection — insertion-ordered key→value map
// ─────────────────────────────────────────────────────────────────────────────

struct ScriptSection {
    using Pair    = std::pair<std::string, ScriptValue>;
    using Entries = std::vector<Pair>;
    using Index   = std::map<std::string, std::size_t>;

    Entries entries;
    Index   index;

    void Set(std::string key, ScriptValue val) {
        auto it = index.find(key);
        if (it != index.end()) {
            entries[it->second].second = std::move(val);
        } else {
            index.emplace(key, entries.size());
            entries.emplace_back(std::move(key), std::move(val));
        }
    }

    [[nodiscard]] std::size_t count(const std::string& key) const noexcept {
        return index.count(key);
    }
    ScriptValue& operator[](const std::string& key) {
        auto it = index.find(key);
        if (it == index.end()) {
            index.emplace(key, entries.size());
            entries.emplace_back(key, ScriptValue{});
            return entries.back().second;
        }
        return entries[it->second].second;
    }
    [[nodiscard]] const ScriptValue& operator[](const std::string& key) const noexcept {
        auto it = index.find(key);
        if (it == index.end()) { static const ScriptValue kNull; return kNull; }
        return entries[it->second].second;
    }

    [[nodiscard]] Entries::iterator       begin()        noexcept { return entries.begin(); }
    [[nodiscard]] Entries::iterator       end()          noexcept { return entries.end();   }
    [[nodiscard]] Entries::const_iterator begin()  const noexcept { return entries.begin(); }
    [[nodiscard]] Entries::const_iterator end()    const noexcept { return entries.end();   }
    [[nodiscard]] Entries::const_iterator cbegin() const noexcept { return entries.cbegin();}
    [[nodiscard]] Entries::const_iterator cend()   const noexcept { return entries.cend();  }

    [[nodiscard]] Entries::iterator find(const std::string& key) noexcept {
        auto it = index.find(key);
        return (it == index.end()) ? entries.end()
            : entries.begin() + static_cast<std::ptrdiff_t>(it->second);
    }
    [[nodiscard]] Entries::const_iterator find(const std::string& key) const noexcept {
        auto it = index.find(key);
        return (it == index.end()) ? entries.cend()
            : entries.cbegin() + static_cast<std::ptrdiff_t>(it->second);
    }
};

inline ScriptValue ScriptValue::Get(const std::string& key) const noexcept {
    if (auto* p = std::get_if<ScriptSectionPtr>(&m_v))
        if (*p) return (**p)[key];
    return ScriptValue{};
}

// ─────────────────────────────────────────────────────────────────────────────
// GameScript parser + DataDocument
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

// Sentinel keys used to tag rect objects inside the DataNode tree
static constexpr const char* kRectTag = "__rect";
static constexpr const char* kRectX   = "__x";
static constexpr const char* kRectY   = "__y";
static constexpr const char* kRectW   = "__w";
static constexpr const char* kRectH   = "__h";

// ── Low-level char-stream parser that writes into ObjectDataNode ──────────────

class GameScriptParser {
public:
    explicit GameScriptParser(std::string src) noexcept : m_src(std::move(src)) {}

    void ParseInto(SDL::ObjectDataNode& root) { _ParseBody(root, '\0'); }

private:
    std::string m_src;
    std::size_t m_pos  = 0;
    int         m_line = 1;

    [[nodiscard]] char _Peek() const noexcept {
        return m_pos < m_src.size() ? m_src[m_pos] : '\0';
    }
    char _Get() noexcept {
        char c = _Peek();
        if (c == '\n') ++m_line;
        if (c) ++m_pos;
        return c;
    }
    bool _At(char c) const noexcept { return _Peek() == c; }

    void _SkipWS() noexcept {
        while (m_pos < m_src.size()) {
            char c = _Peek();
            if (c == ' ' || c == '\t' || c == '\r') { ++m_pos; }
            else if (c == '\n') { ++m_line; ++m_pos; }
            else if (c == '#') { while (m_pos < m_src.size() && _Peek() != '\n') ++m_pos; }
            else break;
        }
    }
    bool _Expect(char c) noexcept { _SkipWS(); if (_At(c)) { _Get(); return true; } return false; }

    // ── Key: identifier | %N | (x,y)→"x_y" ──────────────────────────────────
    std::string _ParseKey() {
        _SkipWS();
        if (m_pos >= m_src.size()) return "";
        char c = _Peek();

        if (c == '%') {
            std::string k; k += _Get();
            while (m_pos < m_src.size() && std::isdigit(_Peek())) k += _Get();
            return k;
        }
        if (c == '(') {
            _Get();
            std::string xs, ys;
            while (m_pos < m_src.size() && _Peek() != ',' && _Peek() != ')') xs += _Get();
            if (_At(',')) _Get();
            while (m_pos < m_src.size() && _Peek() != ')') ys += _Get();
            if (_At(')')) _Get();
            auto trim = [](const std::string& s) {
                auto a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
                return (a == std::string::npos) ? std::string{} : s.substr(a, b - a + 1);
            };
            return trim(xs) + "_" + trim(ys);
        }
        if (std::isalpha(c) || c == '_') {
            std::string k;
            while (m_pos < m_src.size() && (std::isalnum(_Peek()) || _Peek() == '_')) k += _Get();
            return k;
        }
        return "";
    }

    // ── Rect args helper (handles 'f' suffix per value) ───────────────────────
    static std::shared_ptr<SDL::ObjectDataNode> _BuildRect(const std::string& args) {
        auto obj = SDL::ObjectDataNode::Build();
        obj->set(kRectTag, SDL::BoolDataNode::Build(true));
        float vals[4] = {};
        int idx = 0;
        std::istringstream ss(args);
        std::string tok;
        while (idx < 4 && std::getline(ss, tok, ',')) {
            auto a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
            if (a != std::string::npos) tok = tok.substr(a, b - a + 1);
            if (!tok.empty() && (tok.back() == 'f' || tok.back() == 'F')) tok.pop_back();
            try { vals[idx] = std::stof(tok); } catch (...) {}
            ++idx;
        }
        static const char* keys[4] = { kRectX, kRectY, kRectW, kRectH };
        for (int i = 0; i < 4; ++i) obj->set(keys[i], SDL::F32DataNode::Build(vals[i]));
        return obj;
    }

    // ── Value ─────────────────────────────────────────────────────────────────
    std::shared_ptr<SDL::DataNode> _ParseValue() {
        _SkipWS();
        char c = _Peek();

        if (c == '{') {
            _Get();
            auto obj = SDL::ObjectDataNode::Build();
            _ParseBody(*obj, '}');
            return obj;
        }
        if (c == '[') {
            _Get();
            return _ParseArray();
        }
        if (c == '"') return SDL::StringDataNode::Build(_ParseString());

        return _ParseSimple();
    }

    std::string _ParseString() {
        _Get(); // '"'
        std::string s;
        while (m_pos < m_src.size()) {
            char c = _Get();
            if (c == '"') break;
            if (c == '\\' && m_pos < m_src.size()) {
                char esc = _Get();
                switch (esc) {
                    case 'n': s += '\n'; break; case 't': s += '\t'; break;
                    case '"': s += '"';  break; case '\\':s += '\\'; break;
                    default:  s += '\\'; s += esc; break;
                }
                continue;
            }
            s += c;
        }
        return s;
    }

    std::shared_ptr<SDL::DataNode> _ParseSimple() {
        _SkipWS();

        // rect / frect — peek for identifier then '('
        {
            std::size_t savedPos  = m_pos;
            int         savedLine = m_line;
            std::string ident;
            while (m_pos < m_src.size() && std::isalpha(_Peek())) ident += _Get();
            if (!ident.empty() && _At('(')
                    && (ident == "rect" || ident == "frect")) {
                _Get(); // '('
                std::string args;
                int depth = 1;
                while (m_pos < m_src.size() && depth > 0) {
                    char ch = _Get();
                    if (ch == '(') { ++depth; args += ch; }
                    else if (ch == ')') { --depth; if (depth > 0) args += ch; }
                    else args += ch;
                }
                return _BuildRect(args);
            }
            m_pos = savedPos; m_line = savedLine;
        }

        // Plain token
        std::string tok;
        while (m_pos < m_src.size()) {
            char ch = _Peek();
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
                    || ch == '=' || ch == '{' || ch == '}' || ch == ']'
                    || ch == ';' || ch == '#')
                break;
            tok += _Get();
        }
        if (tok.empty()) return SDL::NoneDataNode::Build();

        if (tok == "true"  || tok == "yes") return SDL::BoolDataNode::Build(true);
        if (tok == "false" || tok == "no")  return SDL::BoolDataNode::Build(false);

        // Float with 'f' suffix
        if (tok.back() == 'f' || tok.back() == 'F') {
            std::string num(tok.begin(), tok.end() - 1);
            try { return SDL::F32DataNode::Build(std::stof(num)); } catch (...) {}
        }
        // Float with '.'
        if (tok.find('.') != std::string::npos) {
            try { return SDL::F32DataNode::Build(std::stof(tok)); } catch (...) {}
        }
        // Integer
        try { return SDL::S32DataNode::Build(std::stoi(tok)); } catch (...) {}

        return SDL::StringDataNode::Build(tok);
    }

    // ── Body (key=value pairs) ────────────────────────────────────────────────
    void _ParseBody(SDL::ObjectDataNode& node, char terminator) {
        while (true) {
            _SkipWS();
            if (m_pos >= m_src.size()) break;
            if (terminator != '\0' && _At(terminator)) { _Get(); break; }

            std::string key = _ParseKey();
            if (key.empty()) { while (m_pos < m_src.size() && _Peek() != '\n') _Get(); continue; }

            _SkipWS();
            if (!_Expect('=')) { while (m_pos < m_src.size() && _Peek() != '\n') _Get(); continue; }

            node.set(key, _ParseValue());
        }
    }

    // ── Array (anonymous items) ───────────────────────────────────────────────
    std::shared_ptr<SDL::ArrayDataNode> _ParseArray() {
        auto arr = SDL::ArrayDataNode::Build();
        int idx = 0;
        while (true) {
            _SkipWS();
            if (m_pos >= m_src.size() || _At(']')) { if (_At(']')) _Get(); break; }
            if (_At('{')) {
                _Get();
                auto sub = SDL::ObjectDataNode::Build();
                _ParseBody(*sub, '}');
                arr->add(sub);
            } else if (_At('"')) {
                arr->add(SDL::StringDataNode::Build(_ParseString()));
            } else {
                arr->add(_ParseSimple());
            }
            (void)idx++;
        }
        return arr;
    }
};

// ── DataDocument subclass registered with SDL::DataScriptFactory ──────────────

class GameScriptDocument final : public SDL::DataDocument {
public:
    GameScriptDocument() = default;

    [[nodiscard]] std::string encode() const override { return ""; }
    [[nodiscard]] std::string stringifyNode(
            std::shared_ptr<SDL::DataNode> n) const override {
        return SDL::NodeSerializer::toScalarString(n);
    }

protected:
    std::optional<SDL::DataParseError> decodeImpl(std::istream& is) override {
        std::string src(std::istreambuf_iterator<char>(is), {});
        auto root = SDL::ObjectDataNode::Build();
        setRoot(root);
        try {
            GameScriptParser parser(std::move(src));
            parser.ParseInto(*root);
        } catch (const std::exception& e) {
            return SDL::DataParseError{std::string("GameScript: ") + e.what()};
        }
        return std::nullopt;
    }
};

// Auto-register at program start (same pattern as the built-in formats)
inline bool _registerGameScript() {
    SDL::DataScriptFactory::instance().registerFormat(
        "gamescript", {".script", ".conf"},
        [] { return std::make_shared<GameScriptDocument>(); });
    return true;
}
inline const bool kGameScriptRegistered = _registerGameScript();

// ── Bridge: DataNode tree → ScriptSection / ScriptValue ──────────────────────

inline ScriptValue     BridgeValue  (const std::shared_ptr<SDL::DataNode>&        n);
inline ScriptSectionPtr BridgeSection(const std::shared_ptr<SDL::ObjectDataNode>& n);

inline ScriptValue BridgeValue(const std::shared_ptr<SDL::DataNode>& n) {
    if (!n) return ScriptValue{};
    using DT = SDL::DataNodeType;
    switch (n->getType()) {
        case DT::NONE:    return ScriptValue{};
        case DT::BOOLEAN: return ScriptValue(std::dynamic_pointer_cast<SDL::BoolDataNode>(n)->getValue());
        case DT::S8:      return ScriptValue((int)std::dynamic_pointer_cast<SDL::S8DataNode>(n)->getValue());
        case DT::U8:      return ScriptValue((int)std::dynamic_pointer_cast<SDL::U8DataNode>(n)->getValue());
        case DT::S16:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::S16DataNode>(n)->getValue());
        case DT::U16:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::U16DataNode>(n)->getValue());
        case DT::S32:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::S32DataNode>(n)->getValue());
        case DT::U32:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::U32DataNode>(n)->getValue());
        case DT::S64:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::S64DataNode>(n)->getValue());
        case DT::U64:     return ScriptValue((int)std::dynamic_pointer_cast<SDL::U64DataNode>(n)->getValue());
        case DT::F32:     return ScriptValue(std::dynamic_pointer_cast<SDL::F32DataNode>(n)->getValue());
        case DT::F64:     return ScriptValue((float)std::dynamic_pointer_cast<SDL::F64DataNode>(n)->getValue());
        case DT::STRING:  return ScriptValue(std::dynamic_pointer_cast<SDL::StringDataNode>(n)->getValue());
        case DT::ARRAY: {
            auto arr = std::dynamic_pointer_cast<SDL::ArrayDataNode>(n);
            auto sec = std::make_shared<ScriptSection>();
            for (std::size_t i = 0; i < arr->getSize(); ++i)
                sec->Set(std::to_string(i), BridgeValue(arr->get(i)));
            return ScriptValue(sec);
        }
        case DT::OBJECT: {
            auto obj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(n);
            // Rect objects carry a "__rect" sentinel bool
            if (obj->has(kRectTag)) {
                auto getF = [&](const char* k) -> float {
                    auto sub = obj->get(k);
                    if (!sub) return 0.f;
                    if (auto p = std::dynamic_pointer_cast<SDL::F32DataNode>(sub)) return p->getValue();
                    if (auto p = std::dynamic_pointer_cast<SDL::S32DataNode>(sub)) return (float)p->getValue();
                    return 0.f;
                };
                return ScriptValue(ScriptRect{getF(kRectX), getF(kRectY), getF(kRectW), getF(kRectH)});
            }
            return ScriptValue(BridgeSection(obj));
        }
        default: return ScriptValue{};
    }
}

inline ScriptSectionPtr BridgeSection(const std::shared_ptr<SDL::ObjectDataNode>& n) {
    auto sec = std::make_shared<ScriptSection>();
    if (!n) return sec;
    for (const auto& [k, v] : n->getValues())
        sec->Set(k, BridgeValue(v));
    return sec;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

/// Parse a .script config file via the registered GameScriptDocument.
/// Throws std::runtime_error on open failure or parse error.
[[nodiscard]] inline ScriptSectionPtr ParseConfFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("ScriptParser: cannot open '" + path + "'");

    std::string src(std::istreambuf_iterator<char>(f), {});

    detail::GameScriptDocument doc;
    if (auto err = doc.decodeStr(src))
        throw std::runtime_error("ScriptParser: " + err->format() + " in '" + path + "'");

    return detail::BridgeSection(doc.getRoot());
}

} // namespace core
