/**
 * @file 08_chat.cpp
 * @brief P2P chat application using SDL3pp_ui and SDL3pp_net.
 *
 * Launch modes (command-line):
 *   --server         Start as chat server (default port 9900)
 *   --client         Start as chat client
 *   --port=N         Override the default port
 *   --host=H         Server host for client mode (default 127.0.0.1)
 *
 * ## Server features
 *   - Lists connected clients with status.
 *   - Create / delete discussion rooms.
 *   - Settings: max clients, max clients per room.
 *   - Kick clients.
 *   - Scheduled announcements: text + HH:MM + repeat period in minutes.
 *   - Persists config and per-room message history to binary files.
 *   - Debug panel showing all protocol traffic.
 *
 * ## Client features
 *   - Connects to server, joins rooms, sends room messages.
 *   - Sees room users.
 *   - Downloads last 20 room messages on join.
 *   - DM invitation system: enter target client ID → invite sent via server
 *     → invitee sees notification → on accept a direct P2P TCP link opens.
 *   - Persists config, contacts, and DM histories to binary files.
 *
 * ## Binary persistence
 *   - data/server_config.bin  : server settings + room names
 *   - data/room_<N>.bin       : per-room message history (last 50 kept)
 *   - data/client_config.bin  : client settings + contacts
 *   - data/dm_<N>.bin         : per-peer DM history
 *
 * ## Protocol
 *   Length-prefixed framing via SDL::MessageFramer.
 *   Payload: 1-byte type tag + '\n' + "key=value\n" lines.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_ttf.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_dataScripts.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
  constexpr SDL::Color BG      = {12,  16,  26, 255};
  constexpr SDL::Color PANEL   = {18,  24,  40, 255};
  constexpr SDL::Color CARD    = {24,  32,  54, 255};
  constexpr SDL::Color HEADER  = {15,  20,  35, 255};
  constexpr SDL::Color ACCENT  = {55, 130, 220, 255};
  constexpr SDL::Color WHITE   = {215,220, 232, 255};
  constexpr SDL::Color GREY    = {120,128, 150, 255};
  constexpr SDL::Color BORDER  = {40,  50,  78, 255};
  constexpr SDL::Color GREEN   = { 50,195,  90, 255};
  constexpr SDL::Color ORANGE  = {230,145,  40, 255};
  constexpr SDL::Color RED     = {210,  55,  50, 255};
  constexpr SDL::Color YELLOW  = {240,200,  50, 255};
  constexpr SDL::Color TRANSP  = {  0,   0,   0,   0};
}

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight binary I/O helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace Bin {

struct Writer {
    std::vector<uint8_t> buf;
    void u8 (uint8_t  v) { buf.push_back(v); }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>(v >> 16)); }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        for (unsigned char c : s) u8(c);
    }
    bool save(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        if (!buf.empty()) f.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        return f.good();
    }
};

struct Reader {
    std::vector<uint8_t> buf;
    size_t pos = 0;
    bool ok() const { return pos <= buf.size(); }
    bool u8(uint8_t& v)  { if (pos >= buf.size()) return false; v = buf[pos++]; return true; }
    bool u16(uint16_t& v) {
        uint8_t a, b;
        if (!u8(a) || !u8(b)) return false;
        v = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8));
        return true;
    }
    bool u32(uint32_t& v) {
        uint16_t lo, hi;
        if (!u16(lo) || !u16(hi)) return false;
        v = lo | (static_cast<uint32_t>(hi) << 16);
        return true;
    }
    bool str(std::string& s) {
        uint32_t len;
        if (!u32(len) || pos + len > buf.size()) return false;
        s.assign(reinterpret_cast<const char*>(buf.data() + pos), len);
        pos += len;
        return true;
    }
    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        buf.assign(std::istreambuf_iterator<char>(f), {});
        pos = 0;
        return true;
    }
};

} // namespace Bin

// ─────────────────────────────────────────────────────────────────────────────
// Protocol helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace Protocol {

enum class MsgType : Uint8 {
    NONE        = 0x00,
    HELLO       = 0x01, ///< Client→Server: nickname=...
    WELCOME     = 0x02, ///< Server→Client: id=N, nickname=...
    CLIENT_LIST = 0x03, ///< Server→Client: clients=id:nickname:status,...
    ROOM_LIST   = 0x04, ///< Server→Client: rooms=id:name,...
    JOIN_ROOM   = 0x05, ///< Client→Server: room_id=N
    LEAVE_ROOM  = 0x06, ///< Client→Server: (no fields)
    ROOM_MSG    = 0x07, ///< Both: from=id, nickname=..., text=..., [hist=1]
    ROOM_USERS  = 0x08, ///< Server→Client: users=id:nickname,...
    DM_INVITE   = 0x09, ///< Server-relayed: to=id, from=id, nickname=..., port=N
    DM_ACCEPT   = 0x0A, ///< Server-relayed: to=id, from=id, nickname=..., port=N
    DM_MSG      = 0x0B, ///< Direct P2P connection: from=id, text=...
    SET_STATUS  = 0x0C, ///< Client→Server: status=online|away|busy
    ANNOUNCE    = 0x0D, ///< Server→Client: text=...
    KICK        = 0x0E, ///< Server→Client: (no fields)
    PING        = 0x0F, ///< keepalive
    CREATE_ROOM = 0x10, ///< Client→Server: name=...
    DELETE_ROOM = 0x11, ///< Client→Server: room_id=N
};

inline std::vector<Uint8> Make(MsgType t,
    std::initializer_list<std::pair<const char*, std::string>> kv = {})
{
    std::vector<Uint8> buf;
    buf.push_back(static_cast<Uint8>(t));
    buf.push_back('\n');
    for (auto& [k, v] : kv) {
        for (char c : std::string_view(k)) buf.push_back(static_cast<Uint8>(c));
        buf.push_back('=');
        for (char c : v) buf.push_back(static_cast<Uint8>(c));
        buf.push_back('\n');
    }
    return buf;
}

inline std::string Get(const std::vector<Uint8>& msg, const std::string& key)
{
    if (msg.size() < 2) return "";
    std::string body(reinterpret_cast<const char*>(msg.data() + 2), msg.size() - 2);
    std::string search = key + "=";
    auto pos = body.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = body.find('\n', pos);
    return body.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

inline MsgType Type(const std::vector<Uint8>& msg)
{
    return msg.empty() ? MsgType::NONE : static_cast<MsgType>(msg[0]);
}

} // namespace Protocol

// ─────────────────────────────────────────────────────────────────────────────
// Domain types
// ─────────────────────────────────────────────────────────────────────────────
struct ClientInfo {
    int         id     = 0;
    std::string nickname;
    std::string status = "online";
    int         roomId = -1;
};

struct RoomMessage {
    int         fromId = 0;
    std::string nickname;
    std::string text;
};

struct Room {
    int              id = 0;
    std::string      name;
    std::vector<int> memberIds;
    std::deque<RoomMessage> history; ///< kept at most 50 entries
};

struct Announcement {
    std::string text;
    int hour       = 0;
    int minute     = 0;
    int repeatMins = 0;
    std::chrono::system_clock::time_point lastSent{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Parse helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<std::pair<int,std::string>> ParseIdNameList(const std::string& s)
{
    std::vector<std::pair<int,std::string>> out;
    size_t pos = 0;
    while (pos < s.size()) {
        auto comma = s.find(',', pos);
        std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        auto colon = tok.find(':');
        if (colon != std::string::npos) {
            try { out.push_back({std::stoi(tok.substr(0, colon)), tok.substr(colon + 1)}); }
            catch (...) {}
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Binary persistence — server
// ─────────────────────────────────────────────────────────────────────────────
// room_<id>.bin  : "ROOM" magic + uint32 count + {uint32 fromId + str nick + str text}*
// server_config.bin: "SRVC" magic + uint32 maxClients + uint32 maxPerRoom
//                    + uint32 roomCount + {str name}*

static constexpr uint32_t MAGIC_ROOM = 0x4D4F4F52; // "ROOM"
static constexpr uint32_t MAGIC_SRVC = 0x43565253; // "SRVC"
static constexpr uint32_t MAGIC_CLNT = 0x544E4C43; // "CLNT"
static constexpr uint32_t MAGIC_DMMG = 0x474D4D44; // "DMMG"

static bool SaveRoomHistory(int roomId, const std::deque<RoomMessage>& hist)
{
    Bin::Writer w;
    w.u32(MAGIC_ROOM);
    w.u32(static_cast<uint32_t>(hist.size()));
    for (auto& m : hist) {
        w.u32(static_cast<uint32_t>(m.fromId));
        w.str(m.nickname);
        w.str(m.text);
    }
    return w.save(std::format("{}../../../data/room_{}.bin", SDL::GetBasePath(), roomId));
}

static std::deque<RoomMessage> LoadRoomHistory(int roomId)
{
    Bin::Reader r;
    if (!r.load(std::format("{}../../../data/room_{}.bin", SDL::GetBasePath(), roomId))) return {};
    uint32_t magic; if (!r.u32(magic) || magic != MAGIC_ROOM) return {};
    uint32_t count; if (!r.u32(count)) return {};
    std::deque<RoomMessage> hist;
    for (uint32_t i = 0; i < count; ++i) {
        RoomMessage m;
        uint32_t id;
        if (!r.u32(id)) break;
        m.fromId = static_cast<int>(id);
        if (!r.str(m.nickname) || !r.str(m.text)) break;
        hist.push_back(std::move(m));
    }
    return hist;
}

// ─────────────────────────────────────────────────────────────────────────────
// ChatServer  (background thread)
// ─────────────────────────────────────────────────────────────────────────────
class ChatServer {
public:
    static constexpr Uint16 BASE_PORT = 9900;
    static constexpr size_t MAX_HISTORY = 50;
    static constexpr size_t SEND_HISTORY = 20;

    struct Peer {
        SDL::StreamSocket   sock;
        SDL::MessageFramer  framer;
        ClientInfo          info;
    };

    std::mutex  m_mtx;
    int         m_maxClients = 16;
    int         m_maxPerRoom = 8;

    void Start(Uint16 port)
    {
        m_running = true;
        m_thread  = std::thread([this, port]{ _Run(port); });
    }

    void Stop()
    {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
    }

    // ── UI-thread commands ────────────────────────────────────────────────────

    void AddRoom(const std::string& name)
    {
        std::lock_guard lock(m_mtx);
        Room& r = m_rooms.emplace_back();
        r.id   = m_nextRoomId++;
        r.name = name;
        _BroadcastRoomList();
    }

    void DeleteRoom(int roomId)
    {
        std::lock_guard lock(m_mtx);
        _DeleteRoomLocked(roomId);
    }

    void KickClient(int clientId)
    {
        std::lock_guard lock(m_mtx);
        _SendToPeer(clientId, Protocol::Make(Protocol::MsgType::KICK));
        _RemovePeerLocked(clientId);
    }

    void AddAnnouncement(Announcement a)
    {
        std::lock_guard lock(m_mtx);
        m_announcements.push_back(std::move(a));
    }

    void SetClientStatus(int clientId, const std::string& status)
    {
        std::lock_guard lock(m_mtx);
        for (auto& p : m_peers)
            if (p->info.id == clientId) { p->info.status = status; break; }
        _BroadcastClientList();
    }

    void RemoveAnnouncement(size_t idx)
    {
        std::lock_guard lock(m_mtx);
        if (idx < m_announcements.size())
            m_announcements.erase(m_announcements.begin() + static_cast<ptrdiff_t>(idx));
    }

    void SaveConfig()
    {
        std::lock_guard lock(m_mtx);
        Bin::Writer w;
        w.u32(MAGIC_SRVC);
        w.u32(static_cast<uint32_t>(m_maxClients));
        w.u32(static_cast<uint32_t>(m_maxPerRoom));
        w.u32(static_cast<uint32_t>(m_rooms.size()));
        for (auto& r : m_rooms) w.str(r.name);
        w.save(std::format("{}../../../data/server_config.bin", SDL_GetBasePath()));
        _Log("[server] Config saved to server_config.bin");
    }

    void LoadConfig(std::function<void(const std::string&)> addRoomFn = nullptr)
    {
        Bin::Reader r;
        if (!r.load(std::format("{}../../../data/server_config.bin", SDL::GetBasePath()))) return;
        uint32_t magic; if (!r.u32(magic) || magic != MAGIC_SRVC) return;
        uint32_t mc, mr, rc;
        if (!r.u32(mc) || !r.u32(mr) || !r.u32(rc)) return;
        {
            std::lock_guard lock(m_mtx);
            m_maxClients = static_cast<int>(mc);
            m_maxPerRoom = static_cast<int>(mr);
        }
        for (uint32_t i = 0; i < rc; ++i) {
            std::string name;
            if (!r.str(name)) break;
            if (addRoomFn) addRoomFn(name);
            else {
                std::lock_guard lock(m_mtx);
                Room& rm = m_rooms.emplace_back();
                rm.id   = m_nextRoomId++;
                rm.name = name;
                rm.history = LoadRoomHistory(rm.id);
            }
        }
        std::lock_guard lock(m_mtx);
        _Log("[server] Config loaded from server_config.bin");
    }

    std::vector<ClientInfo> GetClients()
    {
        std::lock_guard lock(m_mtx);
        std::vector<ClientInfo> out;
        for (auto& p : m_peers) out.push_back(p->info);
        return out;
    }

    std::vector<Room> GetRooms()
    {
        std::lock_guard lock(m_mtx);
        return m_rooms;
    }

    std::vector<std::string> DrainLog()
    {
        std::lock_guard lock(m_mtx);
        return std::exchange(m_log, {});
    }

    std::vector<std::string> DrainDebug()
    {
        std::lock_guard lock(m_mtx);
        return std::exchange(m_debug, {});
    }

private:
    std::atomic<bool>                  m_running{false};
    std::thread                        m_thread;
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::vector<Room>                  m_rooms;
    std::vector<Announcement>          m_announcements;
    std::vector<std::string>           m_log;
    std::vector<std::string>           m_debug;
    int                                m_nextClientId = 1;
    int                                m_nextRoomId   = 1;

    void _Log(const std::string& s)
    {
        m_log.push_back(s);
        SDL::Log(s);
    }

    void _Debug(const std::string& s)
    {
        m_debug.push_back(s);
    }

    void _Run(Uint16 port)
    {
        SDL::Server server{nullptr, port};
        {
            std::lock_guard lock(m_mtx);
            _Log(std::format("[server] Listening on port {}", port));
        }

        while (m_running) {
            SDL::StreamSocket newSock = server.AcceptClient();
            if (newSock) {
                std::lock_guard lock(m_mtx);
                if (static_cast<int>(m_peers.size()) < m_maxClients) {
                    auto p  = std::make_unique<Peer>();
                    p->sock = std::move(newSock);
                    m_peers.push_back(std::move(p));
                    _Log("[server] New connection pending HELLO");
                }
            }

            std::vector<void*> vsocks;
            {
                std::lock_guard lock(m_mtx);
                for (auto& p : m_peers)
                    vsocks.push_back(p->sock.Get());
            }

            if (!vsocks.empty())
                SDL::WaitUntilInputAvailable(vsocks.data(), static_cast<int>(vsocks.size()), 20);
            else
                SDL_Delay(20);

            {
                std::lock_guard lock(m_mtx);
                for (auto& p : m_peers) {
                    if (!p->sock) continue;
                    p->framer.Fill(p->sock);
                    std::vector<Uint8> msg;
                    while (p->framer.Receive(msg))
                        _HandleMessage(*p, msg);
                }
                m_peers.erase(
                    std::remove_if(m_peers.begin(), m_peers.end(),
                        [](const std::unique_ptr<Peer>& p){ return !p->sock; }),
                    m_peers.end());
                _CheckAnnouncements();
            }
        }

        std::lock_guard lock(m_mtx);
        _Log("[server] Stopped.");
    }

    void _HandleMessage(Peer& p, const std::vector<Uint8>& msg)
    {
        using T = Protocol::MsgType;
        _Debug(std::format("→ [{}] type=0x{:02X}", p.info.id, static_cast<int>(Protocol::Type(msg))));
        switch (Protocol::Type(msg)) {
        case T::HELLO: {
            p.info.id       = m_nextClientId++;
            p.info.nickname = Protocol::Get(msg, "nickname");
            if (p.info.nickname.empty())
                p.info.nickname = "user" + std::to_string(p.info.id);
            auto welcome = Protocol::Make(T::WELCOME, {
                {"id",       std::to_string(p.info.id)},
                {"nickname", p.info.nickname}
            });
            SDL::MessageFramer::Send(p.sock, welcome.data(), static_cast<int>(welcome.size()));
            _BroadcastClientList();
            _SendRoomList(p);
            _Log(std::format("[server] Client {} ({}) joined", p.info.id, p.info.nickname));
            break;
        }
        case T::JOIN_ROOM: {
            int rid = 0;
            try { rid = std::stoi(Protocol::Get(msg, "room_id")); } catch (...) { break; }
            if (p.info.roomId >= 0) _LeaveRoomLocked(p);
            auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                                   [rid](const Room& r){ return r.id == rid; });
            if (it != m_rooms.end() &&
                static_cast<int>(it->memberIds.size()) < m_maxPerRoom) {
                it->memberIds.push_back(p.info.id);
                p.info.roomId = rid;
                _SendRoomUsers(rid);
                _SendRoomHistory(p, *it);
            }
            break;
        }
        case T::LEAVE_ROOM:
            _LeaveRoomLocked(p);
            break;
        case T::ROOM_MSG: {
            if (p.info.roomId < 0) break;
            std::string text = Protocol::Get(msg, "text");
            RoomMessage rm{p.info.id, p.info.nickname, text};
            // Store in history
            for (auto& r : m_rooms) {
                if (r.id != p.info.roomId) continue;
                r.history.push_back(rm);
                if (r.history.size() > MAX_HISTORY) r.history.pop_front();
                SaveRoomHistory(r.id, r.history);
            }
            auto fwd = Protocol::Make(T::ROOM_MSG, {
                {"from",     std::to_string(p.info.id)},
                {"nickname", p.info.nickname},
                {"text",     text}
            });
            _BroadcastRoom(p.info.roomId, fwd);
            break;
        }
        case T::DM_INVITE: {
            int toId = 0;
            try { toId = std::stoi(Protocol::Get(msg, "to")); } catch (...) { break; }
            auto fwd = Protocol::Make(T::DM_INVITE, {
                {"from",     std::to_string(p.info.id)},
                {"nickname", p.info.nickname},
                {"port",     Protocol::Get(msg, "port")}
            });
            _Debug(std::format("← DM_INVITE relay from {} to {}", p.info.id, toId));
            _SendToPeer(toId, fwd);
            break;
        }
        case T::DM_ACCEPT: {
            int toId = 0;
            try { toId = std::stoi(Protocol::Get(msg, "to")); } catch (...) { break; }
            auto fwd = Protocol::Make(T::DM_ACCEPT, {
                {"from",     std::to_string(p.info.id)},
                {"nickname", p.info.nickname},
                {"port",     Protocol::Get(msg, "port")}
            });
            _Debug(std::format("← DM_ACCEPT relay from {} to {}", p.info.id, toId));
            _SendToPeer(toId, fwd);
            break;
        }
        case T::SET_STATUS:
            p.info.status = Protocol::Get(msg, "status");
            _BroadcastClientList();
            break;
        case T::CREATE_ROOM: {
            std::string name = Protocol::Get(msg, "name");
            if (!name.empty()) {
                Room& r = m_rooms.emplace_back();
                r.id   = m_nextRoomId++;
                r.name = name;
                _BroadcastRoomList();
                _Log(std::format("[server] Room '{}' created by {}", name, p.info.nickname));
            }
            break;
        }
        case T::DELETE_ROOM: {
            int rid = 0;
            try { rid = std::stoi(Protocol::Get(msg, "room_id")); } catch (...) { break; }
            _DeleteRoomLocked(rid);
            break;
        }
        case T::PING: break;
        default: break;
        }
    }

    // ── Helpers — all called under m_mtx ─────────────────────────────────────

    void _SendToPeer(int clientId, const std::vector<Uint8>& msg)
    {
        for (auto& p : m_peers)
            if (p->info.id == clientId)
                SDL::MessageFramer::Send(p->sock, msg.data(), static_cast<int>(msg.size()));
    }

    void _BroadcastAll(const std::vector<Uint8>& msg)
    {
        for (auto& p : m_peers)
            SDL::MessageFramer::Send(p->sock, msg.data(), static_cast<int>(msg.size()));
    }

    void _BroadcastRoom(int roomId, const std::vector<Uint8>& msg)
    {
        for (auto& r : m_rooms)
            if (r.id == roomId)
                for (int cid : r.memberIds) _SendToPeer(cid, msg);
    }

    void _BroadcastClientList()
    {
        std::string list;
        for (auto& p : m_peers)
            list += std::to_string(p->info.id) + ":" + p->info.nickname + ":" + p->info.status + ",";
        if (!list.empty()) list.pop_back();
        _BroadcastAll(Protocol::Make(Protocol::MsgType::CLIENT_LIST, {{"clients", list}}));
    }

    void _BroadcastRoomList()
    {
        std::string list;
        for (auto& r : m_rooms)
            list += std::to_string(r.id) + ":" + r.name + ",";
        if (!list.empty()) list.pop_back();
        _BroadcastAll(Protocol::Make(Protocol::MsgType::ROOM_LIST, {{"rooms", list}}));
    }

    void _SendRoomList(Peer& p)
    {
        std::string list;
        for (auto& r : m_rooms)
            list += std::to_string(r.id) + ":" + r.name + ",";
        if (!list.empty()) list.pop_back();
        auto msg = Protocol::Make(Protocol::MsgType::ROOM_LIST, {{"rooms", list}});
        SDL::MessageFramer::Send(p.sock, msg.data(), static_cast<int>(msg.size()));
    }

    void _SendRoomUsers(int roomId)
    {
        for (auto& r : m_rooms) {
            if (r.id != roomId) continue;
            std::string list;
            for (int cid : r.memberIds)
                for (auto& p : m_peers)
                    if (p->info.id == cid)
                        list += std::to_string(cid) + ":" + p->info.nickname + ",";
            if (!list.empty()) list.pop_back();
            auto msg = Protocol::Make(Protocol::MsgType::ROOM_USERS, {{"users", list}});
            for (int cid : r.memberIds) _SendToPeer(cid, msg);
        }
    }

    void _SendRoomHistory(Peer& p, const Room& room)
    {
        // Send last SEND_HISTORY messages as regular ROOM_MSG with hist=1
        size_t start = room.history.size() > SEND_HISTORY
                       ? room.history.size() - SEND_HISTORY : 0;
        _Debug(std::format("← sending {} history msgs for room {}", room.history.size() - start, room.id));
        for (size_t i = start; i < room.history.size(); ++i) {
            const auto& m = room.history[i];
            auto msg = Protocol::Make(Protocol::MsgType::ROOM_MSG, {
                {"hist",     "1"},
                {"from",     std::to_string(m.fromId)},
                {"nickname", m.nickname},
                {"text",     m.text}
            });
            SDL::MessageFramer::Send(p.sock, msg.data(), static_cast<int>(msg.size()));
        }
    }

    void _LeaveRoomLocked(Peer& p)
    {
        if (p.info.roomId < 0) return;
        int rid = p.info.roomId;
        for (auto& r : m_rooms)
            if (r.id == rid)
                r.memberIds.erase(
                    std::remove(r.memberIds.begin(), r.memberIds.end(), p.info.id),
                    r.memberIds.end());
        p.info.roomId = -1;
        _SendRoomUsers(rid);
    }

    void _DeleteRoomLocked(int roomId)
    {
        auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                               [roomId](const Room& r){ return r.id == roomId; });
        if (it == m_rooms.end()) return;
        for (int cid : it->memberIds)
            _SendToPeer(cid, Protocol::Make(Protocol::MsgType::KICK));
        m_rooms.erase(it);
        _BroadcastRoomList();
    }

    void _RemovePeerLocked(int clientId)
    {
        for (auto& p : m_peers)
            if (p->info.id == clientId) {
                _LeaveRoomLocked(*p);
                p->sock = SDL::StreamSocket{};
                break;
            }
        _BroadcastClientList();
    }

    void _CheckAnnouncements()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto tt  = system_clock::to_time_t(now);
        struct tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &tt);
#else
        localtime_r(&tt, &lt);
#endif
        for (auto& ann : m_announcements) {
            if (lt.tm_hour != ann.hour || lt.tm_min != ann.minute) continue;
            long long elapsed = duration_cast<minutes>(now - ann.lastSent).count();
            long long needed  = (ann.repeatMins > 0 ? ann.repeatMins : 1440);
            if (ann.lastSent.time_since_epoch().count() != 0 && elapsed < needed) continue;
            ann.lastSent = now;
            _BroadcastAll(Protocol::Make(Protocol::MsgType::ANNOUNCE, {{"text", ann.text}}));
            _Log(std::format("[announce] {}", ann.text));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ChatClient  (background thread)
// ─────────────────────────────────────────────────────────────────────────────
struct ChatClient {
    int         myId   = 0;
    std::string myNick;
    int         roomId = -1;

    struct DmConn {
        SDL::StreamSocket  sock;
        SDL::MessageFramer framer;
        int         peerId   = 0;
        std::string peerNick;
        std::vector<std::string> history;
    };

    struct PendingInvite {
        int         fromId   = 0;
        std::string fromNick;
        Uint16      port     = 0;
    };

    std::mutex               m_mtx;
    SDL::StreamSocket        m_sock;
    SDL::MessageFramer       m_framer;
    std::vector<std::vector<Uint8>> m_inbox;
    std::unique_ptr<DmConn>  m_dm;
    std::optional<PendingInvite> m_pendingInvite;
    std::string              m_serverHost;

    bool Connect(const std::string& host, Uint16 port, const std::string& nickname)
    {
        SDL::Address addr{host.c_str()};
        if (addr.WaitUntilResolved(5000) != SDL::NET_SUCCESS_STATUS) return false;
        m_sock = SDL::StreamSocket{addr, port};
        if (m_sock.WaitUntilConnected(5000) != SDL::NET_SUCCESS_STATUS) return false;
        myNick       = nickname;
        m_serverHost = host;
        auto hello = Protocol::Make(Protocol::MsgType::HELLO, {{"nickname", nickname}});
        SDL::MessageFramer::Send(m_sock, hello.data(), static_cast<int>(hello.size()));
        m_running = true;
        m_thread  = std::thread([this]{ _Run(); });
        return true;
    }

    void Disconnect()
    {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard lock(m_mtx);
        m_sock = SDL::StreamSocket{};
        m_dm.reset();
    }

    void SendToServer(const std::vector<Uint8>& msg)
    {
        std::lock_guard lock(m_mtx);
        SDL::MessageFramer::Send(m_sock, msg.data(), static_cast<int>(msg.size()));
    }

    std::vector<std::vector<Uint8>> DrainInbox()
    {
        std::lock_guard lock(m_mtx);
        return std::exchange(m_inbox, {});
    }

    std::optional<PendingInvite> TakePendingInvite()
    {
        std::lock_guard lock(m_mtx);
        return std::exchange(m_pendingInvite, std::nullopt);
    }

    void InviteDm(int targetId)
    {
        Uint16 dmPort = static_cast<Uint16>(10000 + myId);
        std::thread([this, targetId, dmPort]() {
            SDL::Server srv{nullptr, dmPort};
            for (int i = 0; i < 300 && m_running.load(); ++i) {
                SDL::StreamSocket peer = srv.AcceptClient();
                if (peer) {
                    std::lock_guard lock(m_mtx);
                    if (!m_dm) m_dm = std::make_unique<DmConn>();
                    m_dm->peerId   = targetId;
                    m_dm->peerNick = "peer" + std::to_string(targetId);
                    m_dm->sock     = std::move(peer);
                    _LoadDmHistory(*m_dm);
                    return;
                }
                SDL_Delay(100);
            }
        }).detach();

        auto inv = Protocol::Make(Protocol::MsgType::DM_INVITE, {
            {"to",   std::to_string(targetId)},
            {"port", std::to_string(dmPort)}
        });
        SendToServer(inv);
    }

    void AcceptDm(int fromId, const std::string& fromNick, Uint16 port)
    {
        {
            std::lock_guard lock(m_mtx);
            if (!m_dm) m_dm = std::make_unique<DmConn>();
            m_dm->peerId   = fromId;
            m_dm->peerNick = fromNick;
            _LoadDmHistory(*m_dm);
        }
        std::thread([this, fromId, port]() {
            SDL::Address addr{m_serverHost.c_str()};
            if (addr.WaitUntilResolved(5000) != SDL::NET_SUCCESS_STATUS) return;
            SDL::StreamSocket sock{addr, port};
            if (sock.WaitUntilConnected(5000) != SDL::NET_SUCCESS_STATUS) return;
            std::lock_guard lock(m_mtx);
            if (m_dm && m_dm->peerId == fromId)
                m_dm->sock = std::move(sock);
        }).detach();

        Uint16 myDmPort = static_cast<Uint16>(10000 + myId);
        auto acc = Protocol::Make(Protocol::MsgType::DM_ACCEPT, {
            {"to",   std::to_string(fromId)},
            {"port", std::to_string(myDmPort)}
        });
        SendToServer(acc);
    }

    void SendDm(const std::string& text)
    {
        std::lock_guard lock(m_mtx);
        if (!m_dm || !m_dm->sock) return;
        auto msg = Protocol::Make(Protocol::MsgType::DM_MSG, {
            {"from", std::to_string(myId)},
            {"text", text}
        });
        SDL::MessageFramer::Send(m_dm->sock, msg.data(), static_cast<int>(msg.size()));
        m_dm->history.push_back("Me: " + text);
        _SaveDmHistory(*m_dm);
    }

    std::vector<std::string> GetDmHistory()
    {
        std::lock_guard lock(m_mtx);
        return m_dm ? m_dm->history : std::vector<std::string>{};
    }

    void CloseDm()
    {
        std::lock_guard lock(m_mtx);
        m_dm.reset();
    }

    // ── Config persistence ────────────────────────────────────────────────────

    struct Config {
        std::string host = "127.0.0.1";
        Uint16      port = 9900;
        std::string nickname;
        std::vector<std::pair<int,std::string>> contacts;
    };

    static void SaveConfig(const Config& cfg)
    {
        Bin::Writer w;
        w.u32(MAGIC_CLNT);
        w.str(cfg.host);
        w.u16(cfg.port);
        w.str(cfg.nickname);
        w.u32(static_cast<uint32_t>(cfg.contacts.size()));
        for (auto& [id, name] : cfg.contacts) {
            w.u32(static_cast<uint32_t>(id));
            w.str(name);
        }
        w.save(std::format("{}../../../data/client_config.bin", SDL::GetBasePath()));
    }

    static Config LoadConfig()
    {
        Config cfg;
        Bin::Reader r;
        if (!r.load(std::format("{}../../../data/client_config.bin", SDL::GetBasePath()))) return cfg;
        uint32_t magic; if (!r.u32(magic) || magic != MAGIC_CLNT) return cfg;
        r.str(cfg.host);
        uint16_t p; r.u16(p); cfg.port = p;
        r.str(cfg.nickname);
        uint32_t cc; if (!r.u32(cc)) return cfg;
        for (uint32_t i = 0; i < cc; ++i) {
            uint32_t id; std::string name;
            if (!r.u32(id) || !r.str(name)) break;
            cfg.contacts.emplace_back(static_cast<int>(id), name);
        }
        return cfg;
    }

private:
    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    static void _LoadDmHistory(DmConn& dm)
    {
        Bin::Reader r;
        if (!r.load(std::format("{}../../../data/dm_{}.bin", SDL::GetBasePath(), dm.peerId))) return;
        uint32_t magic; if (!r.u32(magic) || magic != MAGIC_DMMG) return;
        uint32_t count; if (!r.u32(count)) return;
        dm.history.clear();
        for (uint32_t i = 0; i < count; ++i) {
            std::string line;
            if (!r.str(line)) break;
            dm.history.push_back(line);
        }
    }

    static void _SaveDmHistory(const DmConn& dm)
    {
        Bin::Writer w;
        w.u32(MAGIC_DMMG);
        // keep last 200 DM messages
        size_t start = dm.history.size() > 200 ? dm.history.size() - 200 : 0;
        w.u32(static_cast<uint32_t>(dm.history.size() - start));
        for (size_t i = start; i < dm.history.size(); ++i)
            w.str(dm.history[i]);
        w.save(std::format("{}../../../data/dm_{}.bin", SDL::GetBasePath(), dm.peerId));
    }

    void _Run()
    {
        while (m_running) {
            void* vsock = nullptr;
            {
                std::lock_guard lock(m_mtx);
                if (!m_sock) break;
                vsock = m_sock.Get();
            }
            SDL::WaitUntilInputAvailable(&vsock, 1, 50);
            {
                std::lock_guard lock(m_mtx);
                if (!m_sock) break;
                m_framer.Fill(m_sock);
                std::vector<Uint8> msg;
                while (m_framer.Receive(msg))
                    m_inbox.push_back(msg);
                if (m_dm && m_dm->sock) {
                    m_dm->framer.Fill(m_dm->sock);
                    std::vector<Uint8> dm;
                    while (m_dm->framer.Receive(dm)) {
                        std::string text = Protocol::Get(dm, "text");
                        if (!text.empty()) {
                            m_dm->history.push_back(m_dm->peerNick + ": " + text);
                            _SaveDmHistory(*m_dm);
                        }
                    }
                }
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Main application
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
    static constexpr SDL::Point kWinSz = {1280, 720};

    enum class Mode { Launcher, Server, Client };
    Mode m_mode = Mode::Launcher;

    // ── SDL callbacks ─────────────────────────────────────────────────────────

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::SetAppMetadata("SDL3pp Chat", "1.0", "com.example.chat");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        SDL::NET::Init();
        *out = new Main(args);
        return SDL::APP_CONTINUE;
    }
    static void Quit(Main* m, SDL::AppResult) {
        delete m;
        SDL::NET::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }
    static SDL::Window MakeWindow() {
        return SDL::CreateWindowAndRenderer(
            "SDL3pp - Chat", kWinSz, SDL_WINDOW_RESIZABLE, nullptr);
    }

    // ── Core objects ──────────────────────────────────────────────────────────

    SDL::Window      window  { MakeWindow()         };
    SDL::RendererRef renderer{ window.GetRenderer() };
    SDL::ResourceManager rm;
    SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };
    SDL::ECS::Context    ecs_ctx;
    SDL::UI::System      ui{ ecs_ctx, renderer, {}, pool };
    SDL::FrameTimer      m_frameTimer{ 60.f };
    std::string dataPath;
    std::string assetsPath;

    // ── Application logic ─────────────────────────────────────────────────────

    ChatServer  m_server;
    ChatClient  m_client;
    Uint16      m_port = ChatServer::BASE_PORT;
    std::string m_host = "127.0.0.1";

    bool m_debugMode = false;

    std::vector<std::tuple<int,std::string,std::string>> m_clients;
    std::vector<std::pair<int,std::string>>              m_rooms;
    std::vector<std::string>                             m_chatHistory;
    std::vector<Announcement>                            m_annCache;
    std::vector<std::pair<int,std::string>>              m_savedContacts;

    // ── UI entity IDs — Launcher ──────────────────────────────────────────────
    SDL::ECS::EntityId id_launcherPanel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_nickInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_hostInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_portInput     = SDL::ECS::NullEntity;

    // ── UI entity IDs — Server ────────────────────────────────────────────────
    SDL::ECS::EntityId id_serverPanel     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvClientList   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvRoomList     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvLogArea      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvRoomName     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvMaxClients   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvMaxPerRoom   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvAnnText      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvAnnHour      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvAnnMin       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvAnnRepeat    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvAnnList      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvMaxCliLabel  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_srvMaxRoomLabel = SDL::ECS::NullEntity;

    // ── UI entity IDs — Client ────────────────────────────────────────────────
    SDL::ECS::EntityId id_clientPanel    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliRoomList    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliUserList    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliChatArea    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliMsgInput    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliStatusLabel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmHistory   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmTarget    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliInvitePanel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliInviteLabel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliContactList = SDL::ECS::NullEntity;

    // ── UI entity IDs — Debug ─────────────────────────────────────────────────
    SDL::ECS::EntityId id_debugPanel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_debugArea  = SDL::ECS::NullEntity;

    // ── Constructor ───────────────────────────────────────────────────────────

    explicit Main(SDL::AppArgs args) {
        Mode forcedMode = Mode::Launcher;
        m_debugMode = false;
        for (auto arg : args) {
            std::string a{arg};
            if      (a == "--server")            forcedMode = Mode::Server;
            else if (a == "--client")            forcedMode = Mode::Client;
            else if (a.substr(0,7) == "--port=") m_port = static_cast<Uint16>(std::stoi(a.substr(7)));
            else if (a.substr(0,7) == "--host=") m_host = a.substr(7);
            else if (a == "--debug") m_debugMode = true;
        }
        window.StartTextInput();
        _LoadResources();
        _BuildUI();
        m_frameTimer.Begin();
        if      (forcedMode == Mode::Server) _StartServer();
        else if (forcedMode == Mode::Client) {
            _LoadClientConfig();
            _SwitchTo(Mode::Client);
        }
    }

    ~Main() {
        m_server.Stop();
        m_client.Disconnect();
        pool.Release();
    }

    // ── Resources ─────────────────────────────────────────────────────────────

    void _LoadResources() {
        // Create data repertory if dosen't existe
        dataPath = std::string(SDL::GetBasePath()) + "../../../data/";
        SDL::EnsureDirectoryExists(dataPath);

        assetsPath = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("font", assetsPath + "fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 13.f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI construction
    // ─────────────────────────────────────────────────────────────────────────

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f)).Radius(SDL::FCorners(0.f))
            .Children(
                _BuildHeader(),
                id_launcherPanel = _BuildLauncher(),
                id_serverPanel   = _BuildServerView(),
                id_clientPanel   = _BuildClientView(),
                id_debugPanel    = _BuildDebugPanel())
            .AsRoot();
        _SwitchTo(Mode::Launcher);
    }

    SDL::ECS::EntityId _BuildHeader() {
        return ui.Row("header", 8.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(48.f)
            .PaddingH(16.f).PaddingV(0.f)
            .BgColor(pal::HEADER)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BorderColor(pal::BORDER)
            .Children(
                ui.Label("app_title", "SDL3pp Chat")
                    .TextColor(pal::ACCENT).Font("font", 16.f).Grow(100.f),
                ui.Label("app_sub", "P2P Chat Demo")
                    .TextColor(pal::GREY));
    }

    SDL::ECS::EntityId _BuildLauncher() {
        auto panel = ui.Column("launcher_panel", 16.f, 0.f)
            .Grow(100.f).BgColor(pal::BG).Borders(SDL::FBox(0.f))
            .AlignChildrenH(SDL::UI::Align::Center)
            .PaddingH(0.f).PaddingV(60.f);

        auto box = ui.Column("launcher_box", 12.f, 0.f)
            .Font("font", 14.f)
            .W(380.f).BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(10.f))
            .PaddingH(24.f).PaddingV(20.f);

        box.Child(ui.Label("lbl_welcome", "Welcome to SDL3pp Chat")
            .TextColor(pal::WHITE).Font("font", 18.f)
            .AlignH(SDL::UI::Align::Center));

        box.Child(ui.Label("lbl_nick", "Nickname").TextColor(pal::GREY));
        id_nickInput = ui.Input("nick_input", "Enter your nickname…")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_nickInput);

        box.Child(ui.Label("lbl_host", "Server host (client mode)").TextColor(pal::GREY));
        id_hostInput = ui.Input("host_input", "127.0.0.1")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_hostInput);

        box.Child(ui.Label("lbl_port", "Port").TextColor(pal::GREY));
        id_portInput = ui.Input("port_input", "9900")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_portInput);

        box.Child(ui.Row("btn_row", 10.f, 0.f)
            .W(SDL::UI::Value::Pcw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                ui.Button("start_server_btn", "Start Server")
                    .Grow(50.f).H(38.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
                    .OnClick([this]{ _StartServer(); }),
                ui.Button("start_client_btn", "Connect as Client")
                    .Grow(50.f).H(38.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
                    .OnClick([this]{ _ConnectClient(); })));

        panel.Child(box);
        return panel;
    }

    SDL::ECS::EntityId _BuildServerView() {
        auto panel = ui.Row("server_panel", 10.f, 0.f)
            .Grow(100.f).PaddingH(10.f).PaddingV(10.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f));

        // ── Left: Clients + Rooms ─────────────────────────────────────────────
        auto left = ui.Column("srv_left", 10.f, 0.f)
            .Font("font", 14.f)
            .W(260.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        left.Child(ui.SectionTitle("Connected Clients", pal::ACCENT));
        id_srvClientList = ui.ListBoxWidget("srv_client_list")
            .W(SDL::UI::Value::Pw(100.f)).H(160.f)
            .Style(SDL::UI::Theme::Card());
        left.Child(id_srvClientList);

        left.Child(ui.Row("srv_client_btns", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                ui.Button("kick_btn", "Kick")
                    .Grow(100.f).H(30.f)
                    .Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{ _SrvKick(); }),
                ui.Button("status_away_btn", "Away")
                    .Grow(100.f).H(30.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
                    .OnClick([this]{ _SrvSetStatus("away"); }),
                ui.Button("status_online_btn", "Online")
                    .Grow(100.f).H(30.f)
                    .Style(SDL::UI::Theme::SuccessButton())
                    .OnClick([this]{ _SrvSetStatus("online"); })));

        left.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        left.Child(ui.SectionTitle("Discussion Rooms", pal::ACCENT));
        id_srvRoomList = ui.ListBoxWidget("srv_room_list")
            .W(SDL::UI::Value::Pw(100.f)).H(100.f)
            .Style(SDL::UI::Theme::Card());
        left.Child(id_srvRoomList);

        id_srvRoomName = ui.Input("srv_room_name", "New room…").Grow(100.f).H(30.f);
        left.Child(ui.Row("room_ctrl", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                id_srvRoomName,
                ui.Button("srv_add_room", "+").W(30.f).H(30.f)
                    .Tooltip("Add new room")
                    .Style(SDL::UI::Theme::SuccessButton())
                    .OnClick([this]{ _SrvAddRoom(); }),
                ui.Button("srv_del_room", "─").W(30.f).H(30.f)
                    .Tooltip("Remove selected room")
                    .Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{ _SrvDelRoom(); })));

        left.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        left.Child(ui.Button("srv_save_config", "Save Config (binary)")
            .W(SDL::UI::Value::Pw(100.f)).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
            .OnClick([this]{ m_server.SaveConfig(); }));

        panel.Child(left);

        // ── Centre: Server log ────────────────────────────────────────────────
        auto centre = ui.Column("srv_centre", 8.f, 0.f)
            .W(SDL::UI::Value::Auto())
            .Grow(100.f).BgColor(pal::TRANSP)
            .Borders(SDL::FBox(0.f));

        centre.Child(ui.SectionTitle("Server Log", pal::ACCENT));
        id_srvLogArea = ui.TextArea("srv_log_area")
            .W(SDL::UI::Value::Pw(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .Style(SDL::UI::Theme::Card())
            .AutoScrollableY(true)
            .ReadOnly();
        centre.Child(id_srvLogArea);

        panel.Child(centre);

        // ── Right: Settings + Announcements ──────────────────────────────────
        auto right = ui.Column("srv_right", 10.f, 0.f)
            .W(230.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        right.Child(ui.SectionTitle("Settings", pal::ACCENT));

        id_srvMaxCliLabel = ui.Label("srv_max_cli_lbl",
            std::format("Max clients: {}", m_server.m_maxClients))
            .TextColor(pal::GREY).Font("font", 11.f);
        id_srvMaxClients = ui.Slider("srv_max_clients",
            1.f, 64.f, static_cast<float>(m_server.m_maxClients))
            .W(SDL::UI::Value::Pw(100.f)).H(22.f)
            .OnChange([this](float v){
                m_server.m_maxClients = static_cast<int>(v);
                ui.SetText(id_srvMaxCliLabel, std::format("Max clients: {}", m_server.m_maxClients));
            });
        right.Child(id_srvMaxCliLabel).Child(id_srvMaxClients);

        id_srvMaxRoomLabel = ui.Label("srv_max_room_lbl",
            std::format("Max per room: {}", m_server.m_maxPerRoom))
            .TextColor(pal::GREY).Font("font", 11.f);
        id_srvMaxPerRoom = ui.Slider("srv_max_per_room",
            1.f, 32.f, static_cast<float>(m_server.m_maxPerRoom))
            .W(SDL::UI::Value::Pw(100.f)).H(22.f)
            .OnChange([this](float v){
                m_server.m_maxPerRoom = static_cast<int>(v);
                ui.SetText(id_srvMaxRoomLabel, std::format("Max per room: {}", m_server.m_maxPerRoom));
            });
        right.Child(id_srvMaxRoomLabel).Child(id_srvMaxPerRoom);

        right.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        right.Child(ui.SectionTitle("Announcements", pal::ACCENT));

        id_srvAnnText = ui.Input("ann_text", "Announcement text…")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f);
        right.Child(id_srvAnnText);

        id_srvAnnHour   = ui.Input("ann_hour",   "HH").W(40.f).H(28.f);
        id_srvAnnMin    = ui.Input("ann_min",     "MM").W(40.f).H(28.f);
        id_srvAnnRepeat = ui.Input("ann_repeat",  "0" ).W(40.f).H(28.f);
        right.Child(ui.Row("ann_time", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                id_srvAnnHour,
                ui.Label("ann_col", ":").TextColor(pal::GREY),
                id_srvAnnMin,
                ui.Label("ann_rep_lbl", " rep:").TextColor(pal::GREY),
                id_srvAnnRepeat));

        right.Child(ui.Button("ann_add_btn", "Add Announcement")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
            .OnClick([this]{ _SrvAddAnnouncement(); }));

        id_srvAnnList = ui.ListBoxWidget("ann_list")
            .W(SDL::UI::Value::Pw(100.f)).H(80.f)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f));
        right.Child(id_srvAnnList);

        right.Child(ui.Button("ann_del_btn", "Remove Selected")
            .W(SDL::UI::Value::Pw(100.f)).H(26.f)
            .Style(SDL::UI::Theme::DangerButton())
            .OnClick([this]{ _SrvRemoveAnnouncement(); }));

        right.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        right.Child(ui.Button("srv_debug_toggle", "Toggle Debug Panel")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::YELLOW))
            .OnClick([this]{ _ToggleDebug(); }));

        panel.Child(right);
        return panel;
    }

    SDL::ECS::EntityId _BuildClientView() {
        auto panel = ui.Row("client_panel", 10.f, 0.f)
            .Grow(100.f).PaddingH(10.f).PaddingV(10.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f));

        // ── Left: Rooms + Contacts ────────────────────────────────────────────
        auto left = ui.Column("cli_left", 8.f, 0.f)
            .W(190.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        left.Child(ui.SectionTitle("Available Rooms", pal::ACCENT));
        id_cliRoomList = ui.ListBoxWidget("cli_room_list")
            .W(SDL::UI::Value::Pw(100.f)).H(100.f)
            .Style(SDL::UI::Theme::Card())
            .OnClick([this]{ _CliJoinRoom(); });
        left.Child(id_cliRoomList);

        left.Child(ui.Button("cli_leave_room", "Leave Room")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::DangerButton())
            .OnClick([this]{
                m_client.roomId = -1;
                m_client.SendToServer(Protocol::Make(Protocol::MsgType::LEAVE_ROOM));
            }));

        left.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        left.Child(ui.SectionTitle("Address Book", pal::ACCENT));
        id_cliContactList = ui.ListBoxWidget("cli_contact_list")
            .W(SDL::UI::Value::Pw(100.f)).H(90.f)
            .Style(SDL::UI::Theme::Card())
            .OnClick([this]{
                int sel = ui.GetListBoxSelection(id_cliContactList);
                if (sel >= 0 && sel < static_cast<int>(m_savedContacts.size()))
                    ui.SetText(id_cliDmTarget, std::to_string(m_savedContacts[sel].first));
            });
        left.Child(id_cliContactList);

        left.Child(ui.Row("cli_contact_btns", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                ui.Button("cli_add_contact", "Add")
                    .Grow(100.f).H(26.f)
                    .Style(SDL::UI::Theme::SuccessButton())
                    .OnClick([this]{ _CliAddContact(); }),
                ui.Button("cli_del_contact", "Del")
                    .Grow(100.f).H(26.f)
                    .Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{ _CliDelContact(); })));

        left.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        id_cliStatusLabel = ui.Label("cli_status", "Not connected")
            .TextColor(pal::GREY).Font("font", 11.f);
        left.Child(id_cliStatusLabel);

        left.Child(ui.Row("cli_status_btns", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                ui.Button("cli_status_online", "Online")
                    .Grow(100.f).H(26.f).Style(SDL::UI::Theme::SuccessButton())
                    .OnClick([this]{ m_client.SendToServer(Protocol::Make(Protocol::MsgType::SET_STATUS, {{"status","online"}})); }),
                ui.Button("cli_status_away", "Away")
                    .Grow(100.f).H(26.f).Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
                    .OnClick([this]{ m_client.SendToServer(Protocol::Make(Protocol::MsgType::SET_STATUS, {{"status","away"}})); }),
                ui.Button("cli_status_busy", "Busy")
                    .Grow(100.f).H(26.f).Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{ m_client.SendToServer(Protocol::Make(Protocol::MsgType::SET_STATUS, {{"status","busy"}})); })));

        left.Child(ui.Row("cli_save_debug_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                ui.Button("cli_save_btn", "Save Profile")
                    .Grow(100.f).H(28.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
                    .OnClick([this]{ _SaveClientConfig(); }),
                ui.Button("cli_debug_toggle", "Debug")
                    .W(56.f).H(28.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::YELLOW))
                    .OnClick([this]{ _ToggleDebug(); })));

        panel.Child(left);

        // ── Centre: Chat area ─────────────────────────────────────────────────
        auto centre = ui.Column("cli_centre", 8.f, 0.f)
            .Grow(100.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        centre.Child(ui.SectionTitle("Room Chat", pal::ACCENT));
        id_cliChatArea = ui.TextArea("cli_chat_area")
            .Grow(100.f).W(SDL::UI::Value::Pw(100.f))
            .Style(SDL::UI::Theme::Card()).AutoScrollableY(true).ReadOnly();
        centre.Child(id_cliChatArea);

        id_cliMsgInput = ui.Input("cli_msg_input", "Write a message…").Grow(100.f).H(36.f);
        centre.Child(ui.Row("msg_row", 8.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                id_cliMsgInput,
                ui.Button("cli_send_btn", "Send")
                    .W(80.f).H(36.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
                    .OnClick([this]{ _CliSendMsg(); })));

        panel.Child(centre);

        // ── Right: Users + DM ─────────────────────────────────────────────────
        auto right = ui.Column("cli_right", 8.f, 0.f)
            .W(230.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        right.Child(ui.SectionTitle("Room Users", pal::ACCENT));
        id_cliUserList = ui.ListBoxWidget("cli_user_list")
            .W(SDL::UI::Value::Pw(100.f)).H(110.f)
            .Style(SDL::UI::Theme::Card());
        right.Child(id_cliUserList);

        right.Child(ui.Separator().H(1.f).BgColor(pal::BORDER));

        right.Child(ui.SectionTitle("Direct Messages", pal::ACCENT));

        id_cliDmHistory = ui.TextArea("cli_dm_history")
            .Grow(100.f).W(SDL::UI::Value::Pw(100.f))
            .Style(SDL::UI::Theme::Card()).AutoScrollableY(true).ReadOnly();
        right.Child(id_cliDmHistory);

        // DM invite notification (hidden by default)
        id_cliInviteLabel = ui.Label("invite_label", "")
            .TextColor({10,10,10,255}).Font("font", 11.f);
        auto invitePanel = ui.Column("invite_panel", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::ORANGE).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f)).PaddingH(8.f).PaddingV(6.f)
            .Visible(false)
            .Children(
                id_cliInviteLabel,
                ui.Button("invite_accept", "Accept")
                    .W(SDL::UI::Value::Pw(100.f)).H(26.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
                    .OnClick([this]{ _CliAcceptDm(); }),
                ui.Button("invite_decline", "Decline")
                    .W(SDL::UI::Value::Pw(100.f)).H(26.f)
                    .Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{ _CliDeclineDm(); }));
        id_cliInvitePanel = invitePanel;
        right.Child(invitePanel);

        // DM controls row: [target ID] [message input] [invite/send btn] [close btn]
        id_cliDmTarget = ui.Input("cli_dm_target", "ID").W(44.f).H(30.f);
        id_cliDmInput  = ui.Input("cli_dm_input",  "DM…").Grow(100.f).H(30.f);
        right.Child(ui.Row("dm_ctrl_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f))
            .Children(
                id_cliDmTarget,
                id_cliDmInput,
                ui.Button("cli_dm_send", "▶")
                    .W(30.f).H(30.f)
                    .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
                    .OnClick([this]{
                        bool hasDm;
                        { std::lock_guard lock(m_client.m_mtx); hasDm = m_client.m_dm != nullptr; }
                        if (!hasDm) _CliInviteDm(); else _CliSendDm();
                    }),
                ui.Button("cli_dm_close", "✕")
                    .W(30.f).H(30.f)
                    .Style(SDL::UI::Theme::DangerButton())
                    .OnClick([this]{
                        m_client.CloseDm();
                        ui.SetTextAreaContent(id_cliDmHistory, "");
                    })));

        panel.Child(right);
        return panel;
    }

    SDL::ECS::EntityId _BuildDebugPanel() {
        auto panel = ui.Column("debug_panel", 4.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(150.f)
            .BgColor({8, 8, 16, 255})
            .Borders(SDL::FBox(0.f, 1.f, 0.f, 0.f)).BorderColor(pal::YELLOW)
            .PaddingH(8.f).PaddingV(4.f)
            .Visible(false);

        panel.Child(ui.Label("debug_title", "Debug — Protocol Traffic")
            .TextColor(pal::YELLOW).Font("font", 11.f));

        id_debugArea = ui.TextArea("debug_area")
            .Grow(100.f).W(SDL::UI::Value::Pw(100.f))
            .BgColor({6, 6, 12, 255}).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(4.f)).AutoScrollableY(true).ReadOnly()
            .Font("font", 10.f);
        panel.Child(id_debugArea);

        return panel;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Panel switching
    // ─────────────────────────────────────────────────────────────────────────

    void _SwitchTo(Mode mode) {
        m_mode = mode;
        ui.SetVisible(id_launcherPanel, mode == Mode::Launcher);
        ui.SetVisible(id_serverPanel,   mode == Mode::Server);
        ui.SetVisible(id_clientPanel,   mode == Mode::Client);
        if (mode == Mode::Launcher) {
            ui.SetVisible(id_debugPanel, m_debugMode);
        }
    }

    void _ToggleDebug() {
        m_debugMode = !m_debugMode;
        ui.SetVisible(id_debugPanel, m_debugMode);
    }

    void _DebugLog(const std::string& s) {
        if (!m_debugMode) return;
        std::string cur = ui.GetTextAreaContent(id_debugArea);
        cur += s + '\n';
        // trim to last 5000 chars to avoid unbounded growth
        if (cur.size() > 5000) cur = cur.substr(cur.size() - 5000);
        ui.SetTextAreaContent(id_debugArea, cur);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Launcher actions
    // ─────────────────────────────────────────────────────────────────────────

    void _ReadLauncherInputs() {
        std::string ps = ui.GetText(id_portInput);
        if (!ps.empty()) try { m_port = static_cast<Uint16>(std::stoi(ps)); } catch (...) {}
        std::string hs = ui.GetText(id_hostInput);
        if (!hs.empty()) m_host = hs;
    }

    std::string _GetNick() {
        std::string n = ui.GetText(id_nickInput);
        return n.empty() ? "user" : n;
    }

    void _StartServer() {
        _ReadLauncherInputs();
        m_server.LoadConfig();
        m_server.Start(m_port);
        _SwitchTo(Mode::Server);
        ui.SetText(id_srvMaxCliLabel, std::format("Max clients: {}", m_server.m_maxClients));
        ui.SetText(id_srvMaxRoomLabel, std::format("Max per room: {}", m_server.m_maxPerRoom));
        window.SetTitle("SDL3pp Chat — Server :" + std::to_string(m_port));
    }

    void _ConnectClient() {
        _ReadLauncherInputs();
        std::string nickname = _GetNick();
        if (m_client.Connect(m_host, m_port, nickname)) {
            _SwitchTo(Mode::Client);
            ui.SetText(id_cliStatusLabel,
                std::format("Connected as {} — waiting for server…", nickname));
            window.SetTitle("SDL3pp Chat — " + nickname + " @ " + m_host);
        } else {
            SDL::Log(std::format("[client] Connection failed: {}", SDL_GetError()));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Server-view actions
    // ─────────────────────────────────────────────────────────────────────────

    void _SrvKick() {
        int sel = ui.GetListBoxSelection(id_srvClientList);
        if (sel < 0 || sel >= static_cast<int>(m_clients.size())) return;
        m_server.KickClient(std::get<0>(m_clients[sel]));
    }

    void _SrvSetStatus(const std::string& status) {
        int sel = ui.GetListBoxSelection(id_srvClientList);
        if (sel < 0 || sel >= static_cast<int>(m_clients.size())) return;
        m_server.SetClientStatus(std::get<0>(m_clients[sel]), status);
    }

    void _SrvAddRoom() {
        std::string name = ui.GetText(id_srvRoomName);
        if (name.empty()) return;
        m_server.AddRoom(name);
        ui.SetText(id_srvRoomName, "");
    }

    void _SrvDelRoom() {
        int sel = ui.GetListBoxSelection(id_srvRoomList);
        if (sel < 0 || sel >= static_cast<int>(m_rooms.size())) return;
        m_server.DeleteRoom(m_rooms[sel].first);
    }

    void _SrvAddAnnouncement() {
        std::string text = ui.GetText(id_srvAnnText);
        if (text.empty()) return;
        Announcement ann;
        ann.text = text;
        try { ann.hour       = std::stoi(ui.GetText(id_srvAnnHour));   } catch (...) {}
        try { ann.minute     = std::stoi(ui.GetText(id_srvAnnMin));    } catch (...) {}
        try { ann.repeatMins = std::stoi(ui.GetText(id_srvAnnRepeat)); } catch (...) {}
        ann.hour   = std::clamp(ann.hour,   0, 23);
        ann.minute = std::clamp(ann.minute, 0, 59);
        m_server.AddAnnouncement(ann);
        m_annCache.push_back(ann);
        _RefreshAnnList();
        ui.SetText(id_srvAnnText, "");
    }

    void _SrvRemoveAnnouncement() {
        int sel = ui.GetListBoxSelection(id_srvAnnList);
        if (sel < 0 || sel >= static_cast<int>(m_annCache.size())) return;
        m_server.RemoveAnnouncement(static_cast<size_t>(sel));
        m_annCache.erase(m_annCache.begin() + sel);
        _RefreshAnnList();
    }

    void _RefreshAnnList() {
        std::vector<std::string> items;
        for (auto& a : m_annCache)
            items.push_back(std::format("{:02d}:{:02d} rep={}min — {}",
                a.hour, a.minute, a.repeatMins, a.text));
        ui.SetListBoxItems(id_srvAnnList, std::move(items));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Client-view actions
    // ─────────────────────────────────────────────────────────────────────────

    void _CliJoinRoom() {
        int sel = ui.GetListBoxSelection(id_cliRoomList);
        if (sel < 0 || sel >= static_cast<int>(m_rooms.size())) return;
        m_chatHistory.clear();
        ui.SetTextAreaContent(id_cliChatArea, "");
        m_client.roomId = m_rooms[sel].first;
        m_client.SendToServer(Protocol::Make(Protocol::MsgType::JOIN_ROOM,
            {{"room_id", std::to_string(m_client.roomId)}}));
    }

    void _CliSendMsg() {
        std::string text = ui.GetText(id_cliMsgInput);
        if (text.empty() || m_client.roomId < 0) return;
        m_client.SendToServer(Protocol::Make(Protocol::MsgType::ROOM_MSG, {{"text", text}}));
        _AppendChat("Me: " + text);
        ui.SetText(id_cliMsgInput, "");
        _DebugLog(std::format("→ ROOM_MSG text={}", text));
    }

    void _CliInviteDm() {
        std::string idStr = ui.GetText(id_cliDmTarget);
        if (idStr.empty()) return;
        int targetId = 0;
        try { targetId = std::stoi(idStr); } catch (...) { return; }
        m_client.InviteDm(targetId);
        _DebugLog(std::format("→ DM_INVITE to={}", targetId));
    }

    void _CliAcceptDm() {
        auto inv = m_client.TakePendingInvite();
        if (!inv) return;
        m_client.AcceptDm(inv->fromId, inv->fromNick, inv->port);
        ui.SetVisible(id_cliInvitePanel, false);
        _DebugLog(std::format("→ DM_ACCEPT from={}", inv->fromId));
    }

    void _CliDeclineDm() {
        m_client.TakePendingInvite();
        ui.SetVisible(id_cliInvitePanel, false);
    }

    void _CliSendDm() {
        std::string text = ui.GetText(id_cliDmInput);
        if (text.empty()) return;
        m_client.SendDm(text);
        ui.SetText(id_cliDmInput, "");
        _RefreshDmHistory();
        _DebugLog(std::format("→ DM_MSG text={}", text));
    }

    void _CliAddContact() {
        std::string idStr = ui.GetText(id_cliDmTarget);
        if (idStr.empty()) return;
        int id = 0;
        try { id = std::stoi(idStr); } catch (...) { return; }
        std::string nick = "user" + idStr;
        for (auto& [cid, cname] : m_savedContacts)
            if (cid == id) return; // already in address book
        m_savedContacts.emplace_back(id, nick);
        _RefreshContactList();
    }

    void _CliDelContact() {
        int sel = ui.GetListBoxSelection(id_cliContactList);
        if (sel < 0 || sel >= static_cast<int>(m_savedContacts.size())) return;
        m_savedContacts.erase(m_savedContacts.begin() + sel);
        _RefreshContactList();
    }

    void _RefreshContactList() {
        std::vector<std::string> items;
        for (auto& [id, name] : m_savedContacts)
            items.push_back(std::format("[{}] {}", id, name));
        ui.SetListBoxItems(id_cliContactList, std::move(items));
    }

    void _AppendChat(const std::string& line, bool isHistory = false) {
        m_chatHistory.push_back(line);
        std::string full;
        full.reserve(m_chatHistory.size() * 40);
        for (auto& l : m_chatHistory) { full += l; full += '\n'; }
        ui.SetTextAreaContent(id_cliChatArea, full);
        (void)isHistory;
    }

    void _RefreshDmHistory() {
        auto hist = m_client.GetDmHistory();
        std::string s;
        for (auto& l : hist) { s += l; s += '\n'; }
        ui.SetTextAreaContent(id_cliDmHistory, s);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Client binary config
    // ─────────────────────────────────────────────────────────────────────────

    void _SaveClientConfig() {
        ChatClient::Config cfg;
        cfg.host     = m_host;
        cfg.port     = m_port;
        cfg.nickname = _GetNick();
        cfg.contacts = m_savedContacts;
        ChatClient::SaveConfig(cfg);
        SDL::Log("[client] Config saved to client_config.bin");
    }

    void _LoadClientConfig() {
        auto cfg = ChatClient::LoadConfig();
        if (!cfg.host.empty())     { m_host = cfg.host; ui.SetText(id_hostInput, m_host); }
        if (cfg.port != 0)         { m_port = cfg.port; ui.SetText(id_portInput, std::to_string(m_port)); }
        if (!cfg.nickname.empty()) { ui.SetText(id_nickInput, cfg.nickname); }
        m_savedContacts = cfg.contacts;
        _RefreshContactList();
        SDL::Log("[client] Config loaded from client_config.bin");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Frame loop
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        m_frameTimer.Begin();
        const float dt = m_frameTimer.GetDelta();

        if (m_mode == Mode::Server) _UpdateServer();
        if (m_mode == Mode::Client) _UpdateClient();

        pool.Update();
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        m_frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    void _UpdateServer() {
        auto srvClients = m_server.GetClients();
        m_clients.clear();
        std::vector<std::string> cItems;
        for (auto& c : srvClients) {
            m_clients.emplace_back(c.id, c.nickname, c.status);
            cItems.push_back(std::format("[{}] {} ({})", c.id, c.nickname, c.status));
        }
        ui.SetListBoxItems(id_srvClientList, std::move(cItems));

        auto srvRooms = m_server.GetRooms();
        m_rooms.clear();
        std::vector<std::string> rItems;
        for (auto& r : srvRooms) {
            m_rooms.emplace_back(r.id, r.name);
            rItems.push_back(std::format("[{}] {} ({} members)",
                r.id, r.name, r.memberIds.size()));
        }
        ui.SetListBoxItems(id_srvRoomList, std::move(rItems));

        auto logs = m_server.DrainLog();
        if (!logs.empty()) {
            std::string current = ui.GetTextAreaContent(id_srvLogArea);
            for (auto& l : logs) { current += l; current += '\n'; }
            ui.SetTextAreaContent(id_srvLogArea, current);
        }

        if (m_debugMode) {
            for (auto& d : m_server.DrainDebug())
                _DebugLog("[srv] " + d);
        }
    }

    void _UpdateClient() {
        for (auto& msg : m_client.DrainInbox())
            _HandleNetMessage(msg);

        {
            std::lock_guard lock(m_client.m_mtx);
            if (m_client.m_dm) _RefreshDmHistory();
        }

        auto inv = m_client.TakePendingInvite();
        if (inv) {
            std::lock_guard lock(m_client.m_mtx);
            m_client.m_pendingInvite = inv;
            ui.SetText(id_cliInviteLabel,
                std::format("DM invite from {} (ID {})", inv->fromNick, inv->fromId));
            ui.SetVisible(id_cliInvitePanel, true);
        }
    }

    void _HandleNetMessage(const std::vector<Uint8>& msg) {
        using T = Protocol::MsgType;
        _DebugLog(std::format("← type=0x{:02X}", static_cast<int>(Protocol::Type(msg))));
        switch (Protocol::Type(msg)) {
        case T::WELCOME: {
            m_client.myId = 0;
            try { m_client.myId = std::stoi(Protocol::Get(msg, "id")); } catch (...) {}
            m_client.myNick = Protocol::Get(msg, "nickname");
            ui.SetText(id_cliStatusLabel,
                std::format("Connected as {} (ID {})", m_client.myNick, m_client.myId));
            _DebugLog(std::format("  WELCOME id={} nick={}", m_client.myId, m_client.myNick));
            break;
        }
        case T::CLIENT_LIST:
            _DebugLog("  CLIENT_LIST received");
            break;
        case T::ROOM_LIST: {
            auto parsed = ParseIdNameList(Protocol::Get(msg, "rooms"));
            m_rooms.clear();
            std::vector<std::string> items;
            for (auto& [id, name] : parsed) {
                m_rooms.emplace_back(id, name);
                items.push_back(name);
            }
            ui.SetListBoxItems(id_cliRoomList, std::move(items));
            _DebugLog(std::format("  ROOM_LIST {} rooms", m_rooms.size()));
            break;
        }
        case T::ROOM_USERS: {
            auto parsed = ParseIdNameList(Protocol::Get(msg, "users"));
            std::vector<std::string> items;
            for (auto& [id, name] : parsed)
                items.push_back(std::format("[{}] {}", id, name));
            ui.SetListBoxItems(id_cliUserList, std::move(items));
            break;
        }
        case T::ROOM_MSG: {
            bool isHist = Protocol::Get(msg, "hist") == "1";
            std::string line = Protocol::Get(msg, "nickname") + ": " + Protocol::Get(msg, "text");
            if (isHist) line = "[hist] " + line;
            _AppendChat(line, isHist);
            break;
        }
        case T::ANNOUNCE:
            _AppendChat("[Announcement] " + Protocol::Get(msg, "text"));
            break;
        case T::KICK:
            _AppendChat("[Server] You have been kicked.");
            m_client.Disconnect();
            _SwitchTo(Mode::Launcher);
            break;
        case T::DM_INVITE: {
            int fromId = 0;
            try { fromId = std::stoi(Protocol::Get(msg, "from")); } catch (...) { break; }
            Uint16 port = 0;
            try { port = static_cast<Uint16>(std::stoi(Protocol::Get(msg, "port"))); } catch (...) { break; }
            std::lock_guard lock(m_client.m_mtx);
            m_client.m_pendingInvite = ChatClient::PendingInvite{
                fromId, Protocol::Get(msg, "nickname"), port};
            _DebugLog(std::format("  DM_INVITE from={}", fromId));
            break;
        }
        case T::DM_ACCEPT:
            _DebugLog("  DM_ACCEPT received");
            break;
        default: break;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            if (ev.key.key == SDL::KEYCODE_ESCAPE) {
                if (m_mode != Mode::Launcher) {
                    m_server.Stop();
                    m_client.Disconnect();
                    _SwitchTo(Mode::Launcher);
                    return SDL::APP_CONTINUE;
                }
                return SDL::APP_SUCCESS;
            }
            if (m_mode == Mode::Client &&
                (ev.key.key == SDL::KEYCODE_RETURN ||
                 ev.key.key == SDL::KEYCODE_KP_ENTER))
                _CliSendMsg();
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
