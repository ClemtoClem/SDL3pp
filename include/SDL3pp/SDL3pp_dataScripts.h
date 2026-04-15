/**
 * @file SDL3pp_dataScripts.h
 * @brief SDL3pp — DataScripts Module
 *
 * Unified API to parse and generate data scripts:
 * JSON · XML · YAML · INI · TOML · CSV
 *
 * The data tree relies on a typed node model (ObjectNode,
 * ArrayNode, scalars) independent of the format. This allows reading an
 * XML file and exporting it to JSON, or reading YAML and writing
 * to INI, etc.
 *
 * CSV Constraint: CSV export/import only supports a single-level
 * hierarchy — a root ObjectNode where each value is an
 * ArrayNode (named columns).
 *
 * I/O relies on SDL::IOStreamRef (SDL3pp_iostream.h).
 *
 * @code
 * // Convert an XML file to JSON
 * auto src  = SDL::IOStream::FromFile("config.xml",  "r");
 * auto dst  = SDL::IOStream::FromFile("config.json", "w");
 * auto err  = SDL::ConvertDataScript(src, "xml", dst, "json");
 * @endcode
 */

#ifndef SDL3PP_DATASCRIPTS_H_
#define SDL3PP_DATASCRIPTS_H_

#include "SDL3pp_stdinc.h"
#include "SDL3pp_iostream.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace SDL
{

    /**
     * @defgroup CategoryDataScripts Data Scripts
     *
     * API to parse and generate data scripts.
     *
     * Supported formats: JSON · XML · YAML · INI · TOML · CSV
     *
     * The data structure is represented by a tree of typed nodes.
     * This normalized intermediate representation allows exporting the
     * data into any supported format.
     *
     * @{
     */

    // ========================================================================= //
    //  Internal section: bridge SDL::IOStreamRef ↔ std::streambuf               //
    // ========================================================================= //

    namespace detail
    {

        /// @brief Read-only std::streambuf delegating to SDL::IOStreamRef.
        class IOStreamReadBuf final : public std::streambuf
        {
        public:
            explicit IOStreamReadBuf(IOStreamRef io) : _io(std::move(io)) {}

        protected:
            int_type underflow() override
            {
                if (gptr() < egptr())
                    return traits_type::to_int_type(*gptr());
                auto n = _io.Read(SDL::TargetBytes{_buf, sizeof(_buf)});
                if (n == 0)
                    return traits_type::eof();
                setg(_buf, _buf, _buf + n);
                return traits_type::to_int_type(*gptr());
            }

        private:
            IOStreamRef _io;
            char _buf[4096]{};
        };

        /// @brief Write-only std::streambuf delegating to SDL::IOStreamRef.
        class IOStreamWriteBuf final : public std::streambuf
        {
        public:
            explicit IOStreamWriteBuf(IOStreamRef io) : _io(std::move(io)) {}

            ~IOStreamWriteBuf() override { sync(); }

        protected:
            int_type overflow(int_type ch) override
            {
                if (ch != traits_type::eof())
                {
                    _buf += traits_type::to_char_type(ch);
                    if (_buf.size() >= 4096)
                        return sync() == 0 ? ch : traits_type::eof();
                }
                return ch;
            }

            std::streamsize xsputn(const char *s, std::streamsize n) override
            {
                _buf.append(s, static_cast<size_t>(n));
                if (_buf.size() >= 4096)
                    sync();
                return n;
            }

            int sync() override
            {
                if (_buf.empty())
                    return 0;
                auto written = _io.Write(SDL::SourceBytes{_buf.data(), _buf.size()});
                _buf.clear();
                return (written > 0) ? 0 : -1;
            }

        private:
            IOStreamRef _io;
            std::string _buf;
        };

    } // namespace detail

    // ========================================================================= //
    //  LinkedMap — map preserving insertion order                               //
    // ========================================================================= //

    /// @brief Dictionary ordered by insertion order (amortized O(1)).
    template <typename K, typename V>
    class LinkedMap
    {
        using Pair = std::pair<K, V>;
        using List = std::list<Pair>;
        using ListIt = typename List::iterator;

        List _list;
        std::unordered_map<K, ListIt> _index;

    public:
        using iterator = typename List::iterator;
        using const_iterator = typename List::const_iterator;

        void insert(const K &key, const V &value)
        {
            auto it = _index.find(key);
            if (it == _index.end())
            {
                _list.emplace_back(key, value);
                _index[key] = std::prev(_list.end());
            }
            else
            {
                it->second->second = value;
            }
        }

        V &operator[](const K &key)
        {
            auto it = _index.find(key);
            if (it == _index.end())
            {
                _list.emplace_back(key, V{});
                _index[key] = std::prev(_list.end());
                return _list.back().second;
            }
            return it->second->second;
        }

        bool erase(const K &key)
        {
            auto it = _index.find(key);
            if (it == _index.end())
                return false;
            _list.erase(it->second);
            _index.erase(it);
            return true;
        }

        iterator find(const K &key)
        {
            auto it = _index.find(key);
            return (it != _index.end()) ? it->second : _list.end();
        }
        const_iterator find(const K &key) const
        {
            auto it = _index.find(key);
            return (it != _index.end()) ? it->second : _list.cend();
        }

        size_t count(const K &key) const { return _index.count(key); }
        size_t size() const { return _list.size(); }
        bool empty() const { return _list.empty(); }

        iterator begin() { return _list.begin(); }
        iterator end() { return _list.end(); }
        const_iterator begin() const { return _list.begin(); }
        const_iterator end() const { return _list.end(); }
        const_iterator cbegin() const { return _list.cbegin(); }
        const_iterator cend() const { return _list.cend(); }
    };

    // ========================================================================= //
    //  NodeType                                                                  //
    // ========================================================================= //

    /// @brief Type of a data node.
    enum class DataNodeType
    {
        NONE,    ///< Null / missing value
        ARRAY,   ///< Array of nodes
        OBJECT,  ///< Key→node dictionary (order preserved)
        STRING,  ///< UTF-8 String
        BOOLEAN, ///< Boolean
        S8,
        U8,
        S16,
        U16,
        S32,
        U32,
        S64,
        U64, ///< Integers
        F32,
        F64 ///< Floats
    };

    // ========================================================================= //
    //  Forward declarations                                                      //
    // ========================================================================= //

    class DataDocument;
    class ObjectDataNode;

    // ========================================================================= //
    //  DataNode — RAII base class                                                //
    // ========================================================================= //

    /// @brief Data node — polymorphic base class.
    class DataNode : public std::enable_shared_from_this<DataNode>
    {
    public:
        friend class DataDocument;

        explicit DataNode(DataNodeType type) : _type(type) {}
        virtual ~DataNode() = default;

        DataNode(const DataNode &) = delete;
        DataNode &operator=(const DataNode &) = delete;

        /// Type of the node.
        DataNodeType getType() const noexcept { return _type; }

        /// Serializes this node via the document it is linked to.
        std::string toString() const;

        /// Deep copy of the subtree.
        [[nodiscard]] virtual std::shared_ptr<DataNode> clone() const = 0;

        void setParent(std::shared_ptr<DataNode> parent) { _parent = std::move(parent); }
        std::shared_ptr<DataNode> getParent() const { return _parent.lock(); }

        virtual void link(std::weak_ptr<DataDocument> doc) { _doc = std::move(doc); }

    protected:
        DataNodeType _type;
        std::weak_ptr<DataNode> _parent;
        std::weak_ptr<DataDocument> _doc;
    };

    // ========================================================================= //
    //  NoneDataNode                                                              //
    // ========================================================================= //

    /// @brief Node representing a null value.
    class NoneDataNode final : public DataNode
    {
    public:
        static std::shared_ptr<NoneDataNode> Make() { return std::make_shared<NoneDataNode>(); }
        NoneDataNode() : DataNode(DataNodeType::NONE) {}
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Make(); }
    };

    // ========================================================================= //
    //  ValueDataNode<T>                                                          //
    // ========================================================================= //

    template <typename T>
    class ValueDataNode : public DataNode
    {
    public:
        ValueDataNode(DataNodeType type, T value) : DataNode(type), _value(std::move(value)) {}

        virtual bool parse(const std::string &text) = 0;

        T &getValue() noexcept { return _value; }
        const T &getValue() const noexcept { return _value; }
        void setValue(T v) { _value = std::move(v); }

    protected:
        T _value;
    };

    // ========================================================================= //
    //  BoolDataNode / StringDataNode                                             //
    // ========================================================================= //

    class BoolDataNode final : public ValueDataNode<bool>
    {
    public:
        static std::shared_ptr<BoolDataNode> Build(bool v = false) { return std::make_shared<BoolDataNode>(v); }
        explicit BoolDataNode(bool v = false) : ValueDataNode(DataNodeType::BOOLEAN, v) {}
        bool parse(const std::string &text) override
        {
            std::string up = text;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            if (up == "TRUE")
            {
                _value = true;
                return true;
            }
            if (up == "FALSE")
            {
                _value = false;
                return true;
            }
            return false;
        }
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Build(_value); }
    };

    class StringDataNode final : public ValueDataNode<std::string>
    {
    public:
        static std::shared_ptr<StringDataNode> Build(std::string v = {}) { return std::make_shared<StringDataNode>(std::move(v)); }
        explicit StringDataNode(std::string v = {}) : ValueDataNode(DataNodeType::STRING, std::move(v)) {}
        bool parse(const std::string &text) override
        {
            _value = text;
            return true;
        }
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Build(_value); }
    };

    // ========================================================================= //
    //  Numeric nodes                                                            //
    // ========================================================================= //

    namespace detail
    {

        inline bool strToLong(const std::string &s, long &r) noexcept
        {
            char *end;
            r = std::strtol(s.c_str(), &end, 0);
            return *end == '\0' && end != s.c_str();
        }
        inline bool strToULong(const std::string &s, unsigned long &r) noexcept
        {
            char *end;
            r = std::strtoul(s.c_str(), &end, 0);
            return *end == '\0' && end != s.c_str();
        }

    } // namespace detail

#define SDL3PP_DS_INT_NODE(ClassName, CppType, TypeEnum)                                         \
    class ClassName final : public ValueDataNode<CppType>                                        \
    {                                                                                            \
    public:                                                                                      \
        static std::shared_ptr<ClassName> Build(CppType v = 0)                                   \
        {                                                                                        \
            return std::make_shared<ClassName>(v);                                               \
        }                                                                                        \
        explicit ClassName(CppType v = 0) : ValueDataNode(DataNodeType::TypeEnum, v) {}          \
        bool parse(const std::string &t) override                                                \
        {                                                                                        \
            long r;                                                                              \
            if (!detail::strToLong(t, r))                                                        \
                return false;                                                                    \
            _value = static_cast<CppType>(r);                                                    \
            return true;                                                                         \
        }                                                                                        \
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Build(_value); } \
    };

#define SDL3PP_DS_UINT_NODE(ClassName, CppType, TypeEnum)                                        \
    class ClassName final : public ValueDataNode<CppType>                                        \
    {                                                                                            \
    public:                                                                                      \
        static std::shared_ptr<ClassName> Build(CppType v = 0)                                   \
        {                                                                                        \
            return std::make_shared<ClassName>(v);                                               \
        }                                                                                        \
        explicit ClassName(CppType v = 0) : ValueDataNode(DataNodeType::TypeEnum, v) {}          \
        bool parse(const std::string &t) override                                                \
        {                                                                                        \
            unsigned long r;                                                                     \
            if (!detail::strToULong(t, r))                                                       \
                return false;                                                                    \
            _value = static_cast<CppType>(r);                                                    \
            return true;                                                                         \
        }                                                                                        \
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Build(_value); } \
    };

#define SDL3PP_DS_FLOAT_NODE(ClassName, CppType, TypeEnum, StdFn)                                \
    class ClassName final : public ValueDataNode<CppType>                                        \
    {                                                                                            \
    public:                                                                                      \
        static std::shared_ptr<ClassName> Build(CppType v = 0)                                   \
        {                                                                                        \
            return std::make_shared<ClassName>(v);                                               \
        }                                                                                        \
        explicit ClassName(CppType v = 0) : ValueDataNode(DataNodeType::TypeEnum, v) {}          \
        bool parse(const std::string &t) override                                                \
        {                                                                                        \
            try                                                                                  \
            {                                                                                    \
                _value = StdFn(t);                                                               \
                return true;                                                                     \
            }                                                                                    \
            catch (...)                                                                          \
            {                                                                                    \
                return false;                                                                    \
            }                                                                                    \
        }                                                                                        \
        [[nodiscard]] std::shared_ptr<DataNode> clone() const override { return Build(_value); } \
    };

    SDL3PP_DS_INT_NODE(S8DataNode, int8_t, S8)
    SDL3PP_DS_UINT_NODE(U8DataNode, uint8_t, U8)
    SDL3PP_DS_INT_NODE(S16DataNode, int16_t, S16)
    SDL3PP_DS_UINT_NODE(U16DataNode, uint16_t, U16)
    SDL3PP_DS_INT_NODE(S32DataNode, int32_t, S32)
    SDL3PP_DS_UINT_NODE(U32DataNode, uint32_t, U32)
    SDL3PP_DS_INT_NODE(S64DataNode, int64_t, S64)
    SDL3PP_DS_UINT_NODE(U64DataNode, uint64_t, U64)
    SDL3PP_DS_FLOAT_NODE(F32DataNode, float, F32, std::stof)
    SDL3PP_DS_FLOAT_NODE(F64DataNode, double, F64, std::stod)

#undef SDL3PP_DS_INT_NODE
#undef SDL3PP_DS_UINT_NODE
#undef SDL3PP_DS_FLOAT_NODE

    // ========================================================================= //
    //  ArrayDataNode                                                             //
    // ========================================================================= //

    /// @brief Array node (ordered list of DataNodes).
    class ArrayDataNode : public DataNode
    {
    public:
        static std::shared_ptr<ArrayDataNode> Make() { return std::make_shared<ArrayDataNode>(); }
        ArrayDataNode() : DataNode(DataNodeType::ARRAY) {}

        [[nodiscard]] std::shared_ptr<DataNode> clone() const override
        {
            auto c = std::make_shared<ArrayDataNode>();
            for (const auto &ch : _array)
                c->add(ch->clone());
            return c;
        }
        void link(std::weak_ptr<DataDocument> doc) override
        {
            DataNode::link(doc);
            for (auto &ch : _array)
                if (ch)
                    ch->link(doc);
        }

        size_t getSize() const noexcept { return _array.size(); }
        const std::vector<std::shared_ptr<DataNode>> &getValues() const noexcept { return _array; }

        std::shared_ptr<DataNode> pop()
        {
            if (_array.empty())
                return nullptr;
            auto last = _array.back();
            _array.pop_back();
            return last;
        }
        std::shared_ptr<ArrayDataNode> add(std::shared_ptr<DataNode> node);
        std::shared_ptr<DataNode> get(size_t index) const
        {
            return (index < _array.size()) ? _array[index] : nullptr;
        }
        std::shared_ptr<ArrayDataNode> set(size_t index, std::shared_ptr<DataNode> node);
        std::shared_ptr<ArrayDataNode> cut(size_t begin, size_t end) const;
        std::shared_ptr<ArrayDataNode> invert() const;

    private:
        std::vector<std::shared_ptr<DataNode>> _array;
    };

    // ========================================================================= //
    //  ObjectDataNode                                                            //
    // ========================================================================= //

    /// @brief Object node (key→node dictionary, insertion order preserved).
    class ObjectDataNode : public DataNode
    {
    public:
        static std::shared_ptr<ObjectDataNode> Make() { return std::make_shared<ObjectDataNode>(); }
        ObjectDataNode() : DataNode(DataNodeType::OBJECT) {}

        [[nodiscard]] std::shared_ptr<DataNode> clone() const override
        {
            auto c = std::make_shared<ObjectDataNode>();
            for (const auto &p : _map)
                c->set(p.first, p.second->clone());
            return c;
        }
        void link(std::weak_ptr<DataDocument> doc) override
        {
            DataNode::link(doc);
            for (auto &p : _map)
                if (p.second)
                    p.second->link(doc);
        }

        const LinkedMap<std::string, std::shared_ptr<DataNode>> &getValues() const noexcept { return _map; }

        std::shared_ptr<ObjectDataNode> set(const std::string &key, std::shared_ptr<DataNode> value);
        std::shared_ptr<DataNode> get(const std::string &key) const
        {
            auto it = _map.find(key);
            return (it != _map.end()) ? it->second : nullptr;
        }
        bool has(const std::string &key) const noexcept { return _map.count(key) > 0; }
        void remove(const std::string &key) { _map.erase(key); }

    private:
        LinkedMap<std::string, std::shared_ptr<DataNode>> _map;
    };

    // ========================================================================= //
    //  Inline implementations of nodes                                          //
    // ========================================================================= //

    inline std::shared_ptr<ArrayDataNode>
    ArrayDataNode::add(std::shared_ptr<DataNode> node)
    {
        node->setParent(shared_from_this());
        if (auto doc = _doc.lock())
            node->link(doc);
        _array.push_back(std::move(node));
        return std::dynamic_pointer_cast<ArrayDataNode>(shared_from_this());
    }

    inline std::shared_ptr<ArrayDataNode>
    ArrayDataNode::set(size_t index, std::shared_ptr<DataNode> node)
    {
        if (index < _array.size())
        {
            node->setParent(shared_from_this());
            if (auto doc = _doc.lock())
                node->link(doc);
            _array[index] = std::move(node);
        }
        return std::dynamic_pointer_cast<ArrayDataNode>(shared_from_this());
    }

    inline std::shared_ptr<ArrayDataNode>
    ArrayDataNode::cut(size_t begin, size_t end) const
    {
        auto r = std::make_shared<ArrayDataNode>();
        size_t cap = std::min(end, _array.size());
        for (size_t i = begin; i < cap; ++i)
            r->add(_array[i]);
        return r;
    }

    inline std::shared_ptr<ArrayDataNode>
    ArrayDataNode::invert() const
    {
        auto r = std::make_shared<ArrayDataNode>();
        for (size_t i = _array.size(); i > 0; --i)
            r->add(_array[i - 1]);
        return r;
    }

    inline std::shared_ptr<ObjectDataNode>
    ObjectDataNode::set(const std::string &key, std::shared_ptr<DataNode> value)
    {
        value->setParent(shared_from_this());
        if (auto doc = _doc.lock())
            value->link(doc);
        _map[key] = std::move(value);
        return std::dynamic_pointer_cast<ObjectDataNode>(shared_from_this());
    }

    // ========================================================================= //
    //  DataParseError                                                            //
    // ========================================================================= //

    /// @brief Structured parsing error.
    struct DataParseError
    {
        std::string message;
        int line = -1;
        int column = -1;

        explicit DataParseError(std::string msg, int l = -1, int c = -1)
            : message(std::move(msg)), line(l), column(c) {}

        std::string format() const
        {
            if (line >= 0)
                return message + " (line " + std::to_string(line) + ")";
            return message;
        }
    };

    // ========================================================================= //
    //  NodeSerializer — centralized serialization utilities                       //
    // ========================================================================= //

    namespace NodeSerializer
    {

        inline void writeScalar(std::ostream &ss, const std::shared_ptr<DataNode> &node)
        {
            if (!node)
            {
                ss << "null";
                return;
            }
            ss << std::setprecision(std::numeric_limits<double>::max_digits10);
            switch (node->getType())
            {
            case DataNodeType::NONE:
                ss << "null";
                return;
            case DataNodeType::BOOLEAN:
                ss << (std::dynamic_pointer_cast<BoolDataNode>(node)->getValue() ? "true" : "false");
                return;
            case DataNodeType::STRING:
                ss << std::dynamic_pointer_cast<StringDataNode>(node)->getValue();
                return;
            case DataNodeType::S8:
                ss << static_cast<int>(std::dynamic_pointer_cast<S8DataNode>(node)->getValue());
                return;
            case DataNodeType::U8:
                ss << static_cast<unsigned>(std::dynamic_pointer_cast<U8DataNode>(node)->getValue());
                return;
            case DataNodeType::S16:
                ss << std::dynamic_pointer_cast<S16DataNode>(node)->getValue();
                return;
            case DataNodeType::U16:
                ss << std::dynamic_pointer_cast<U16DataNode>(node)->getValue();
                return;
            case DataNodeType::S32:
                ss << std::dynamic_pointer_cast<S32DataNode>(node)->getValue();
                return;
            case DataNodeType::U32:
                ss << std::dynamic_pointer_cast<U32DataNode>(node)->getValue();
                return;
            case DataNodeType::S64:
                ss << std::dynamic_pointer_cast<S64DataNode>(node)->getValue();
                return;
            case DataNodeType::U64:
                ss << std::dynamic_pointer_cast<U64DataNode>(node)->getValue();
                return;
            case DataNodeType::F32:
                ss << std::dynamic_pointer_cast<F32DataNode>(node)->getValue();
                return;
            case DataNodeType::F64:
                ss << std::dynamic_pointer_cast<F64DataNode>(node)->getValue();
                return;
            default:
                throw std::invalid_argument("NodeSerializer::writeScalar: composite type");
            }
        }

        inline std::string toScalarString(const std::shared_ptr<DataNode> &node)
        {
            std::ostringstream ss;
            writeScalar(ss, node);
            return ss.str();
        }

        /// Builds the most precise scalar node for a given string.
        inline std::shared_ptr<DataNode> parseScalar(const std::string &text)
        {
            {
                std::string up = text;
                std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                if (up == "TRUE")
                    return BoolDataNode::Build(true);
                if (up == "FALSE")
                    return BoolDataNode::Build(false);
                if (up == "NULL" || up == "~")
                    return NoneDataNode::Make();
            }
            {
                char *end;
                long v = std::strtol(text.c_str(), &end, 10);
                if (*end == '\0' && end != text.c_str())
                    return S64DataNode::Build(static_cast<int64_t>(v));
            }
            {
                char *end;
                unsigned long v = std::strtoul(text.c_str(), &end, 10);
                if (*end == '\0' && end != text.c_str())
                    return U64DataNode::Build(static_cast<uint64_t>(v));
            }
            {
                char *end;
                double v = std::strtod(text.c_str(), &end);
                if (*end == '\0' && end != text.c_str())
                    return F64DataNode::Build(v);
            }
            return StringDataNode::Build(text);
        }

    } // namespace NodeSerializer

    // ========================================================================= //
    //  DataDocument — RAII base class                                           //
    // ========================================================================= //

    /**
     * @brief Data document — base for all formats.
     *
     * Subclass contract:
     * - `decodeImpl(std::istream&)` → fills the root via `setRoot()`.
     * - `encode() const`           → serializes the root to std::string.
     * - `stringifyNode()`          → serializes an isolated DataNode.
     */
    class DataDocument : public std::enable_shared_from_this<DataDocument>
    {
    public:
        friend class DataNode;

        DataDocument() = default;
        virtual ~DataDocument() = default;

        DataDocument(const DataDocument &) = delete;
        DataDocument &operator=(const DataDocument &) = delete;

        // ------------------------------------------------------------------ //
        //  Decoding                                                            //
        // ------------------------------------------------------------------ //

        /// Decodes from an SDL IOStreamRef.
        std::optional<DataParseError> decode(IOStreamRef io)
        {
            detail::IOStreamReadBuf buf(std::move(io));
            std::istream is(&buf);
            return decodeImpl(is);
        }

        /// Decodes from an in-memory string.
        std::optional<DataParseError> decodeStr(const std::string &content)
        {
            std::istringstream ss(content);
            return decodeImpl(ss);
        }

        // ------------------------------------------------------------------ //
        //  Encoding                                                            //
        // ------------------------------------------------------------------ //

        /// Encodes the root and writes the result to an SDL IOStreamRef.
        bool encode(IOStreamRef io) const
        {
            std::string data = encode();
            if (data.empty())
                return false;
            detail::IOStreamWriteBuf buf(std::move(io));
            std::ostream os(&buf);
            os << data;
            return os.good();
        }

        /// Encodes the root to std::string.
        [[nodiscard]] virtual std::string encode() const = 0;

        // ------------------------------------------------------------------ //
        //  Root                                                                //
        // ------------------------------------------------------------------ //

        std::shared_ptr<ObjectDataNode> getRoot() const noexcept { return _root; }
        void setRoot(std::shared_ptr<ObjectDataNode> node)
        {
            _root = std::move(node);
            if (_root)
                _root->link(weak_from_this());
        }
        void clear() noexcept { _root = nullptr; }

    protected:
        /// Decoding implementation — uses std::istream.
        virtual std::optional<DataParseError> decodeImpl(std::istream &is) = 0;

        /// Serializes an isolated DataNode (used by DataNode::toString()).
        [[nodiscard]] virtual std::string stringifyNode(std::shared_ptr<DataNode> node) const = 0;

    private:
        std::shared_ptr<ObjectDataNode> _root;
    };

    // DataNode::toString() implemented here (depends on DataDocument)
    inline std::string DataNode::toString() const
    {
        auto doc = _doc.lock();
        if (!doc)
            return "<unlinked DataNode>";
        return doc->stringifyNode(std::const_pointer_cast<DataNode>(shared_from_this()));
    }

    // ========================================================================= //
    //  DataScriptFactory                                                         //
    // ========================================================================= //

    /**
     * @brief Registry and factory for document formats (singleton).
     *
     * All built-in formats are auto-registered at program initialization.
     * Custom formats can be added.
     */
    class DataScriptFactory
    {
    public:
        using Creator = std::function<std::shared_ptr<DataDocument>()>;

        struct FormatInfo
        {
            std::string name;
            std::vector<std::string> extensions;
            Creator creator;
        };

        static DataScriptFactory &instance()
        {
            static DataScriptFactory inst;
            return inst;
        }

        DataScriptFactory(const DataScriptFactory &) = delete;
        DataScriptFactory &operator=(const DataScriptFactory &) = delete;

        /// Registers a format (returns true for static initialization).
        bool registerFormat(const std::string &name,
                            std::vector<std::string> extensions,
                            Creator creator)
        {
            FormatInfo info{name, std::move(extensions), std::move(creator)};
            _byName[name] = info;
            for (const auto &ext : info.extensions)
                _byExtension[ext] = info;
            return true;
        }

        /// Creates a document by format name (e.g., "json").
        std::shared_ptr<DataDocument> createByName(const std::string &name) const
        {
            auto it = _byName.find(name);
            return (it != _byName.end()) ? it->second.creator() : nullptr;
        }

        /// Creates a document by inferring the format from the file extension.
        std::shared_ptr<DataDocument> createByFilename(const std::string &filename) const
        {
            auto ext = extractExtension(filename);
            auto it = _byExtension.find(ext);
            return (it != _byExtension.end()) ? it->second.creator() : nullptr;
        }

        /// Returns the names of all registered formats.
        std::vector<std::string> registeredFormats() const
        {
            std::vector<std::string> v;
            v.reserve(_byName.size());
            for (const auto &p : _byName)
                v.push_back(p.first);
            return v;
        }

    private:
        DataScriptFactory() = default;

        static std::string extractExtension(const std::string &filename)
        {
            auto pos = filename.rfind('.');
            if (pos == std::string::npos)
                return {};
            std::string ext = filename.substr(pos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }

        std::unordered_map<std::string, FormatInfo> _byName;
        std::unordered_map<std::string, FormatInfo> _byExtension;
    };

// Auto-registration macro (to be placed in the compilation unit if needed)
#define SDL3PP_DS_REGISTER(name, ClassName, ...)                                                       \
    namespace                                                                                          \
    {                                                                                                  \
        inline const bool _ds_reg_##ClassName =                                                        \
            SDL::DataScriptFactory::instance().registerFormat(                                         \
                name, std::vector<std::string>{__VA_ARGS__},                                           \
                []() -> std::shared_ptr<SDL::DataDocument> { return std::make_shared<ClassName>(); }); \
    }

    // ========================================================================= //
    //  JSONDataDocument                                                          //
    // ========================================================================= //

    /// @brief JSON Document (RFC 8259).
    class JSONDataDocument final : public DataDocument
    {
    public:
        JSONDataDocument() = default;
        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        // ---- Encoding ----
        void encodeNode(std::ostringstream &, const std::shared_ptr<DataNode> &, int, bool) const;
        void encodeObject(std::ostringstream &, const std::shared_ptr<ObjectDataNode> &, int, bool) const;
        void encodeArray(std::ostringstream &, const std::shared_ptr<ArrayDataNode> &, int, bool) const;
        void encodeString(std::ostringstream &, const std::shared_ptr<StringDataNode> &) const;
        void writeIndent(std::ostringstream &, int, bool) const;
        // ---- Decoding ----
        std::shared_ptr<DataNode> parseNode(std::istream &);
        std::shared_ptr<ObjectDataNode> parseObject(std::istream &);
        std::shared_ptr<ArrayDataNode> parseArray(std::istream &);
        std::shared_ptr<StringDataNode> parseString(std::istream &);
        std::shared_ptr<DataNode> parseLiteral(std::istream &);
        void skipWS(std::istream &) const;
        char nextCh(std::istream &) const;
    };

    inline std::optional<DataParseError> JSONDataDocument::decodeImpl(std::istream &is)
    {
        try
        {
            auto node = parseNode(is);
            if (!node || node->getType() != DataNodeType::OBJECT)
                return DataParseError{"JSON: root must be an object {}"};
            setRoot(std::dynamic_pointer_cast<ObjectDataNode>(node));
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            return DataParseError{std::string("JSON: ") + e.what()};
        }
    }

    inline std::string JSONDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;
        encodeNode(ss, getRoot(), 0, true);
        return ss.str();
    }

    inline std::string JSONDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        std::ostringstream ss;
        encodeNode(ss, node, 0, false);
        return ss.str();
    }

    inline void JSONDataDocument::writeIndent(std::ostringstream &ss, int indent, bool pretty) const
    {
        if (!pretty)
            return;
        for (int i = 0; i < indent; ++i)
            ss << "  ";
    }

    inline void JSONDataDocument::encodeString(std::ostringstream &ss,
                                               const std::shared_ptr<StringDataNode> &node) const
    {
        ss << '"';
        for (unsigned char c : node->getValue())
        {
            switch (c)
            {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\b':
                ss << "\\b";
                break;
            case '\f':
                ss << "\\f";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                if (c < 0x20)
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                else
                    ss << static_cast<char>(c);
            }
        }
        ss << '"';
    }

    inline void JSONDataDocument::encodeArray(std::ostringstream &ss,
                                              const std::shared_ptr<ArrayDataNode> &node,
                                              int indent, bool pretty) const
    {
        ss << '[';
        if (pretty)
            ss << '\n';
        const auto &vals = node->getValues();
        for (size_t i = 0; i < vals.size(); ++i)
        {
            writeIndent(ss, indent + 1, pretty);
            encodeNode(ss, vals[i], indent + 1, pretty);
            if (i + 1 < vals.size())
                ss << ',';
            if (pretty)
                ss << '\n';
        }
        writeIndent(ss, indent, pretty);
        ss << ']';
    }

    inline void JSONDataDocument::encodeObject(std::ostringstream &ss,
                                               const std::shared_ptr<ObjectDataNode> &node,
                                               int indent, bool pretty) const
    {
        ss << '{';
        if (pretty)
            ss << '\n';
        const auto &map = node->getValues();
        size_t i = 0;
        for (const auto &pair : map)
        {
            writeIndent(ss, indent + 1, pretty);
            ss << '"' << pair.first << "\":";
            if (pretty)
                ss << ' ';
            encodeNode(ss, pair.second, indent + 1, pretty);
            if (i + 1 < map.size())
                ss << ',';
            if (pretty)
                ss << '\n';
            ++i;
        }
        writeIndent(ss, indent, pretty);
        ss << '}';
    }

    inline void JSONDataDocument::encodeNode(std::ostringstream &ss,
                                             const std::shared_ptr<DataNode> &node,
                                             int indent, bool pretty) const
    {
        if (!node)
        {
            ss << "null";
            return;
        }
        ss << std::setprecision(std::numeric_limits<double>::max_digits10);
        switch (node->getType())
        {
        case DataNodeType::ARRAY:
            encodeArray(ss, std::dynamic_pointer_cast<ArrayDataNode>(node), indent, pretty);
            return;
        case DataNodeType::OBJECT:
            encodeObject(ss, std::dynamic_pointer_cast<ObjectDataNode>(node), indent, pretty);
            return;
        case DataNodeType::STRING:
            encodeString(ss, std::dynamic_pointer_cast<StringDataNode>(node));
            return;
        default:
            NodeSerializer::writeScalar(ss, node);
        }
    }

    inline void JSONDataDocument::skipWS(std::istream &is) const
    {
        while (std::isspace(static_cast<unsigned char>(is.peek())))
            is.get();
    }
    inline char JSONDataDocument::nextCh(std::istream &is) const
    {
        skipWS(is);
        return static_cast<char>(is.get());
    }

    inline std::shared_ptr<StringDataNode> JSONDataDocument::parseString(std::istream &is)
    {
        is.get();
        std::string value;
        while (is.good() && is.peek() != '"')
        {
            char c = static_cast<char>(is.get());
            if (c == '\\')
            {
                char nx = static_cast<char>(is.get());
                switch (nx)
                {
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                case 'b':
                    value += '\b';
                    break;
                case 'f':
                    value += '\f';
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                case 'u':
                    is.ignore(4);
                    break;
                default:
                    value += nx;
                    break;
                }
            }
            else
            {
                value += c;
            }
        }
        if (is.get() != '"')
            throw std::runtime_error("unterminated string");
        return StringDataNode::Build(value);
    }

    inline std::shared_ptr<DataNode> JSONDataDocument::parseLiteral(std::istream &is)
    {
        std::string token;
        while (std::isalnum(static_cast<unsigned char>(is.peek())) || is.peek() == '-' || is.peek() == '+' || is.peek() == '.')
            token += static_cast<char>(is.get());
        if (token.empty())
            throw std::runtime_error("expected value");
        if (token == "true")
            return BoolDataNode::Build(true);
        if (token == "false")
            return BoolDataNode::Build(false);
        if (token == "null")
            return NoneDataNode::Make();
        return NodeSerializer::parseScalar(token);
    }

    inline std::shared_ptr<ArrayDataNode> JSONDataDocument::parseArray(std::istream &is)
    {
        is.get();
        auto arr = ArrayDataNode::Make();
        skipWS(is);
        if (is.peek() == ']')
        {
            is.get();
            return arr;
        }
        while (is.good())
        {
            arr->add(parseNode(is));
            char c = nextCh(is);
            if (c == ']')
                break;
            if (c != ',')
                throw std::runtime_error("expected ',' or ']' in array");
        }
        return arr;
    }

    inline std::shared_ptr<ObjectDataNode> JSONDataDocument::parseObject(std::istream &is)
    {
        is.get();
        auto obj = ObjectDataNode::Make();
        skipWS(is);
        if (is.peek() == '}')
        {
            is.get();
            return obj;
        }
        while (is.good())
        {
            skipWS(is);
            if (is.peek() != '"')
                throw std::runtime_error("expected string key");
            std::string key = parseString(is)->getValue();
            if (nextCh(is) != ':')
                throw std::runtime_error("expected ':' after key");
            obj->set(key, parseNode(is));
            char c = nextCh(is);
            if (c == '}')
                break;
            if (c != ',')
                throw std::runtime_error("expected ',' or '}' in object");
        }
        return obj;
    }

    inline std::shared_ptr<DataNode> JSONDataDocument::parseNode(std::istream &is)
    {
        skipWS(is);
        switch (is.peek())
        {
        case '{':
            return parseObject(is);
        case '[':
            return parseArray(is);
        case '"':
            return parseString(is);
        default:
            return parseLiteral(is);
        }
    }

    // ========================================================================= //
    //  XMLDataDocument                                                           //
    // ========================================================================= //

    /// @brief XML 1.0 Document (subset: elements, attributes, text, comments).
    class XMLDataDocument final : public DataDocument
    {
    public:
        XMLDataDocument() = default;
        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        static constexpr const char *ATTR_KEY = "@attributes";
        static constexpr const char *TEXT_KEY = "#text";
        static constexpr const char CHILD_PFX = '@';

        void encodeNode(std::ostringstream &, const std::string &, const std::shared_ptr<DataNode> &, int, bool) const;
        void encodeAttributes(std::ostringstream &, const std::shared_ptr<ObjectDataNode> &) const;
        void writeIndent(std::ostringstream &, int, bool) const;

        std::shared_ptr<ObjectDataNode> parseElement(std::istream &, std::string &);
        std::shared_ptr<ObjectDataNode> parseAttributes(std::istream &);
        void parseContent(std::istream &, std::shared_ptr<ObjectDataNode>, int &);
        void mergeChild(std::shared_ptr<ObjectDataNode>, const std::string &, std::shared_ptr<DataNode>);

        std::string readName(std::istream &);
        std::string readAttributeValue(std::istream &);
        std::string readTextContent(std::istream &);
        void skipWS(std::istream &) const;
        void skipCmt(std::istream &);
        char nextCh(std::istream &);
    };

    inline std::optional<DataParseError> XMLDataDocument::decodeImpl(std::istream &is)
    {
        try
        {
            auto root = ObjectDataNode::Make();
            setRoot(root);
            skipWS(is);
            if (is.peek() == '<')
            {
                is.get();
                if (is.peek() == '?')
                {
                    is.get();
                    while (is.good())
                    {
                        if (is.get() == '?' && is.peek() == '>')
                            break;
                    }
                    is.get();
                }
                else
                {
                    is.putback('<');
                }
            }
            while (is.good())
            {
                skipWS(is);
                if (is.peek() == EOF || !is.good())
                    break;
                if (is.peek() == '<')
                {
                    is.get();
                    if (is.peek() == '!')
                    {
                        skipCmt(is);
                        continue;
                    }
                    is.putback('<');
                    std::string tag;
                    auto elem = parseElement(is, tag);
                    if (elem)
                        mergeChild(root, tag, elem);
                }
                else
                {
                    readTextContent(is);
                    if (!is.good())
                        break;
                }
            }
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            return DataParseError{std::string("XML: ") + e.what()};
        }
    }

    inline std::string XMLDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        for (const auto &pair : getRoot()->getValues())
            encodeNode(ss, pair.first, pair.second, 0, true);
        return ss.str();
    }

    inline std::string XMLDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        std::ostringstream ss;
        encodeNode(ss, "root", node, 0, false);
        return ss.str();
    }

    inline void XMLDataDocument::skipWS(std::istream &is) const
    {
        while (std::isspace(static_cast<unsigned char>(is.peek())))
            is.get();
    }
    inline char XMLDataDocument::nextCh(std::istream &is)
    {
        skipWS(is);
        return static_cast<char>(is.get());
    }
    inline void XMLDataDocument::skipCmt(std::istream &is)
    {
        is.get();
        if (is.peek() == '-')
        {
            is.get();
            if (is.get() != '-')
                throw std::runtime_error("malformed XML comment (expected <!--)");
            int d = 0;
            while (is.good())
            {
                char c = static_cast<char>(is.get());
                if (c == '-')
                    ++d;
                else if (c == '>' && d >= 2)
                    return;
                else
                    d = 0;
            }
            throw std::runtime_error("unterminated XML comment");
        }
        throw std::runtime_error("unsupported XML directive");
    }
    inline std::string XMLDataDocument::readName(std::istream &is)
    {
        std::string name;
        while (is.good())
        {
            int c = is.peek();
            if (std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.')
                name += static_cast<char>(is.get());
            else
                break;
        }
        return name;
    }
    inline std::string XMLDataDocument::readAttributeValue(std::istream &is)
    {
        skipWS(is);
        char q = static_cast<char>(is.get());
        if (q != '"' && q != '\'')
            throw std::runtime_error("expected quote for attribute value");
        std::string v;
        while (is.good() && is.peek() != q)
            v += static_cast<char>(is.get());
        if (static_cast<char>(is.get()) != q)
            throw std::runtime_error("unterminated attribute value");
        return v;
    }
    inline std::string XMLDataDocument::readTextContent(std::istream &is)
    {
        std::string t;
        while (is.good() && is.peek() != '<' && is.peek() != EOF)
            t += static_cast<char>(is.get());
        return t;
    }

    inline std::shared_ptr<ObjectDataNode> XMLDataDocument::parseAttributes(std::istream &is)
    {
        auto attrs = ObjectDataNode::Make();
        while (is.good())
        {
            skipWS(is);
            int c = is.peek();
            if (c == '>' || c == '/' || c == EOF)
                break;
            std::string name = readName(is);
            if (name.empty())
                break;
            skipWS(is);
            if (is.get() != '=')
                throw std::runtime_error("expected '=' after attribute: " + name);
            attrs->set(name, StringDataNode::Build(readAttributeValue(is)));
        }
        return attrs;
    }

    inline void XMLDataDocument::parseContent(std::istream &is,
                                              std::shared_ptr<ObjectDataNode> parent,
                                              int &childIdx)
    {
        while (is.good())
        {
            skipWS(is);
            int c = is.peek();
            if (c == '<')
            {
                is.get();
                if (is.peek() == '/')
                {
                    is.putback('<');
                    return;
                }
                if (is.peek() == '!')
                {
                    skipCmt(is);
                    continue;
                }
                is.putback('<');
                std::string childTag;
                auto child = parseElement(is, childTag);
                if (child)
                {
                    std::string key = std::string(1, CHILD_PFX) + std::to_string(childIdx++) + ":" + childTag;
                    parent->set(key, child);
                }
            }
            else if (c == EOF || !is.good())
            {
                throw std::runtime_error("unexpected EOF (missing closing tag)");
            }
            else
            {
                std::string text = readTextContent(is);
                if (text.find_first_not_of(" \t\n\r") != std::string::npos)
                {
                    std::string key = std::string(1, CHILD_PFX) + std::to_string(childIdx++) + ":" + TEXT_KEY;
                    parent->set(key, StringDataNode::Build(text));
                }
            }
        }
    }

    inline std::shared_ptr<ObjectDataNode> XMLDataDocument::parseElement(std::istream &is,
                                                                         std::string &outTag)
    {
        if (nextCh(is) != '<')
            throw std::runtime_error("expected '<'");
        outTag = readName(is);
        if (outTag.empty())
            throw std::runtime_error("missing tag name");
        auto elem = ObjectDataNode::Make();
        auto attrs = parseAttributes(is);
        if (!attrs->getValues().empty())
            elem->set(ATTR_KEY, attrs);
        skipWS(is);
        char c = static_cast<char>(is.peek());
        if (c == '/')
        {
            is.get();
            if (is.get() != '>')
                throw std::runtime_error("expected '>' after '/'");
            return elem;
        }
        if (c == '>')
        {
            is.get();
            int idx = 0;
            parseContent(is, elem, idx);
            if (nextCh(is) != '<')
                throw std::runtime_error("expected '<' (closing tag)");
            if (is.get() != '/')
                throw std::runtime_error("expected '/' (closing tag)");
            std::string closing = readName(is);
            if (closing != outTag)
                throw std::runtime_error("mismatched tags: <" + outTag + "> closed by </" + closing + ">");
            if (nextCh(is) != '>')
                throw std::runtime_error("expected '>' at end of closing tag");
            return elem;
        }
        throw std::runtime_error("malformed tag after attributes");
    }

    inline void XMLDataDocument::mergeChild(std::shared_ptr<ObjectDataNode> parent,
                                            const std::string &key,
                                            std::shared_ptr<DataNode> newNode)
    {
        if (!parent->has(key))
        {
            parent->set(key, newNode);
        }
        else
        {
            auto existing = parent->get(key);
            if (existing->getType() == DataNodeType::ARRAY)
            {
                std::dynamic_pointer_cast<ArrayDataNode>(existing)->add(newNode);
            }
            else
            {
                auto arr = ArrayDataNode::Make();
                arr->add(existing);
                arr->add(newNode);
                parent->set(key, arr);
            }
        }
    }

    inline void XMLDataDocument::writeIndent(std::ostringstream &ss, int indent, bool pretty) const
    {
        if (!pretty)
            return;
        for (int i = 0; i < indent; ++i)
            ss << "  ";
    }

    inline void XMLDataDocument::encodeAttributes(std::ostringstream &ss,
                                                  const std::shared_ptr<ObjectDataNode> &attrs) const
    {
        for (const auto &p : attrs->getValues())
        {
            ss << ' ' << p.first << "=\"";
            if (p.second->getType() == DataNodeType::STRING)
                ss << std::dynamic_pointer_cast<StringDataNode>(p.second)->getValue();
            else
                ss << NodeSerializer::toScalarString(p.second);
            ss << '"';
        }
    }

    inline void XMLDataDocument::encodeNode(std::ostringstream &ss, const std::string &tagName,
                                            const std::shared_ptr<DataNode> &node,
                                            int indent, bool pretty) const
    {
        if (!node)
            return;
        if (node->getType() == DataNodeType::ARRAY)
        {
            for (const auto &item : std::dynamic_pointer_cast<ArrayDataNode>(node)->getValues())
                encodeNode(ss, tagName, item, indent, pretty);
            return;
        }
        if (node->getType() != DataNodeType::OBJECT)
        {
            writeIndent(ss, indent, pretty);
            ss << '<' << tagName << '>' << NodeSerializer::toScalarString(node) << "</" << tagName << '>';
            if (pretty)
                ss << '\n';
            return;
        }
        auto obj = std::dynamic_pointer_cast<ObjectDataNode>(node);
        writeIndent(ss, indent, pretty);
        ss << '<' << tagName;
        if (obj->has(ATTR_KEY))
            encodeAttributes(ss, std::dynamic_pointer_cast<ObjectDataNode>(obj->get(ATTR_KEY)));
        bool hasContent = false;
        for (const auto &p : obj->getValues())
            if (p.first != ATTR_KEY)
            {
                hasContent = true;
                break;
            }
        if (!hasContent)
        {
            ss << " />";
            if (pretty)
                ss << '\n';
            return;
        }
        ss << '>';
        bool onlyText = true;
        for (const auto &p : obj->getValues())
        {
            if (p.first == ATTR_KEY)
                continue;
            auto col = p.first.rfind(':');
            if (col == std::string::npos || p.first.substr(col + 1) != TEXT_KEY)
            {
                onlyText = false;
                break;
            }
        }
        if (!onlyText && pretty)
            ss << '\n';
        for (const auto &p : obj->getValues())
        {
            if (p.first == ATTR_KEY)
                continue;
            auto col = p.first.rfind(':');
            std::string childTag = (col != std::string::npos) ? p.first.substr(col + 1) : p.first;
            if (childTag == TEXT_KEY)
            {
                if (!onlyText)
                    writeIndent(ss, indent + 1, pretty);
                ss << (p.second->getType() == DataNodeType::STRING
                           ? std::dynamic_pointer_cast<StringDataNode>(p.second)->getValue()
                           : NodeSerializer::toScalarString(p.second));
                if (!onlyText && pretty)
                    ss << '\n';
            }
            else
            {
                encodeNode(ss, childTag, p.second, indent + 1, pretty);
            }
        }
        if (!onlyText)
            writeIndent(ss, indent, pretty);
        ss << "</" << tagName << '>';
        if (pretty)
            ss << '\n';
    }

    // ========================================================================= //
    //  YAMLDataDocument                                                          //
    // ========================================================================= //

    /// @brief Document YAML 1.2 (subset : mappings, séquences, scalaires, commentaires).
    class YAMLDataDocument final : public DataDocument
    {
    public:
        YAMLDataDocument() = default;
        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        struct YLine
        {
            int indent = 0;
            std::string content;
            int lineNum = 0;
        };
        using YLines = std::vector<YLine>;

        YLines preprocess(std::istream &);
        int countIndent(const std::string &) const;
        std::string trim(const std::string &) const;
        std::string stripComment(const std::string &) const;
        std::string unquote(const std::string &) const;

        std::shared_ptr<DataNode> parseBlock(const YLines &, size_t &, int);
        std::shared_ptr<ObjectDataNode> parseMapping(const YLines &, size_t &, int);
        std::shared_ptr<ArrayDataNode> parseSequence(const YLines &, size_t &, int);
        std::shared_ptr<DataNode> parseScalar(const std::string &, int);

        void encodeNode(std::ostringstream &, const std::shared_ptr<DataNode> &, int, bool) const;
        void encodeMapping(std::ostringstream &, const std::shared_ptr<ObjectDataNode> &, int) const;
        void encodeSequence(std::ostringstream &, const std::shared_ptr<ArrayDataNode> &, int) const;
        void encodeScalar(std::ostringstream &, const std::shared_ptr<DataNode> &) const;
        void writeIndent(std::ostringstream &, int) const;
    };

    inline int YAMLDataDocument::countIndent(const std::string &line) const
    {
        int n = 0;
        for (char c : line)
        {
            if (c == ' ')
                ++n;
            else if (c == '\t')
                n += 2;
            else
                break;
        }
        return n;
    }
    inline std::string YAMLDataDocument::trim(const std::string &s) const
    {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return {};
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }
    inline std::string YAMLDataDocument::stripComment(const std::string &s) const
    {
        bool inB = false, inL = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (c == '"' && !inL)
            {
                inB = !inB;
                continue;
            }
            if (c == '\'' && !inB)
            {
                inL = !inL;
                continue;
            }
            if (c == '\\' && inB)
            {
                ++i;
                continue;
            }
            if (c == '#' && !inB && !inL && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t'))
                return s.substr(0, i);
        }
        return s;
    }
    inline std::string YAMLDataDocument::unquote(const std::string &s) const
    {
        if (s.size() >= 2)
        {
            char f = s.front(), b = s.back();
            if ((f == '"' && b == '"') || (f == '\'' && b == '\''))
                return s.substr(1, s.size() - 2);
        }
        return s;
    }

    inline YAMLDataDocument::YLines YAMLDataDocument::preprocess(std::istream &is)
    {
        YLines result;
        std::string line;
        int lineNum = 0;
        while (std::getline(is, line))
        {
            ++lineNum;
            if (line == "..." || line == "---")
            {
                if (!result.empty())
                    break;
                continue;
            }
            std::string stripped = stripComment(line);
            if (!stripped.empty() && stripped.back() == '\r')
                stripped.pop_back();
            std::string content = trim(stripped);
            if (content.empty())
                continue;
            YLine yl;
            yl.indent = countIndent(stripped);
            yl.content = content;
            yl.lineNum = lineNum;
            result.push_back(yl);
        }
        return result;
    }

    inline std::shared_ptr<DataNode> YAMLDataDocument::parseScalar(const std::string &raw, int)
    {
        std::string t = trim(raw);
        if (t.empty() || t == "~" || t == "null" || t == "Null" || t == "NULL")
            return NoneDataNode::Make();
        if (t == "true" || t == "True" || t == "TRUE")
            return BoolDataNode::Build(true);
        if (t == "false" || t == "False" || t == "FALSE")
            return BoolDataNode::Build(false);
        if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
        {
            std::string val = unquote(t);
            if (t.front() == '"')
            {
                std::string unesc;
                for (size_t i = 0; i < val.size(); ++i)
                {
                    if (val[i] == '\\' && i + 1 < val.size())
                    {
                        switch (val[++i])
                        {
                        case 'n':
                            unesc += '\n';
                            break;
                        case 't':
                            unesc += '\t';
                            break;
                        case 'r':
                            unesc += '\r';
                            break;
                        case '"':
                            unesc += '"';
                            break;
                        case '\\':
                            unesc += '\\';
                            break;
                        default:
                            unesc += val[i];
                        }
                    }
                    else
                        unesc += val[i];
                }
                return StringDataNode::Build(unesc);
            }
            return StringDataNode::Build(val);
        }
        if (t == ".inf" || t == "+.inf")
            return F64DataNode::Build(std::numeric_limits<double>::infinity());
        if (t == "-.inf")
            return F64DataNode::Build(-std::numeric_limits<double>::infinity());
        if (t == ".nan")
            return F64DataNode::Build(std::numeric_limits<double>::quiet_NaN());
        {
            auto dc = std::count(t.begin(), t.end(), '.');
            if ((dc == 1 || t.find('e') != std::string::npos || t.find('E') != std::string::npos) && dc < 2)
            {
                try
                {
                    return F64DataNode::Build(std::stod(t));
                }
                catch (...)
                {
                }
            }
        }
        {
            char *end;
            long v = std::strtol(t.c_str(), &end, 10);
            if (*end == '\0' && end != t.c_str())
                return S64DataNode::Build(static_cast<int64_t>(v));
        }
        return StringDataNode::Build(t);
    }

    inline std::shared_ptr<DataNode> YAMLDataDocument::parseBlock(const YLines &lines, size_t &pos, int)
    {
        if (pos >= lines.size())
            return NoneDataNode::Make();
        const auto &first = lines[pos];
        if (first.content.size() >= 2 && first.content[0] == '-' && first.content[1] == ' ')
            return parseSequence(lines, pos, first.indent);
        {
            auto cs = first.content.find(": ");
            auto ce = (first.content.back() == ':');
            if (cs != std::string::npos || ce)
                return parseMapping(lines, pos, first.indent);
        }
        auto node = parseScalar(first.content, first.lineNum);
        ++pos;
        return node;
    }

    inline std::shared_ptr<ObjectDataNode> YAMLDataDocument::parseMapping(const YLines &lines, size_t &pos, int indent)
    {
        auto obj = ObjectDataNode::Make();
        while (pos < lines.size() && lines[pos].indent == indent)
        {
            const auto &line = lines[pos];
            auto colon = line.content.find(':');
            if (colon == std::string::npos)
                break;
            std::string key = trim(line.content.substr(0, colon));
            std::string inlineVal;
            if (colon + 1 < line.content.size())
                inlineVal = trim(line.content.substr(colon + 1));
            ++pos;
            if (inlineVal.empty() && pos < lines.size() && lines[pos].indent > indent)
                obj->set(key, parseBlock(lines, pos, lines[pos].indent));
            else
                obj->set(key, parseScalar(inlineVal, line.lineNum));
        }
        return obj;
    }

    inline std::shared_ptr<ArrayDataNode> YAMLDataDocument::parseSequence(const YLines &lines, size_t &pos, int indent)
    {
        auto arr = ArrayDataNode::Make();
        while (pos < lines.size() && lines[pos].indent == indent)
        {
            const auto &line = lines[pos];
            if (line.content.size() < 2 || line.content[0] != '-')
                break;
            std::string rest = trim(line.content.substr(2));
            ++pos;
            if (rest.empty())
            {
                if (pos < lines.size() && lines[pos].indent > indent)
                    arr->add(parseBlock(lines, pos, lines[pos].indent));
                else
                    arr->add(NoneDataNode::Make());
            }
            else
            {
                auto cs = rest.find(": ");
                auto ce = (rest.back() == ':');
                if (cs != std::string::npos || ce)
                {
                    YLines fake;
                    YLine fl;
                    fl.indent = indent + 2;
                    fl.content = rest;
                    fl.lineNum = line.lineNum;
                    fake.push_back(fl);
                    while (pos < lines.size() && lines[pos].indent > indent)
                        fake.push_back(lines[pos++]);
                    size_t fpos = 0;
                    arr->add(parseMapping(fake, fpos, fake[0].indent));
                }
                else
                {
                    arr->add(parseScalar(rest, line.lineNum));
                }
            }
        }
        return arr;
    }

    inline std::optional<DataParseError> YAMLDataDocument::decodeImpl(std::istream &is)
    {
        try
        {
            auto lines = preprocess(is);
            if (lines.empty())
            {
                setRoot(ObjectDataNode::Make());
                return std::nullopt;
            }
            size_t pos = 0;
            auto node = parseBlock(lines, pos, lines[0].indent);
            if (node->getType() == DataNodeType::OBJECT)
                setRoot(std::dynamic_pointer_cast<ObjectDataNode>(node));
            else
            {
                auto root = ObjectDataNode::Make();
                root->set("value", node);
                setRoot(root);
            }
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            return DataParseError{std::string("YAML: ") + e.what()};
        }
    }

    inline void YAMLDataDocument::writeIndent(std::ostringstream &ss, int indent) const
    {
        for (int i = 0; i < indent; ++i)
            ss << "  ";
    }
    inline void YAMLDataDocument::encodeScalar(std::ostringstream &ss, const std::shared_ptr<DataNode> &node) const
    {
        if (!node || node->getType() == DataNodeType::NONE)
        {
            ss << "null";
            return;
        }
        if (node->getType() == DataNodeType::BOOLEAN)
        {
            ss << (std::dynamic_pointer_cast<BoolDataNode>(node)->getValue() ? "true" : "false");
            return;
        }
        if (node->getType() == DataNodeType::STRING)
        {
            const auto &val = std::dynamic_pointer_cast<StringDataNode>(node)->getValue();
            bool q = val.empty() || val.find(':') != std::string::npos || val.find('#') != std::string::npos || val.find('\n') != std::string::npos || val == "true" || val == "false" || val == "null" || val == "~";
            if (q)
            {
                ss << '"';
                for (unsigned char c : val)
                {
                    if (c == '"')
                        ss << "\\\"";
                    else if (c == '\n')
                        ss << "\\n";
                    else if (c == '\\')
                        ss << "\\\\";
                    else
                        ss << (char)c;
                }
                ss << '"';
            }
            else
                ss << val;
            return;
        }
        NodeSerializer::writeScalar(ss, node);
    }
    inline void YAMLDataDocument::encodeSequence(std::ostringstream &ss, const std::shared_ptr<ArrayDataNode> &arr, int indent) const
    {
        for (const auto &item : arr->getValues())
        {
            writeIndent(ss, indent);
            ss << "- ";
            encodeNode(ss, item, indent + 1, true);
        }
    }
    inline void YAMLDataDocument::encodeMapping(std::ostringstream &ss, const std::shared_ptr<ObjectDataNode> &obj, int indent) const
    {
        for (const auto &p : obj->getValues())
        {
            writeIndent(ss, indent);
            ss << p.first << ':';
            if (p.second->getType() == DataNodeType::OBJECT || p.second->getType() == DataNodeType::ARRAY)
            {
                ss << '\n';
                encodeNode(ss, p.second, indent + 1, false);
            }
            else
            {
                ss << ' ';
                encodeNode(ss, p.second, indent, false);
            }
        }
    }
    inline void YAMLDataDocument::encodeNode(std::ostringstream &ss, const std::shared_ptr<DataNode> &node, int indent, bool asSeqItem) const
    {
        if (!node)
        {
            ss << "null\n";
            return;
        }
        switch (node->getType())
        {
        case DataNodeType::OBJECT:
            if (asSeqItem)
            {
                auto obj = std::dynamic_pointer_cast<ObjectDataNode>(node);
                bool first = true;
                for (const auto &p : obj->getValues())
                {
                    if (!first)
                    {
                        ss << '\n';
                        writeIndent(ss, indent);
                    }
                    ss << p.first << ':';
                    if (p.second->getType() == DataNodeType::OBJECT || p.second->getType() == DataNodeType::ARRAY)
                    {
                        ss << '\n';
                        encodeNode(ss, p.second, indent + 1, false);
                    }
                    else
                    {
                        ss << ' ';
                        encodeScalar(ss, p.second);
                        ss << '\n';
                    }
                    first = false;
                }
            }
            else
                encodeMapping(ss, std::dynamic_pointer_cast<ObjectDataNode>(node), indent);
            break;
        case DataNodeType::ARRAY:
            encodeSequence(ss, std::dynamic_pointer_cast<ArrayDataNode>(node), indent);
            break;
        default:
            encodeScalar(ss, node);
            ss << '\n';
        }
    }
    inline std::string YAMLDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;
        ss << "---\n";
        encodeMapping(ss, getRoot(), 0);
        return ss.str();
    }
    inline std::string YAMLDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        std::ostringstream ss;
        encodeScalar(ss, node);
        return ss.str();
    }

    // ========================================================================= //
    //  INIDataDocument                                                           //
    // ========================================================================= //

    /// @brief Document INI (sections [name], clé=valeur, commentaires ; et #).
    class INIDataDocument final : public DataDocument
    {
    public:
        INIDataDocument() = default;
        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        std::string trim(const std::string &s) const;
    };

    inline std::string INIDataDocument::trim(const std::string &s) const
    {
        auto f = s.find_first_not_of(" \t\r\n");
        if (f == std::string::npos)
            return {};
        auto l = s.find_last_not_of(" \t\r\n");
        return s.substr(f, l - f + 1);
    }

    inline std::optional<DataParseError> INIDataDocument::decodeImpl(std::istream &is)
    {
        auto root = ObjectDataNode::Make();
        setRoot(root);
        auto cur = root;
        std::string line;
        int lineNum = 0;
        while (std::getline(is, line))
        {
            ++lineNum;
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if (line.front() == '[' && line.back() == ']')
            {
                std::string sec = trim(line.substr(1, line.size() - 2));
                if (sec.empty())
                    return DataParseError{"empty section name", lineNum};
                auto ns = ObjectDataNode::Make();
                root->set(sec, ns);
                cur = ns;
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\'')))
                cur->set(key, StringDataNode::Build(val.substr(1, val.size() - 2)));
            else
                cur->set(key, NodeSerializer::parseScalar(val));
        }
        return std::nullopt;
    }

    inline std::string INIDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;
        for (const auto &p : getRoot()->getValues())
            if (p.second->getType() != DataNodeType::OBJECT)
                ss << p.first << " = " << NodeSerializer::toScalarString(p.second) << '\n';
        for (const auto &p : getRoot()->getValues())
        {
            if (p.second->getType() != DataNodeType::OBJECT)
                continue;
            ss << "\n[" << p.first << "]\n";
            auto sec = std::dynamic_pointer_cast<ObjectDataNode>(p.second);
            for (const auto &kv : sec->getValues())
            {
                if (kv.second->getType() == DataNodeType::OBJECT)
                    continue;
                ss << kv.first << " = " << NodeSerializer::toScalarString(kv.second) << '\n';
            }
        }
        return ss.str();
    }

    inline std::string INIDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        return NodeSerializer::toScalarString(node);
    }

    // ========================================================================= //
    //  TOMLDataDocument                                                          //
    // ========================================================================= //

    /// @brief Document TOML v1.0 (tables, tableaux de tables, scalaires typés).
    class TOMLDataDocument final : public DataDocument
    {
    public:
        TOMLDataDocument() = default;
        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        std::string trim(const std::string &) const;
        std::string trimComment(const std::string &) const;

        std::vector<std::string> parseKey(const std::string &, size_t &);
        std::shared_ptr<DataNode> parseString(const std::string &, size_t &);
        std::shared_ptr<DataNode> parseInlineArr(const std::string &, size_t &, int);
        std::shared_ptr<DataNode> parseInlineTab(const std::string &, size_t &, int);
        std::shared_ptr<DataNode> parseValue(const std::string &, int);
        std::shared_ptr<ObjectDataNode> resolveTable(std::shared_ptr<ObjectDataNode>,
                                                     const std::vector<std::string> &, bool);

        void encodeTable(std::ostringstream &, const std::shared_ptr<ObjectDataNode> &, const std::string &, bool) const;
        void encodeValue(std::ostringstream &, const std::shared_ptr<DataNode> &) const;
        void encodeString(std::ostringstream &, const std::string &) const;
        void encodeArray(std::ostringstream &, const std::shared_ptr<ArrayDataNode> &) const;
        bool isArrayOfTables(const std::shared_ptr<ArrayDataNode> &) const;
    };

    inline std::string TOMLDataDocument::trim(const std::string &s) const
    {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return {};
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }
    inline std::string TOMLDataDocument::trimComment(const std::string &s) const
    {
        bool inB = false, inL = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (c == '"' && !inL)
            {
                inB = !inB;
                continue;
            }
            if (c == '\'' && !inB)
            {
                inL = !inL;
                continue;
            }
            if (c == '\\' && inB)
            {
                ++i;
                continue;
            }
            if (c == '#' && !inB && !inL)
                return s.substr(0, i);
        }
        return s;
    }
    inline std::vector<std::string> TOMLDataDocument::parseKey(const std::string &line, size_t &pos)
    {
        std::vector<std::string> parts;
        while (pos < line.size())
        {
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            std::string part;
            if (pos < line.size() && line[pos] == '"')
            {
                ++pos;
                while (pos < line.size() && line[pos] != '"')
                {
                    if (line[pos] == '\\')
                        ++pos;
                    part += line[pos++];
                }
                if (pos < line.size())
                    ++pos;
            }
            else if (pos < line.size() && line[pos] == '\'')
            {
                ++pos;
                while (pos < line.size() && line[pos] != '\'')
                    part += line[pos++];
                if (pos < line.size())
                    ++pos;
            }
            else
            {
                while (pos < line.size() && (std::isalnum((unsigned char)line[pos]) || line[pos] == '-' || line[pos] == '_'))
                    part += line[pos++];
            }
            if (part.empty())
                break;
            parts.push_back(part);
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            if (pos < line.size() && line[pos] == '.')
            {
                ++pos;
                continue;
            }
            break;
        }
        return parts;
    }
    inline std::shared_ptr<DataNode> TOMLDataDocument::parseString(const std::string &text, size_t &pos)
    {
        char d = text[pos++];
        bool lit = (d == '\'');
        std::string val;
        while (pos < text.size() && text[pos] != d)
        {
            if (!lit && text[pos] == '\\')
            {
                ++pos;
                switch (text[pos])
                {
                case 'b':
                    val += '\b';
                    break;
                case 't':
                    val += '\t';
                    break;
                case 'n':
                    val += '\n';
                    break;
                case 'f':
                    val += '\f';
                    break;
                case 'r':
                    val += '\r';
                    break;
                case '"':
                    val += '"';
                    break;
                case '\\':
                    val += '\\';
                    break;
                default:
                    val += text[pos];
                }
            }
            else
                val += text[pos];
            ++pos;
        }
        if (pos < text.size())
            ++pos;
        return StringDataNode::Build(val);
    }
    inline std::shared_ptr<DataNode> TOMLDataDocument::parseInlineArr(const std::string &text, size_t &pos, int lineNum)
    {
        ++pos;
        auto arr = ArrayDataNode::Make();
        while (pos < text.size())
        {
            while (pos < text.size() && std::isspace((unsigned char)text[pos]))
                ++pos;
            if (pos >= text.size())
                break;
            if (text[pos] == ']')
            {
                ++pos;
                break;
            }
            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (text[pos] == '[')
                arr->add(parseInlineArr(text, pos, lineNum));
            else if (text[pos] == '{')
                arr->add(parseInlineTab(text, pos, lineNum));
            else
            {
                size_t start = pos;
                bool inS = false;
                char sd = 0;
                while (pos < text.size())
                {
                    char c = text[pos];
                    if (!inS && (c == '"' || c == '\''))
                    {
                        inS = true;
                        sd = c;
                        ++pos;
                        continue;
                    }
                    if (inS && c == sd)
                    {
                        inS = false;
                        ++pos;
                        continue;
                    }
                    if (!inS && (c == ',' || c == ']'))
                        break;
                    ++pos;
                }
                arr->add(parseValue(trim(text.substr(start, pos - start)), lineNum));
            }
        }
        return arr;
    }
    inline std::shared_ptr<DataNode> TOMLDataDocument::parseInlineTab(const std::string &text, size_t &pos, int lineNum)
    {
        ++pos;
        auto obj = ObjectDataNode::Make();
        while (pos < text.size())
        {
            while (pos < text.size() && std::isspace((unsigned char)text[pos]))
                ++pos;
            if (pos >= text.size())
                break;
            if (text[pos] == '}')
            {
                ++pos;
                break;
            }
            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }
            auto keys = parseKey(text, pos);
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
                ++pos;
            if (pos >= text.size() || text[pos] != '=')
                throw std::runtime_error("expected '=' in inline table");
            ++pos;
            while (pos < text.size() && std::isspace((unsigned char)text[pos]))
                ++pos;
            std::shared_ptr<DataNode> val;
            if (text[pos] == '[')
                val = parseInlineArr(text, pos, lineNum);
            else if (text[pos] == '{')
                val = parseInlineTab(text, pos, lineNum);
            else
            {
                size_t start = pos;
                bool inS = false;
                char sd = 0;
                while (pos < text.size())
                {
                    char c = text[pos];
                    if (!inS && (c == '"' || c == '\''))
                    {
                        inS = true;
                        sd = c;
                        ++pos;
                        continue;
                    }
                    if (inS && c == sd)
                    {
                        inS = false;
                        ++pos;
                        continue;
                    }
                    if (!inS && (c == ',' || c == '}'))
                        break;
                    ++pos;
                }
                val = parseValue(trim(text.substr(start, pos - start)), lineNum);
            }
            auto tgt = obj;
            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (!tgt->has(keys[i]))
                    tgt->set(keys[i], ObjectDataNode::Make());
                tgt = std::dynamic_pointer_cast<ObjectDataNode>(tgt->get(keys[i]));
            }
            tgt->set(keys.back(), val);
        }
        return obj;
    }
    inline std::shared_ptr<DataNode> TOMLDataDocument::parseValue(const std::string &raw, int lineNum)
    {
        std::string t = trim(raw);
        if (t.empty())
            throw std::runtime_error("empty value (line " + std::to_string(lineNum) + ")");
        if (t == "true")
            return BoolDataNode::Build(true);
        if (t == "false")
            return BoolDataNode::Build(false);
        if (t.front() == '"' || t.front() == '\'')
        {
            size_t pos = 0;
            return parseString(t, pos);
        }
        if (t.front() == '[')
        {
            size_t pos = 0;
            return parseInlineArr(t, pos, lineNum);
        }
        if (t.front() == '{')
        {
            size_t pos = 0;
            return parseInlineTab(t, pos, lineNum);
        }
        if (t.size() >= 10 && t[4] == '-' && t[7] == '-')
            return StringDataNode::Build(t);
        if (t.find('.') != std::string::npos || t.find('e') != std::string::npos || t.find('E') != std::string::npos)
        {
            if (t == "inf" || t == "+inf")
                return F64DataNode::Build(std::numeric_limits<double>::infinity());
            if (t == "-inf")
                return F64DataNode::Build(-std::numeric_limits<double>::infinity());
            if (t == "nan" || t == "+nan" || t == "-nan")
                return F64DataNode::Build(std::numeric_limits<double>::quiet_NaN());
            try
            {
                return F64DataNode::Build(std::stod(t));
            }
            catch (...)
            {
            }
        }
        {
            std::string cl;
            for (char c : t)
                if (c != '_')
                    cl += c;
            try
            {
                if (cl.substr(0, 2) == "0x")
                    return S64DataNode::Build(static_cast<int64_t>(std::stoll(cl.substr(2), nullptr, 16)));
                if (cl.substr(0, 2) == "0o")
                    return S64DataNode::Build(static_cast<int64_t>(std::stoll(cl.substr(2), nullptr, 8)));
                if (cl.substr(0, 2) == "0b")
                    return S64DataNode::Build(static_cast<int64_t>(std::stoll(cl.substr(2), nullptr, 2)));
                return S64DataNode::Build(std::stoll(cl));
            }
            catch (...)
            {
            }
        }
        return StringDataNode::Build(t);
    }
    inline std::shared_ptr<ObjectDataNode> TOMLDataDocument::resolveTable(
        std::shared_ptr<ObjectDataNode> root, const std::vector<std::string> &path, bool createArr)
    {
        auto cur = root;
        for (size_t i = 0; i < path.size(); ++i)
        {
            const std::string &key = path[i];
            bool isLast = (i + 1 == path.size());
            if (!cur->has(key))
            {
                if (isLast && createArr)
                {
                    auto arr = ArrayDataNode::Make();
                    auto e = ObjectDataNode::Make();
                    arr->add(e);
                    cur->set(key, arr);
                    return e;
                }
                auto ch = ObjectDataNode::Make();
                cur->set(key, ch);
                cur = ch;
            }
            else
            {
                auto ex = cur->get(key);
                if (ex->getType() == DataNodeType::ARRAY)
                {
                    auto arr = std::dynamic_pointer_cast<ArrayDataNode>(ex);
                    if (isLast && createArr)
                    {
                        auto e = ObjectDataNode::Make();
                        arr->add(e);
                        return e;
                    }
                    auto last = std::dynamic_pointer_cast<ObjectDataNode>(arr->get(arr->getSize() - 1));
                    if (!last)
                        throw std::runtime_error("navigation into non-object array");
                    cur = last;
                }
                else if (ex->getType() == DataNodeType::OBJECT)
                {
                    cur = std::dynamic_pointer_cast<ObjectDataNode>(ex);
                }
                else
                    throw std::runtime_error("key conflict: '" + key + "' is a scalar");
            }
        }
        return cur;
    }
    inline std::optional<DataParseError> TOMLDataDocument::decodeImpl(std::istream &is)
    {
        try
        {
            auto root = ObjectDataNode::Make();
            setRoot(root);
            auto cur = root;
            std::string line;
            int lineNum = 0;
            while (std::getline(is, line))
            {
                ++lineNum;
                std::string t = trim(trimComment(line));
                if (t.empty())
                    continue;
                if (t.size() >= 4 && t[0] == '[' && t[1] == '[')
                {
                    size_t end = t.find("]]");
                    if (end == std::string::npos)
                        return DataParseError{"missing ']]'", lineNum};
                    std::string hdr = trim(t.substr(2, end - 2));
                    size_t pos = 0;
                    auto path = parseKey(hdr, pos);
                    cur = resolveTable(root, path, true);
                    continue;
                }
                if (t.front() == '[')
                {
                    size_t end = t.find(']');
                    if (end == std::string::npos)
                        return DataParseError{"missing ']'", lineNum};
                    std::string hdr = trim(t.substr(1, end - 1));
                    size_t pos = 0;
                    auto path = parseKey(hdr, pos);
                    cur = resolveTable(root, path, false);
                    continue;
                }
                size_t pos = 0;
                auto keys = parseKey(t, pos);
                if (keys.empty())
                    return DataParseError{"invalid key", lineNum};
                while (pos < t.size() && (t[pos] == ' ' || t[pos] == '\t'))
                    ++pos;
                if (pos >= t.size() || t[pos] != '=')
                    return DataParseError{"expected '=' after key", lineNum};
                ++pos;
                while (pos < t.size() && (t[pos] == ' ' || t[pos] == '\t'))
                    ++pos;
                std::string valStr = trim(t.substr(pos));
                auto value = parseValue(valStr, lineNum);
                auto tgt = cur;
                for (size_t i = 0; i + 1 < keys.size(); ++i)
                {
                    const std::string &k = keys[i];
                    if (!tgt->has(k))
                        tgt->set(k, ObjectDataNode::Make());
                    tgt = std::dynamic_pointer_cast<ObjectDataNode>(tgt->get(k));
                }
                tgt->set(keys.back(), value);
            }
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            return DataParseError{std::string("TOML: ") + e.what()};
        }
    }

    inline void TOMLDataDocument::encodeString(std::ostringstream &ss, const std::string &s) const
    {
        ss << '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                if (c < 0x20)
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
                else
                    ss << (char)c;
            }
        }
        ss << '"';
    }
    inline void TOMLDataDocument::encodeArray(std::ostringstream &ss, const std::shared_ptr<ArrayDataNode> &arr) const
    {
        ss << '[';
        const auto &v = arr->getValues();
        for (size_t i = 0; i < v.size(); ++i)
        {
            encodeValue(ss, v[i]);
            if (i + 1 < v.size())
                ss << ", ";
        }
        ss << ']';
    }
    inline void TOMLDataDocument::encodeValue(std::ostringstream &ss, const std::shared_ptr<DataNode> &node) const
    {
        if (!node)
        {
            ss << "\"\"";
            return;
        }
        switch (node->getType())
        {
        case DataNodeType::NONE:
            ss << "\"\"";
            break;
        case DataNodeType::BOOLEAN:
            ss << (std::dynamic_pointer_cast<BoolDataNode>(node)->getValue() ? "true" : "false");
            break;
        case DataNodeType::STRING:
            encodeString(ss, std::dynamic_pointer_cast<StringDataNode>(node)->getValue());
            break;
        case DataNodeType::ARRAY:
            encodeArray(ss, std::dynamic_pointer_cast<ArrayDataNode>(node));
            break;
        case DataNodeType::OBJECT:
        {
            ss << '{';
            auto obj = std::dynamic_pointer_cast<ObjectDataNode>(node);
            bool first = true;
            for (const auto &p : obj->getValues())
            {
                if (!first)
                    ss << ", ";
                ss << p.first << " = ";
                encodeValue(ss, p.second);
                first = false;
            }
            ss << '}';
            break;
        }
        default:
            NodeSerializer::writeScalar(ss, node);
        }
    }
    inline bool TOMLDataDocument::isArrayOfTables(const std::shared_ptr<ArrayDataNode> &arr) const
    {
        return arr && arr->getSize() > 0 && arr->get(0)->getType() == DataNodeType::OBJECT;
    }
    inline void TOMLDataDocument::encodeTable(std::ostringstream &ss,
                                              const std::shared_ptr<ObjectDataNode> &obj,
                                              const std::string &prefix, bool isAT) const
    {
        if (!prefix.empty())
        {
            if (isAT)
                ss << "\n[[" << prefix << "]]\n";
            else
                ss << "\n[" << prefix << "]\n";
        }
        for (const auto &p : obj->getValues())
        {
            if (p.second->getType() == DataNodeType::OBJECT)
                continue;
            if (p.second->getType() == DataNodeType::ARRAY && isArrayOfTables(std::dynamic_pointer_cast<ArrayDataNode>(p.second)))
                continue;
            ss << p.first << " = ";
            encodeValue(ss, p.second);
            ss << '\n';
        }
        for (const auto &p : obj->getValues())
        {
            std::string fk = prefix.empty() ? p.first : prefix + "." + p.first;
            if (p.second->getType() == DataNodeType::OBJECT)
            {
                encodeTable(ss, std::dynamic_pointer_cast<ObjectDataNode>(p.second), fk, false);
            }
            else if (p.second->getType() == DataNodeType::ARRAY)
            {
                auto arr = std::dynamic_pointer_cast<ArrayDataNode>(p.second);
                if (isArrayOfTables(arr))
                    for (const auto &e : arr->getValues())
                        encodeTable(ss, std::dynamic_pointer_cast<ObjectDataNode>(e), fk, true);
            }
        }
    }
    inline std::string TOMLDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;
        encodeTable(ss, getRoot(), "", false);
        return ss.str();
    }
    inline std::string TOMLDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        std::ostringstream ss;
        encodeValue(ss, node);
        return ss.str();
    }

    // ========================================================================= //
    //  CSVDataDocument                                                           //
    // ========================================================================= //

    /**
     * @brief Document CSV (RFC 4180).
     *
     * Structure de données supportée :
     *   - **Lecture** : la première ligne donne les noms de colonnes. Chaque
     *     colonne est représentée par un ArrayDataNode dans l'ObjectDataNode
     *     racine. L'index i d'un tableau correspond à la valeur de la ligne i+1.
     *   - **Écriture** : la racine doit être un ObjectDataNode dont chaque valeur
     *     est un ArrayDataNode (colonnes nommées). Les longueurs sont alignées
     *     sur la colonne la plus longue (cellules vides pour les courtes).
     *
     * @note L'export CSV ne supporte qu'une arborescence d'un niveau.
     *       Les valeurs non-ArrayDataNode dans la racine sont ignorées à
     *       l'export.
     */
    class CSVDataDocument final : public DataDocument
    {
    public:
        CSVDataDocument() = default;

        /// Séparateur de champs (défaut : ',').
        void setDelimiter(char d) noexcept { _delim = d; }
        char getDelimiter() noexcept { return _delim; }

        [[nodiscard]] std::string encode() const override;

    protected:
        std::optional<DataParseError> decodeImpl(std::istream &is) override;
        [[nodiscard]] std::string stringifyNode(std::shared_ptr<DataNode> node) const override;

    private:
        char _delim = ',';

        std::string trim(const std::string &s) const;
        std::vector<std::string> splitLine(const std::string &line) const;
        std::string encodeField(const std::string &field) const;
    };

    inline std::string CSVDataDocument::trim(const std::string &s) const
    {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return {};
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    inline std::vector<std::string> CSVDataDocument::splitLine(const std::string &line) const
    {
        std::vector<std::string> fields;
        std::string field;
        bool inQuote = false;

        for (size_t i = 0; i <= line.size(); ++i)
        {
            if (i == line.size())
            {
                if (inQuote)
                    field += '"';
                fields.push_back(field);
                break;
            }
            char c = line[i];
            if (inQuote)
            {
                if (c == '"')
                {
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        field += '"';
                        ++i;
                    }
                    else
                        inQuote = false;
                }
                else
                {
                    field += c;
                }
            }
            else
            {
                if (c == '"')
                {
                    inQuote = true;
                }
                else if (c == _delim)
                {
                    fields.push_back(field);
                    field.clear();
                }
                else
                {
                    field += c;
                }
            }
        }
        return fields;
    }

    inline std::string CSVDataDocument::encodeField(const std::string &field) const
    {
        bool needsQuote = field.find(_delim) != std::string::npos ||
                          field.find('"') != std::string::npos ||
                          field.find('\n') != std::string::npos ||
                          field.find('\r') != std::string::npos;
        if (!needsQuote)
            return field;
        std::string out = "\"";
        for (char c : field)
        {
            if (c == '"')
                out += '"';
            out += c;
        }
        out += '"';
        return out;
    }

    inline std::optional<DataParseError> CSVDataDocument::decodeImpl(std::istream &is)
    {
        auto root = ObjectDataNode::Make();
        setRoot(root);

        std::string line;
        std::vector<std::string> headers;
        std::vector<std::shared_ptr<ArrayDataNode>> columns;

        // Ligne d'en-tête
        if (!std::getline(is, line))
            return std::nullopt;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        headers = splitLine(line);
        for (const auto &h : headers)
        {
            auto col = ArrayDataNode::Make();
            root->set(trim(h), col);
            columns.push_back(col);
        }

        // Lignes de données
        int lineNum = 1;
        while (std::getline(is, line))
        {
            ++lineNum;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            auto fields = splitLine(line);
            for (size_t i = 0; i < columns.size(); ++i)
            {
                std::string val = (i < fields.size()) ? fields[i] : "";
                columns[i]->add(NodeSerializer::parseScalar(val));
            }
        }
        return std::nullopt;
    }

    inline std::string CSVDataDocument::encode() const
    {
        if (!getRoot())
            return "";
        std::ostringstream ss;

        // Collecter les colonnes (clés dont la valeur est un ArrayDataNode)
        std::vector<std::string> colNames;
        std::vector<std::shared_ptr<ArrayDataNode>> cols;
        size_t maxRows = 0;

        for (const auto &p : getRoot()->getValues())
        {
            if (p.second->getType() != DataNodeType::ARRAY)
                continue;
            colNames.push_back(p.first);
            auto arr = std::dynamic_pointer_cast<ArrayDataNode>(p.second);
            cols.push_back(arr);
            maxRows = std::max(maxRows, arr->getSize());
        }

        if (colNames.empty())
            return "";

        // En-tête
        for (size_t i = 0; i < colNames.size(); ++i)
        {
            if (i)
                ss << _delim;
            ss << encodeField(colNames[i]);
        }
        ss << "\r\n";

        // Lignes de données
        for (size_t row = 0; row < maxRows; ++row)
        {
            for (size_t col = 0; col < cols.size(); ++col)
            {
                if (col)
                    ss << _delim;
                if (row < cols[col]->getSize())
                {
                    auto node = cols[col]->get(row);
                    std::string val = (node && node->getType() == DataNodeType::STRING)
                                          ? std::dynamic_pointer_cast<StringDataNode>(node)->getValue()
                                          : NodeSerializer::toScalarString(node);
                    ss << encodeField(val);
                }
            }
            ss << "\r\n";
        }

        return ss.str();
    }

    inline std::string CSVDataDocument::stringifyNode(std::shared_ptr<DataNode> node) const
    {
        return NodeSerializer::toScalarString(node);
    }

    // ========================================================================= //
    //  Auto-enregistrement de tous les formats intégrés                         //
    // ========================================================================= //

    namespace detail
    {

        inline bool registerBuiltinFormats()
        {
            auto &f = DataScriptFactory::instance();
            f.registerFormat("json", {".json"}, []
                             { return std::make_shared<JSONDataDocument>(); });
            f.registerFormat("xml", {".xml"}, []
                             { return std::make_shared<XMLDataDocument>(); });
            f.registerFormat("yaml", {".yaml", ".yml"}, []
                             { return std::make_shared<YAMLDataDocument>(); });
            f.registerFormat("ini", {".ini", ".cfg"}, []
                             { return std::make_shared<INIDataDocument>(); });
            f.registerFormat("toml", {".toml"}, []
                             { return std::make_shared<TOMLDataDocument>(); });
            f.registerFormat("csv", {".csv", ".tsv"}, []
                             { return std::make_shared<CSVDataDocument>(); });
            return true;
        }

        inline const bool _builtinFormatsRegistered = registerBuiltinFormats();

    } // namespace detail

    // ========================================================================= //
    //  Fonctions utilitaires                                                     //
    // ========================================================================= //

    /**
     * @brief Parse un flux SDL en document du format indiqué.
     *
     * @param io     Flux source (doit être ouvert en lecture).
     * @param format Nom du format : "json", "xml", "yaml", "ini", "toml", "csv".
     * @returns      Document initialisé, ou nullptr si le format est inconnu.
     *
     * @code
     *   auto src = SDL::IOStream::FromFile("data.yaml", "r");
     *   auto doc = SDL::ParseDataScript(src, "yaml");
     *   if (!doc) { // format inconnu }
     * @endcode
     */
    inline std::shared_ptr<DataDocument>
    ParseDataScript(IOStreamRef io, std::string_view format)
    {
        auto doc = DataScriptFactory::instance().createByName(std::string(format));
        if (!doc)
            return nullptr;
        if (auto err = doc->decode(std::move(io)); err)
            return nullptr;
        return doc;
    }

    /**
     * @brief Parse un flux SDL en déduisant le format depuis le nom de fichier.
     *
     * @param io       Flux source.
     * @param filename Nom de fichier servant à détecter l'extension.
     */
    inline std::shared_ptr<DataDocument>
    ParseDataScriptFile(IOStreamRef io, std::string_view filename)
    {
        auto doc = DataScriptFactory::instance().createByFilename(std::string(filename));
        if (!doc)
            return nullptr;
        if (auto err = doc->decode(std::move(io)); err)
            return nullptr;
        return doc;
    }

    /**
     * @brief Encode un document dans le format indiqué et l'écrit dans un flux SDL.
     *
     * @param doc    Document source (doit avoir une racine non nulle).
     * @param io     Flux de destination (doit être ouvert en écriture).
     * @param format Nom du format cible.
     * @returns true si l'encodage et l'écriture ont réussi.
     */
    inline bool
    EncodeDataScript(const std::shared_ptr<DataDocument> &srcDoc,
                     IOStreamRef io,
                     std::string_view format)
    {
        if (!srcDoc || !srcDoc->getRoot())
            return false;

        auto dstDoc = DataScriptFactory::instance().createByName(std::string(format));
        if (!dstDoc)
            return false;

        // Transférer la racine (copie profonde pour préserver le srcDoc)
        dstDoc->setRoot(std::dynamic_pointer_cast<ObjectDataNode>(srcDoc->getRoot()->clone()));

        return dstDoc->encode(std::move(io));
    }

    /**
     * @brief Convertit un flux source d'un format vers un flux destination d'un autre format.
     *
     * @param src       Flux source (lecture).
     * @param srcFormat Format source : "json", "xml", "yaml", "ini", "toml", "csv".
     * @param dst       Flux destination (écriture).
     * @param dstFormat Format destination.
     * @returns nullopt si succès, ou un DataParseError décrivant l'erreur.
     *
     * @code
     *   // Convertir un XML en JSON
     *   auto err = SDL::ConvertDataScript(
     *       SDL::IOStream::FromFile("config.xml",  "r"), "xml",
     *       SDL::IOStream::FromFile("config.json", "w"), "json");
     * @endcode
     */
    inline std::optional<DataParseError>
    ConvertDataScript(IOStreamRef src, std::string_view srcFormat,
                      IOStreamRef dst, std::string_view dstFormat)
    {
        // 1. Créer et parser le document source
        auto srcDoc = DataScriptFactory::instance().createByName(std::string(srcFormat));
        if (!srcDoc)
            return DataParseError{"ConvertDataScript: unknown source format '" + std::string(srcFormat) + "'"};

        if (auto err = srcDoc->decode(std::move(src)))
            return err;

        // 2. Créer le document destination et lui transférer la racine
        auto dstDoc = DataScriptFactory::instance().createByName(std::string(dstFormat));
        if (!dstDoc)
            return DataParseError{"ConvertDataScript: unknown destination format '" + std::string(dstFormat) + "'"};

        if (srcDoc->getRoot())
            dstDoc->setRoot(std::dynamic_pointer_cast<ObjectDataNode>(srcDoc->getRoot()->clone()));

        // 3. Encoder vers la destination
        if (!dstDoc->encode(std::move(dst)))
            return DataParseError{"ConvertDataScript: encoding to '" + std::string(dstFormat) + "' failed"};

        return std::nullopt;
    }

    /**
     * @brief Convertit entre deux formats à partir de chaînes en mémoire.
     *
     * @param src       Contenu source (chaîne).
     * @param srcFormat Format source.
     * @param dstFormat Format destination.
     * @returns Chaîne encodée dans le format destination, ou "" en cas d'erreur.
     */
    inline std::string
    ConvertDataScriptStr(const std::string &src,
                         std::string_view srcFormat,
                         std::string_view dstFormat)
    {
        auto srcDoc = DataScriptFactory::instance().createByName(std::string(srcFormat));
        if (!srcDoc || srcDoc->decodeStr(src))
            return "";

        auto dstDoc = DataScriptFactory::instance().createByName(std::string(dstFormat));
        if (!dstDoc)
            return "";

        if (srcDoc->getRoot())
            dstDoc->setRoot(std::dynamic_pointer_cast<ObjectDataNode>(srcDoc->getRoot()->clone()));

        return dstDoc->encode();
    }

    /**
     * @}
     */

} // namespace SDL

#endif // SDL3PP_DATASCRIPTS_H_