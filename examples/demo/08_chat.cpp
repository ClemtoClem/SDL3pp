/**
 * @file 08_chat.cpp
 * @brief P2P chat application using SDL3pp_ui and SDL3pp_net.
 *
 * Launch Modes (command-line flag, or selected via launcher screen):
 *   --server   Start as chat server (default port 9900)
 *   --client   Start as chat client
 *   --port N   Override the default port (9900)
 *   --host H   Server host to connect to (client mode, default 127.0.0.1)
 *
 * ## Server features
 *   - Lists all connected clients with their status.
 *   - Create / delete discussion rooms.
 *   - Settings: max clients, max clients per room.
 *   - Kick clients.
 *   - Scheduled announcements: message + hour/minute + repeat period.
 *
 * ## Client features
 *   - Connects to server, sees room list, joins a room.
 *   - Sees users in the current room.
 *   - Sends messages to the current room.
 *   - Send a DM invitation to another client by ID.
 *     If accepted, a direct TCP thread is spawned for the DM.
 *
 * ## Protocol
 *   All messages over TCP are length-prefixed via SDL::MessageFramer.
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
  constexpr SDL::Color TRANSP  = {0,    0,   0,   0};
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol
// ─────────────────────────────────────────────────────────────────────────────
namespace Proto {

enum class MsgType : Uint8 {
    HELLO       = 0x01, ///< Client -> Server: {nick=...}
    WELCOME     = 0x02, ///< Server -> Client: {id=N, nick=...}
    CLIENT_LIST = 0x03, ///< Server -> Client: {clients=id:nick:status,...}
    ROOM_LIST   = 0x04, ///< Server -> Client: {rooms=id:name,...}
    JOIN_ROOM   = 0x05, ///< Client -> Server: {room_id=N}
    LEAVE_ROOM  = 0x06, ///< Client -> Server: {}
    ROOM_MSG    = 0x07, ///< Both directions: {from=id, text=...}
    ROOM_USERS  = 0x08, ///< Server -> Client: {users=id:nick,...}
    DM_INVITE   = 0x09, ///< Client -> Server -> Client: {from=id, port=N}
    DM_ACCEPT   = 0x0A, ///< Client -> Server -> Client: {to=id, port=N}
    DM_MSG      = 0x0B, ///< Direct TCP (separate connection): {from=id, text=...}
    SET_STATUS  = 0x0C, ///< Client -> Server: {status=online|away|busy}
    ANNOUNCE    = 0x0D, ///< Server -> Client: {text=...}
    KICK        = 0x0E, ///< Server -> Client: {}
    PING        = 0x0F, ///< keepalive (no payload)
    CREATE_ROOM = 0x10, ///< Client(server-admin) -> Server: {name=...}
    DELETE_ROOM = 0x11, ///< Client(server-admin) -> Server: {room_id=N}
};

/// Build a framed payload: type byte + '\n' + key=value lines
inline std::vector<Uint8> Make(MsgType t,
                               std::initializer_list<std::pair<const char*,std::string>> kv = {})
{
    std::vector<Uint8> buf;
    buf.push_back(static_cast<Uint8>(t));
    buf.push_back('\n');
    for (auto& [k,v] : kv) {
        for (char c : std::string(k)) buf.push_back(c);
        buf.push_back('=');
        for (char c : v) buf.push_back(c);
        buf.push_back('\n');
    }
    return buf;
}

/// Extract a key from a payload (bytes after the initial "type\n")
inline std::string Get(const std::vector<Uint8>& msg, const std::string& key)
{
    // payload starts at offset 2 (type byte + '\n')
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
    if (msg.empty()) return static_cast<MsgType>(0);
    return static_cast<MsgType>(msg[0]);
}

} // namespace Proto

// ─────────────────────────────────────────────────────────────────────────────
// Shared data types
// ─────────────────────────────────────────────────────────────────────────────
struct ClientInfo {
    int         id     = 0;
    std::string nick;
    std::string status = "online"; // online | away | busy
    int         roomId = -1;       // -1 = no room
};

struct Room {
    int         id = 0;
    std::string name;
    std::vector<int> memberIds;
};

struct Announcement {
    std::string text;
    int         hour   = 0;   // 0-23
    int         minute = 0;   // 0-59
    int         repeatMins = 0; // 0 = no repeat
    std::chrono::system_clock::time_point lastSent{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Server logic  (runs on a background thread)
// ─────────────────────────────────────────────────────────────────────────────
class ChatServer {
public:
    static constexpr Uint16 BASE_PORT = 9900;

    struct Peer {
        SDL::StreamSocket sock;
        SDL::MessageFramer framer;
        ClientInfo info;
    };

    // Settings (written from UI thread, read from net thread under m_mtx)
    std::mutex        m_mtx;
    int               m_maxClients   = 16;
    int               m_maxPerRoom   = 8;
    bool              m_running      = false;
    std::thread       m_thread;

    // State (only modified under m_mtx)
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::vector<Room>                  m_rooms;
    std::vector<Announcement>          m_announcements;
    int                                m_nextClientId = 1;
    int                                m_nextRoomId   = 1;

    // Log lines produced by server (consumed by UI thread)
    std::vector<std::string>           m_log;

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

    // ── UI-thread commands (all lock m_mtx) ──────────────────────────────────

    void AddRoom(const std::string& name)
    {
        std::lock_guard lock(m_mtx);
        Room r;
        r.id   = m_nextRoomId++;
        r.name = name;
        m_rooms.push_back(r);
        _BroadcastRoomList();
    }

    void DeleteRoom(int roomId)
    {
        std::lock_guard lock(m_mtx);
        auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                               [&](const Room& r){ return r.id == roomId; });
        if (it == m_rooms.end()) return;
        // Kick members out
        for (int cid : it->memberIds)
            _SendToPeer(cid, Proto::Make(Proto::MsgType::KICK));
        m_rooms.erase(it);
        _BroadcastRoomList();
    }

    void KickClient(int clientId)
    {
        std::lock_guard lock(m_mtx);
        _SendToPeer(clientId, Proto::Make(Proto::MsgType::KICK));
        _RemovePeer(clientId);
    }

    void SetStatus(int clientId, const std::string& status)
    {
        std::lock_guard lock(m_mtx);
        for (auto& p : m_peers)
            if (p->info.id == clientId) {
                p->info.status = status;
                break;
            }
        _BroadcastClientList();
    }

    void AddAnnouncement(Announcement a)
    {
        std::lock_guard lock(m_mtx);
        m_announcements.push_back(std::move(a));
    }

    void RemoveAnnouncement(size_t idx)
    {
        std::lock_guard lock(m_mtx);
        if (idx < m_announcements.size())
            m_announcements.erase(m_announcements.begin() + idx);
    }

    // Snapshot for UI display
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

private:
    void _Log(const std::string& s)
    {
        // called under lock
        m_log.push_back(s);
        SDL::Log("%s", s.c_str());
    }

    void _Run(Uint16 port)
    {
        SDL::Server server{nullptr, port};
        _Log(std::format("[server] Listening on port {}", port));

        while (m_running) {
            std::lock_guard lock(m_mtx);

            // Accept new clients
            SDL::StreamSocket newSock = server.AcceptClient();
            if (newSock) {
                if (static_cast<int>(m_peers.size()) < m_maxClients) {
                    auto p  = std::make_unique<Peer>();
                    p->sock = std::move(newSock);
                    m_peers.push_back(std::move(p));
                    _Log("[server] New connection");
                }
                // If full: socket destroyed → connection reset
            }

            // Build vsock array for WaitUntilInputAvailable
            std::vector<void*> vsocks;
            vsocks.reserve(m_peers.size());
            for (auto& p : m_peers)
                vsocks.push_back(p->sock.Get());

            if (!vsocks.empty()) {
                SDL::WaitUntilInputAvailable(vsocks.data(),
                                             static_cast<int>(vsocks.size()), 10);
                for (auto& p : m_peers) {
                    p->framer.Fill(p->sock);
                    std::vector<Uint8> msg;
                    while (p->framer.Receive(msg))
                        _HandleMessage(*p, msg);
                }
            } else {
                SDL_Delay(10);
            }

            // Remove dead peers
            m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(),
                [](const std::unique_ptr<Peer>& p){ return !p->sock; }),
                m_peers.end());

            // Scheduled announcements
            _CheckAnnouncements();
        }

        _Log("[server] Stopped.");
    }

    void _HandleMessage(Peer& p, const std::vector<Uint8>& msg)
    {
        using T = Proto::MsgType;
        switch (Proto::Type(msg)) {
        case T::HELLO: {
            p.info.id   = m_nextClientId++;
            p.info.nick = Proto::Get(msg, "nick");
            if (p.info.nick.empty())
                p.info.nick = "user" + std::to_string(p.info.id);
            auto welcome = Proto::Make(T::WELCOME, {
                {"id",   std::to_string(p.info.id)},
                {"nick", p.info.nick}
            });
            SDL::MessageFramer::Send(p.sock, welcome.data(), static_cast<int>(welcome.size()));
            _BroadcastClientList();
            _SendRoomList(p);
            _Log(std::format("[server] Client {} ({}) joined", p.info.id, p.info.nick));
            break;
        }
        case T::JOIN_ROOM: {
            int rid = std::stoi(Proto::Get(msg, "room_id"));
            // Leave current room
            if (p.info.roomId >= 0)
                _LeaveRoom(p);
            auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                                   [&](const Room& r){ return r.id == rid; });
            if (it != m_rooms.end()) {
                int cap = m_maxPerRoom;
                if (static_cast<int>(it->memberIds.size()) < cap) {
                    it->memberIds.push_back(p.info.id);
                    p.info.roomId = rid;
                    _SendRoomUsers(rid);
                }
            }
            break;
        }
        case T::LEAVE_ROOM:
            _LeaveRoom(p);
            break;
        case T::ROOM_MSG: {
            if (p.info.roomId < 0) break;
            std::string text = Proto::Get(msg, "text");
            auto fwd = Proto::Make(T::ROOM_MSG, {
                {"from", std::to_string(p.info.id)},
                {"nick", p.info.nick},
                {"text", text}
            });
            _BroadcastRoom(p.info.roomId, fwd);
            break;
        }
        case T::DM_INVITE: {
            int toId  = std::stoi(Proto::Get(msg, "to"));
            std::string port = Proto::Get(msg, "port");
            // Forward invite
            auto fwd = Proto::Make(T::DM_INVITE, {
                {"from", std::to_string(p.info.id)},
                {"nick", p.info.nick},
                {"port", port}
            });
            _SendToPeer(toId, fwd);
            break;
        }
        case T::DM_ACCEPT: {
            int toId = std::stoi(Proto::Get(msg, "to"));
            std::string port = Proto::Get(msg, "port");
            auto fwd = Proto::Make(T::DM_ACCEPT, {
                {"from", std::to_string(p.info.id)},
                {"nick", p.info.nick},
                {"port", port}
            });
            _SendToPeer(toId, fwd);
            break;
        }
        case T::SET_STATUS:
            p.info.status = Proto::Get(msg, "status");
            _BroadcastClientList();
            break;
        case T::CREATE_ROOM: {
            std::string name = Proto::Get(msg, "name");
            if (!name.empty()) {
                Room r;
                r.id   = m_nextRoomId++;
                r.name = name;
                m_rooms.push_back(r);
                _BroadcastRoomList();
            }
            break;
        }
        case T::DELETE_ROOM: {
            int rid = std::stoi(Proto::Get(msg, "room_id"));
            DeleteRoom(rid);
            break;
        }
        case T::PING: break;
        default: break;
        }
    }

    // Helpers — all called under m_mtx ----------------------------------------

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
        auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                               [&](const Room& r){ return r.id == roomId; });
        if (it == m_rooms.end()) return;
        for (int cid : it->memberIds)
            _SendToPeer(cid, msg);
    }

    void _BroadcastClientList()
    {
        std::string list;
        for (auto& p : m_peers)
            list += std::to_string(p->info.id) + ":" + p->info.nick + ":" + p->info.status + ",";
        if (!list.empty()) list.pop_back();
        _BroadcastAll(Proto::Make(Proto::MsgType::CLIENT_LIST, {{"clients", list}}));
    }

    void _BroadcastRoomList()
    {
        std::string list;
        for (auto& r : m_rooms)
            list += std::to_string(r.id) + ":" + r.name + ",";
        if (!list.empty()) list.pop_back();
        _BroadcastAll(Proto::Make(Proto::MsgType::ROOM_LIST, {{"rooms", list}}));
    }

    void _SendRoomList(Peer& p)
    {
        std::string list;
        for (auto& r : m_rooms)
            list += std::to_string(r.id) + ":" + r.name + ",";
        if (!list.empty()) list.pop_back();
        auto msg = Proto::Make(Proto::MsgType::ROOM_LIST, {{"rooms", list}});
        SDL::MessageFramer::Send(p.sock, msg.data(), static_cast<int>(msg.size()));
    }

    void _SendRoomUsers(int roomId)
    {
        auto it = std::find_if(m_rooms.begin(), m_rooms.end(),
                               [&](const Room& r){ return r.id == roomId; });
        if (it == m_rooms.end()) return;

        std::string list;
        for (int cid : it->memberIds) {
            for (auto& p : m_peers)
                if (p->info.id == cid)
                    list += std::to_string(cid) + ":" + p->info.nick + ",";
        }
        if (!list.empty()) list.pop_back();
        auto msg = Proto::Make(Proto::MsgType::ROOM_USERS, {{"users", list}});
        for (int cid : it->memberIds)
            _SendToPeer(cid, msg);
    }

    void _LeaveRoom(Peer& p)
    {
        if (p.info.roomId < 0) return;
        int rid = p.info.roomId;
        for (auto& r : m_rooms)
            if (r.id == rid)
                r.memberIds.erase(std::remove(r.memberIds.begin(), r.memberIds.end(),
                                              p.info.id), r.memberIds.end());
        p.info.roomId = -1;
        _SendRoomUsers(rid);
    }

    void _RemovePeer(int clientId)
    {
        for (auto& p : m_peers)
            if (p->info.id == clientId) {
                _LeaveRoom(*p);
                p->sock.Reset();
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
            auto elapsed = duration_cast<minutes>(now - ann.lastSent).count();
            if (ann.lastSent.time_since_epoch().count() != 0 &&
                elapsed < (ann.repeatMins > 0 ? ann.repeatMins : 1440))
                continue;
            ann.lastSent = now;
            auto msg = Proto::Make(Proto::MsgType::ANNOUNCE, {{"text", ann.text}});
            _BroadcastAll(msg);
            _Log(std::format("[announce] {}", ann.text));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Client network layer  (background thread)
// ─────────────────────────────────────────────────────────────────────────────
struct ChatClient {
    int         myId   = 0;
    std::string myNick;
    int         roomId = -1;

    std::atomic<bool>  m_running{false};
    std::thread        m_thread;

    // Protected by m_mtx
    std::mutex               m_mtx;
    SDL::StreamSocket        m_sock;
    SDL::MessageFramer       m_framer;

    // Incoming message queue
    std::vector<std::vector<Uint8>> m_inbox;

    // DM peer: we maintain one DM connection at a time for simplicity
    struct DmConn {
        SDL::StreamSocket sock;
        SDL::MessageFramer framer;
        int peerId = 0;
        std::string peerNick;
        std::vector<std::string> history;
    };
    std::unique_ptr<DmConn> m_dm;

    // Pending DM invite received
    struct PendingInvite {
        int fromId = 0;
        std::string fromNick;
        Uint16 port = 0;
    };
    std::optional<PendingInvite> m_pendingInvite;
    std::string m_serverHost;

    bool Connect(const std::string& host, Uint16 port, const std::string& nick)
    {
        SDL::Address addr{host.c_str()};
        if (addr.WaitUntilResolved(5000) != SDL::NET_SUCCESS_STATUS) return false;
        m_sock = SDL::StreamSocket{addr, port};
        if (m_sock.WaitUntilConnected(5000) != SDL::NET_SUCCESS_STATUS) return false;
        myNick = nick;
        m_serverHost = host;
        // Send HELLO
        auto hello = Proto::Make(Proto::MsgType::HELLO, {{"nick", nick}});
        SDL::MessageFramer::Send(m_sock, hello.data(), static_cast<int>(hello.size()));
        m_running = true;
        m_thread  = std::thread([this]{ _Run(); });
        return true;
    }

    void Disconnect()
    {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
    }

    void Send(const std::vector<Uint8>& msg)
    {
        std::lock_guard lock(m_mtx);
        SDL::MessageFramer::Send(m_sock, msg.data(), static_cast<int>(msg.size()));
    }

    // Returns and clears inbox (call from UI thread)
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

    // DM: invite another client
    void InviteDm(int targetId)
    {
        // Listen on a random port
        SDL::DatagramSocket tmp{nullptr, 0}; // just to probe — use StreamSocket port trick
        // We'll use port myId + 10000 as a deterministic DM port
        Uint16 dmPort = static_cast<Uint16>(10000 + myId);
        auto invite = Proto::Make(Proto::MsgType::DM_INVITE, {
            {"to",   std::to_string(targetId)},
            {"port", std::to_string(dmPort)}
        });
        Send(invite);
    }

    // DM: accept invite
    void AcceptDm(int fromId, const std::string& fromNick, Uint16 port)
    {
        // Connect directly to the inviter's DM port
        {
            std::lock_guard lock(m_mtx);
            m_dm = std::make_unique<DmConn>();
            m_dm->peerId   = fromId;
            m_dm->peerNick = fromNick;
        }
        std::thread([this, fromId, fromNick, port]() {
            SDL::Address addr{m_serverHost.c_str()};
            addr.WaitUntilResolved(5000);
            SDL::StreamSocket sock{addr, port};
            if (sock.WaitUntilConnected(5000) != SDL::NET_SUCCESS_STATUS) return;
            std::lock_guard lock(m_mtx);
            if (m_dm) m_dm->sock = std::move(sock);
        }).detach();

        // Tell server
        Uint16 myDmPort = static_cast<Uint16>(10000 + myId);
        auto accept = Proto::Make(Proto::MsgType::DM_ACCEPT, {
            {"to",   std::to_string(fromId)},
            {"port", std::to_string(myDmPort)}
        });
        Send(accept);
    }

    // DM: listen (inviter side) — blocks until accept or timeout
    void ListenForDm(int targetId)
    {
        Uint16 dmPort = static_cast<Uint16>(10000 + myId);
        std::thread([this, targetId, dmPort]() {
            SDL::Server srv{nullptr, dmPort};
            SDL::StreamSocket peer = srv.AcceptClient(10000); // 10 s timeout
            if (!peer) return;
            std::lock_guard lock(m_mtx);
            m_dm = std::make_unique<DmConn>();
            m_dm->peerId   = targetId;
            m_dm->peerNick = "peer" + std::to_string(targetId);
            m_dm->sock     = std::move(peer);
        }).detach();
    }

    void SendDm(const std::string& text)
    {
        std::lock_guard lock(m_mtx);
        if (!m_dm || !m_dm->sock) return;
        auto msg = Proto::Make(Proto::MsgType::DM_MSG, {
            {"from", std::to_string(myId)},
            {"text", text}
        });
        SDL::MessageFramer::Send(m_dm->sock, msg.data(), static_cast<int>(msg.size()));
        m_dm->history.push_back("Me: " + text);
    }

    std::vector<std::string> GetDmHistory()
    {
        std::lock_guard lock(m_mtx);
        return m_dm ? m_dm->history : std::vector<std::string>{};
    }

private:
    void _Run()
    {
        while (m_running) {
            std::lock_guard lock(m_mtx);
            if (!m_sock) break;

            void* vsock = m_sock.Get();
            SDL::WaitUntilInputAvailable(&vsock, 1, 50);
            m_framer.Fill(m_sock);

            std::vector<Uint8> msg;
            while (m_framer.Receive(msg))
                m_inbox.push_back(msg);

            // DM receive
            if (m_dm && m_dm->sock) {
                m_dm->framer.Fill(m_dm->sock);
                std::vector<Uint8> dm;
                while (m_dm->framer.Receive(dm)) {
                    std::string text = Proto::Get(dm, "text");
                    m_dm->history.push_back(m_dm->peerNick + ": " + text);
                }
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Parse a "id:name,id:name,..." list
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
            try {
                int id = std::stoi(tok.substr(0, colon));
                out.push_back({id, tok.substr(colon + 1)});
            } catch (...) {}
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// Parse "id:nick:status,..." (3 fields)
static std::vector<std::tuple<int,std::string,std::string>> ParseClientList(const std::string& s)
{
    std::vector<std::tuple<int,std::string,std::string>> out;
    size_t pos = 0;
    while (pos < s.size()) {
        auto comma = s.find(',', pos);
        std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        auto c1 = tok.find(':');
        auto c2 = tok.find(':', c1 + 1);
        if (c1 != std::string::npos && c2 != std::string::npos) {
            try {
                int id = std::stoi(tok.substr(0, c1));
                out.emplace_back(id,
                    tok.substr(c1 + 1, c2 - c1 - 1),
                    tok.substr(c2 + 1));
            } catch (...) {}
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

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
            "SDL3pp — Chat", kWinSz, SDL_WINDOW_RESIZABLE, nullptr);
    }

    // ── Core objects ──────────────────────────────────────────────────────────

    SDL::Window      window  { MakeWindow()         };
    SDL::RendererRef renderer{ window.GetRenderer() };

    SDL::ResourceManager rm;
    SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };

    SDL::ECS::Context ecs_ctx;
    SDL::UI::System   ui{ ecs_ctx, renderer, {}, pool };

    SDL::FrameTimer   m_frameTimer{ 60.f };

    // ── Application logic ─────────────────────────────────────────────────────

    ChatServer m_server;
    ChatClient m_client;

    Uint16      m_port = ChatServer::BASE_PORT;
    std::string m_host = "127.0.0.1";

    // Cached data for UI
    std::vector<std::tuple<int,std::string,std::string>> m_clients; // id,nick,status
    std::vector<std::pair<int,std::string>>              m_rooms;   // id,name
    std::vector<std::pair<int,std::string>>              m_roomUsers;
    std::vector<std::string>                             m_chatHistory;

    // ── UI IDs — Launcher ─────────────────────────────────────────────────────
    SDL::ECS::EntityId id_launcherPanel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_nickInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_hostInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_portInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_serverBtn     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_clientBtn     = SDL::ECS::NullEntity;

    // ── UI IDs — Server ───────────────────────────────────────────────────────
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

    // ── UI IDs — Client ───────────────────────────────────────────────────────
    SDL::ECS::EntityId id_clientPanel    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliRoomList    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliUserList    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliChatArea    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliMsgInput    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliStatusLabel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmPanel     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmHistory   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliDmTarget    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliInvitePanel = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cliInviteLabel = SDL::ECS::NullEntity;

    // Announcement display
    std::vector<Announcement> m_annCache;

    // ── Constructor ───────────────────────────────────────────────────────────

    explicit Main(SDL::AppArgs args) {
        // Parse args
        Mode forcedMode = Mode::Launcher;
        for (auto arg : args) {
            if (arg == "--server") forcedMode = Mode::Server;
            else if (arg == "--client") forcedMode = Mode::Client;
            else if (std::string(arg).substr(0, 7) == "--port=")
                m_port = static_cast<Uint16>(std::stoi(std::string(arg).substr(7)));
            else if (std::string(arg).substr(0, 7) == "--host=")
                m_host = std::string(arg).substr(7);
        }
        window.StartTextInput();
        _LoadResources();
        _BuildUI();
        m_frameTimer.Begin();
        if (forcedMode == Mode::Server) _StartServer();
        else if (forcedMode == Mode::Client) _SwitchTo(Mode::Client);
    }

    ~Main() {
        m_server.Stop();
        m_client.Disconnect();
        pool.Release();
    }

    // ── Resources ─────────────────────────────────────────────────────────────

    void _LoadResources() {
        const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("font", base + "fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 13.f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI construction
    // ─────────────────────────────────────────────────────────────────────────

    void _BuildUI() {
        auto root = ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f)).Radius(SDL::FCorners(0.f));

        root.Children(_BuildHeader(),
                      _BuildLauncher(),
                      _BuildServerView(),
                      _BuildClientView());
        root.AsRoot();

        _SwitchTo(Mode::Launcher);
    }

    SDL::ECS::EntityId _BuildHeader() {
        return ui.Row("header", 8.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(48.f)
            .PaddingH(16.f).PaddingV(0.f)
            .BgColor(pal::HEADER)
            .Borders(SDL::FBox(0.f,0.f,0.f,1.f)).BorderColor(pal::BORDER)
            .Children(
                ui.Label("app_title", "SDL3pp Chat")
                    .TextColor(pal::ACCENT).FontKey("font", 16.f).Grow(1),
                ui.Label("app_sub", "P2P chat demo")
                    .TextColor(pal::GREY)
            );
    }

    SDL::ECS::EntityId _BuildLauncher() {
        auto panel = ui.Column("launcher_panel", 16.f, 0.f)
            .Grow(1)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f))
            .AlignChildrenH(SDL::UI::Align::Center)
            .PaddingH(0.f).PaddingV(60.f);

        auto box = ui.Column("launcher_box", 12.f, 0.f)
            .W(380.f).BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(10.f))
            .PaddingH(24.f).PaddingV(20.f);

        box.Child(ui.Label("lbl_title", "Welcome to SDL3pp Chat")
            .TextColor(pal::WHITE).FontKey("font", 18.f)
            .AlignH(SDL::UI::Align::Center));

        box.Child(ui.Label("lbl_nick", "Nickname")
            .TextColor(pal::GREY));
        id_nickInput = ui.Input("nick_input", "Enter your nickname…")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_nickInput);

        box.Child(ui.Label("lbl_host", "Server host (client mode)")
            .TextColor(pal::GREY));
        id_hostInput = ui.Input("host_input", "127.0.0.1")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_hostInput);

        box.Child(ui.Label("lbl_port", "Port")
            .TextColor(pal::GREY));
        id_portInput = ui.Input("port_input", "9900")
            .W(SDL::UI::Value::Pw(100.f)).H(34.f);
        box.Child(id_portInput);

        auto btnRow = ui.Row("btn_row", 10.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        id_serverBtn = ui.Button("start_server_btn", "Start Server")
            .Grow(1).H(38.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
            .OnClick([this]{ _StartServer(); });
        btnRow.Child(id_serverBtn);

        id_clientBtn = ui.Button("start_client_btn", "Connect as Client")
            .Grow(1).H(38.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
            .OnClick([this]{ _ConnectClient(); });
        btnRow.Child(id_clientBtn);

        box.Child(btnRow);
        panel.Child(box);
        id_launcherPanel = panel;
        return panel;
    }

    SDL::ECS::EntityId _BuildServerView() {
        auto panel = ui.Row("server_panel", 12.f, 0.f)
            .Grow(1)
            .PaddingH(12.f).PaddingV(12.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f));

        // ── Left: clients + rooms ─────────────────────────────────────────────
        auto left = ui.Column("srv_left", 8.f, 0.f)
            .W(220.f)
            .BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        left.Child(ui.Label("srv_lbl_clients", "Connected Clients")
            .TextColor(pal::GREY).FontKey("font", 11.f));
        id_srvClientList = ui.ListBoxWidget("srv_client_list")
            .W(SDL::UI::Value::Pw(100.f)).H(200.f)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f));
        left.Child(id_srvClientList);

        auto kickRow = ui.Row("kick_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        kickRow.Child(ui.Button("kick_btn", "Kick")
            .Grow(1).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::RED))
            .OnClick([this]{ _SrvKick(); }));
        left.Child(kickRow);

        left.Child(ui.Label("srv_lbl_rooms", "Rooms")
            .TextColor(pal::GREY).FontKey("font", 11.f));
        id_srvRoomList = ui.ListBoxWidget("srv_room_list")
            .W(SDL::UI::Value::Pw(100.f)).H(150.f)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f));
        left.Child(id_srvRoomList);

        auto roomCtrl = ui.Row("room_ctrl", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        id_srvRoomName = ui.Input("srv_room_name", "Room name…")
            .Grow(1).H(30.f);
        roomCtrl.Child(id_srvRoomName);
        roomCtrl.Child(ui.Button("srv_add_room", "+")
            .W(30.f).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
            .OnClick([this]{ _SrvAddRoom(); }));
        roomCtrl.Child(ui.Button("srv_del_room", "−")
            .W(30.f).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::RED))
            .OnClick([this]{ _SrvDelRoom(); }));
        left.Child(roomCtrl);

        panel.Child(left);

        // ── Centre: log ───────────────────────────────────────────────────────
        auto centre = ui.Column("srv_centre", 8.f, 0.f)
            .Grow(1).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        centre.Child(ui.Label("srv_lbl_log", "Server Log")
            .TextColor(pal::GREY).FontKey("font", 11.f));
        id_srvLogArea = ui.TextArea("srv_log_area", "")
            .Grow(1).W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .AutoScrollableY(true);
        centre.Child(id_srvLogArea);
        panel.Child(centre);

        // ── Right: settings + announcements ──────────────────────────────────
        auto right = ui.Column("srv_right", 10.f, 0.f)
            .W(240.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        right.Child(ui.Label("srv_lbl_settings", "Settings")
            .TextColor(pal::ACCENT).FontKey("font", 13.f));

        id_srvMaxCliLabel = ui.Label("srv_max_cli_lbl",
            std::format("Max clients: {}", m_server.m_maxClients))
            .TextColor(pal::GREY).FontKey("font", 11.f);
        right.Child(id_srvMaxCliLabel);
        id_srvMaxClients = ui.Slider("srv_max_clients", 1.f, 64.f, m_server.m_maxClients)
            .W(SDL::UI::Value::Pw(100.f)).H(24.f)
            .OnChange([this](float v){
                m_server.m_maxClients = static_cast<int>(v);
                ui.SetText(id_srvMaxCliLabel,
                    std::format("Max clients: {}", m_server.m_maxClients));
            });
        right.Child(id_srvMaxClients);

        id_srvMaxRoomLabel = ui.Label("srv_max_room_lbl",
            std::format("Max per room: {}", m_server.m_maxPerRoom))
            .TextColor(pal::GREY).FontKey("font", 11.f);
        right.Child(id_srvMaxRoomLabel);
        id_srvMaxPerRoom = ui.Slider("srv_max_per_room", 1.f, 32.f, m_server.m_maxPerRoom)
            .W(SDL::UI::Value::Pw(100.f)).H(24.f)
            .OnChange([this](float v){
                m_server.m_maxPerRoom = static_cast<int>(v);
                ui.SetText(id_srvMaxRoomLabel,
                    std::format("Max per room: {}", m_server.m_maxPerRoom));
            });
        right.Child(id_srvMaxPerRoom);

        right.Child(ui.Separator("sep1").H(1.f).BgColor(pal::BORDER));

        right.Child(ui.Label("ann_title", "Announcements")
            .TextColor(pal::ACCENT).FontKey("font", 13.f));

        id_srvAnnText = ui.Input("ann_text", "Announcement text…")
            .W(SDL::UI::Value::Pw(100.f)).H(30.f);
        right.Child(id_srvAnnText);

        auto annTime = ui.Row("ann_time", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        id_srvAnnHour = ui.Input("ann_hour", "HH").W(48.f).H(30.f);
        annTime.Child(id_srvAnnHour);
        annTime.Child(ui.Label("ann_colon", ":").TextColor(pal::GREY));
        id_srvAnnMin = ui.Input("ann_min", "MM").W(48.f).H(30.f);
        annTime.Child(id_srvAnnMin);
        annTime.Child(ui.Label("ann_rep_lbl", "Rep.min:").TextColor(pal::GREY));
        id_srvAnnRepeat = ui.Input("ann_repeat", "0").W(48.f).H(30.f);
        annTime.Child(id_srvAnnRepeat);
        right.Child(annTime);

        right.Child(ui.Button("ann_add_btn", "Add Announcement")
            .W(SDL::UI::Value::Pw(100.f)).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
            .OnClick([this]{ _SrvAddAnnouncement(); }));

        id_srvAnnList = ui.ListBoxWidget("ann_list")
            .W(SDL::UI::Value::Pw(100.f)).H(100.f)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f));
        right.Child(id_srvAnnList);

        right.Child(ui.Button("ann_del_btn", "Remove Selected")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::RED))
            .OnClick([this]{ _SrvRemoveAnnouncement(); }));

        panel.Child(right);
        id_serverPanel = panel;
        return panel;
    }

    SDL::ECS::EntityId _BuildClientView() {
        auto panel = ui.Row("client_panel", 10.f, 0.f)
            .Grow(1).PaddingH(12.f).PaddingV(12.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f));

        // ── Left: rooms + users ───────────────────────────────────────────────
        auto left = ui.Column("cli_left", 8.f, 0.f)
            .W(180.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        left.Child(ui.Label("cli_lbl_rooms", "Rooms")
            .TextColor(pal::GREY).FontKey("font", 11.f));
        id_cliRoomList = ui.ListBoxWidget("cli_room_list")
            .W(SDL::UI::Value::Pw(100.f)).H(160.f)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .OnClick([this]{ _CliJoinRoom(); });
        left.Child(id_cliRoomList);

        left.Child(ui.Label("cli_lbl_users", "In Room")
            .TextColor(pal::GREY).FontKey("font", 11.f));
        id_cliUserList = ui.ListBoxWidget("cli_user_list")
            .W(SDL::UI::Value::Pw(100.f)).Grow(1)
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f));
        left.Child(id_cliUserList);

        // DM invite
        auto dmRow = ui.Row("dm_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        id_cliDmTarget = ui.Input("cli_dm_target", "Client ID…").Grow(1).H(30.f);
        dmRow.Child(id_cliDmTarget);
        dmRow.Child(ui.Button("cli_invite_btn", "DM")
            .W(38.f).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
            .OnClick([this]{ _CliInviteDm(); }));
        left.Child(dmRow);

        id_cliStatusLabel = ui.Label("cli_status", "Not connected")
            .TextColor(pal::GREY).FontKey("font", 11.f);
        left.Child(id_cliStatusLabel);

        panel.Child(left);

        // ── Centre: chat ──────────────────────────────────────────────────────
        auto centre = ui.Column("cli_centre", 8.f, 0.f)
            .Grow(1).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        centre.Child(ui.Label("cli_lbl_chat", "Room Chat")
            .TextColor(pal::GREY).FontKey("font", 11.f));

        id_cliChatArea = ui.TextArea("cli_chat_area", "")
            .Grow(1).W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .AutoScrollableY(true);
        centre.Child(id_cliChatArea);

        auto msgRow = ui.Row("msg_row", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        id_cliMsgInput = ui.Input("cli_msg_input", "Type message…").Grow(1).H(34.f);
        msgRow.Child(id_cliMsgInput);
        msgRow.Child(ui.Button("cli_send_btn", "Send")
            .W(80.f).H(34.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
            .OnClick([this]{ _CliSendMsg(); }));
        centre.Child(msgRow);
        panel.Child(centre);

        // ── Right: DM panel ───────────────────────────────────────────────────
        id_cliDmPanel = ui.Column("cli_dm_panel", 8.f, 0.f)
            .W(230.f).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));

        auto dmP = id_cliDmPanel;
        auto dmPBuilder = ui.Get(dmP);

        ui.Get(dmP).Child(ui.Label("dm_title", "Direct Messages")
            .TextColor(pal::GREY).FontKey("font", 11.f));

        id_cliDmHistory = ui.TextArea("cli_dm_history", "")
            .Grow(1).W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::CARD).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .AutoScrollableY(true);
        ui.Get(dmP).Child(id_cliDmHistory);

        auto dmInputRow = ui.Row("dm_input_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).BgColor(pal::TRANSP).Borders(SDL::FBox(0.f));
        id_cliDmInput = ui.Input("cli_dm_input", "DM…").Grow(1).H(30.f);
        dmInputRow.Child(id_cliDmInput);
        dmInputRow.Child(ui.Button("cli_dm_send", "»")
            .W(30.f).H(30.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
            .OnClick([this]{ _CliSendDm(); }));
        ui.Get(dmP).Child(dmInputRow);

        // Invite notification panel (initially hidden)
        id_cliInvitePanel = ui.Column("invite_panel", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::ORANGE).Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f)).PaddingH(8.f).PaddingV(6.f)
            .Visible(false);
        id_cliInviteLabel = ui.Label("invite_label", "")
            .TextColor({10,10,10,255}).FontKey("font", 12.f);
        auto inviteAccept = ui.Button("invite_accept", "Accept")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
            .OnClick([this]{ _CliAcceptDm(); });
        auto inviteDecline = ui.Button("invite_decline", "Decline")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::RED))
            .OnClick([this]{ _CliDeclineDm(); });
        ui.Get(id_cliInvitePanel)
            .Children(id_cliInviteLabel, inviteAccept, inviteDecline);

        ui.Get(dmP).Child(id_cliInvitePanel);
        panel.Child(id_cliDmPanel);

        id_clientPanel = panel;
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
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Launcher actions
    // ─────────────────────────────────────────────────────────────────────────

    void _ReadLauncherInputs() {
        std::string portStr = ui.GetText(id_portInput);
        if (!portStr.empty()) {
            try { m_port = static_cast<Uint16>(std::stoi(portStr)); } catch (...) {}
        }
        std::string hostStr = ui.GetText(id_hostInput);
        if (!hostStr.empty()) m_host = hostStr;
    }

    std::string _GetNick() {
        std::string nick = ui.GetText(id_nickInput);
        if (nick.empty()) nick = "user";
        return nick;
    }

    void _StartServer() {
        _ReadLauncherInputs();
        m_server.Start(m_port);
        _SwitchTo(Mode::Server);
        window.SetTitle("SDL3pp Chat — Server");
    }

    void _ConnectClient() {
        _ReadLauncherInputs();
        std::string nick = _GetNick();
        if (m_client.Connect(m_host, m_port, nick)) {
            _SwitchTo(Mode::Client);
            ui.SetText(id_cliStatusLabel,
                std::format("Connected as {} (ID pending…)", nick));
            window.SetTitle("SDL3pp Chat — Client: " + nick);
        } else {
            // Show error (could use a modal but keep it simple)
            SDL::Log("[client] Failed to connect to %s:%d", m_host.c_str(), m_port);
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
        std::string text   = ui.GetText(id_srvAnnText);
        std::string hourS  = ui.GetText(id_srvAnnHour);
        std::string minS   = ui.GetText(id_srvAnnMin);
        std::string repS   = ui.GetText(id_srvAnnRepeat);
        if (text.empty()) return;
        Announcement ann;
        ann.text       = text;
        try { ann.hour      = std::stoi(hourS); } catch (...) { ann.hour = 0; }
        try { ann.minute    = std::stoi(minS);  } catch (...) { ann.minute = 0; }
        try { ann.repeatMins = std::stoi(repS); } catch (...) { ann.repeatMins = 0; }
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
            items.push_back(std::format("{:02d}:{:02d} rep={} — {}",
                a.hour, a.minute, a.repeatMins, a.text));
        ui.SetListBoxItems(id_srvAnnList, std::move(items));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Client-view actions
    // ─────────────────────────────────────────────────────────────────────────

    void _CliJoinRoom() {
        int sel = ui.GetListBoxSelection(id_cliRoomList);
        if (sel < 0 || sel >= static_cast<int>(m_rooms.size())) return;
        int rid = m_rooms[sel].first;
        m_client.roomId = rid;
        auto msg = Proto::Make(Proto::MsgType::JOIN_ROOM,
                               {{"room_id", std::to_string(rid)}});
        m_client.Send(msg);
    }

    void _CliSendMsg() {
        std::string text = ui.GetText(id_cliMsgInput);
        if (text.empty() || m_client.roomId < 0) return;
        auto msg = Proto::Make(Proto::MsgType::ROOM_MSG, {{"text", text}});
        m_client.Send(msg);
        _AppendChat("Me: " + text);
        ui.SetText(id_cliMsgInput, "");
    }

    void _CliInviteDm() {
        std::string idStr = ui.GetText(id_cliDmTarget);
        if (idStr.empty()) return;
        int targetId = 0;
        try { targetId = std::stoi(idStr); } catch (...) { return; }
        m_client.ListenForDm(targetId);
        m_client.InviteDm(targetId);
    }

    void _CliAcceptDm() {
        auto inv = m_client.TakePendingInvite();
        if (!inv) return;
        m_client.AcceptDm(inv->fromId, inv->fromNick, inv->port);
        ui.SetVisible(id_cliInvitePanel, false);
    }

    void _CliDeclineDm() {
        m_client.TakePendingInvite(); // discard
        ui.SetVisible(id_cliInvitePanel, false);
    }

    void _CliSendDm() {
        std::string text = ui.GetText(id_cliDmInput);
        if (text.empty()) return;
        m_client.SendDm(text);
        ui.SetText(id_cliDmInput, "");
        _RefreshDmHistory();
    }

    void _AppendChat(const std::string& line) {
        m_chatHistory.push_back(line);
        std::string full;
        for (auto& l : m_chatHistory) full += l + "\n";
        ui.SetTextAreaContent(id_cliChatArea, full);
    }

    void _RefreshDmHistory() {
        auto hist = m_client.GetDmHistory();
        std::string s;
        for (auto& l : hist) s += l + "\n";
        ui.SetTextAreaContent(id_cliDmHistory, s);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Frame loop
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        m_frameTimer.Begin();
        const float dt = m_frameTimer.GetDelta();

        if (m_mode == Mode::Server)   _UpdateServer();
        if (m_mode == Mode::Client)   _UpdateClient();

        pool.Update();
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        m_frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    void _UpdateServer() {
        // Refresh client list
        auto srvClients = m_server.GetClients();
        m_clients.clear();
        std::vector<std::string> cItems;
        for (auto& c : srvClients) {
            m_clients.emplace_back(c.id, c.nick, c.status);
            cItems.push_back(std::format("[{}] {} ({})", c.id, c.nick, c.status));
        }
        ui.SetListBoxItems(id_srvClientList, std::move(cItems));

        // Refresh room list
        auto srvRooms = m_server.GetRooms();
        m_rooms.clear();
        std::vector<std::string> rItems;
        for (auto& r : srvRooms) {
            m_rooms.emplace_back(r.id, r.name);
            rItems.push_back(std::format("[{}] {} ({} members)",
                r.id, r.name, r.memberIds.size()));
        }
        ui.SetListBoxItems(id_srvRoomList, std::move(rItems));

        // Append log
        auto logs = m_server.DrainLog();
        if (!logs.empty()) {
            const std::string& current = ui.GetTextAreaContent(id_srvLogArea);
            std::string updated = current;
            for (auto& l : logs) updated += l + "\n";
            ui.SetTextAreaContent(id_srvLogArea, updated);
        }
    }

    void _UpdateClient() {
        auto msgs = m_client.DrainInbox();
        for (auto& msg : msgs)
            _HandleClientMessage(msg);

        // Refresh DM history if active
        if (m_client.m_dm)
            _RefreshDmHistory();

        // Check pending invite
        auto inv = m_client.TakePendingInvite();
        if (inv) {
            // Put back so Accept/Decline can use it
            std::lock_guard lock(m_client.m_mtx);
            m_client.m_pendingInvite = inv;
            ui.SetText(id_cliInviteLabel,
                std::format("DM invite from {} (ID {})", inv->fromNick, inv->fromId));
            ui.SetVisible(id_cliInvitePanel, true);
        }
    }

    void _HandleClientMessage(const std::vector<Uint8>& msg) {
        using T = Proto::MsgType;
        switch (Proto::Type(msg)) {
        case T::WELCOME: {
            m_client.myId   = std::stoi(Proto::Get(msg, "id"));
            m_client.myNick = Proto::Get(msg, "nick");
            ui.SetText(id_cliStatusLabel,
                std::format("Connected as {} (ID: {})", m_client.myNick, m_client.myId));
            break;
        }
        case T::CLIENT_LIST: {
            std::string list = Proto::Get(msg, "clients");
            auto parsed = ParseClientList(list);
            // (client doesn't display full client list in this layout,
            //  but we could populate a panel if desired)
            break;
        }
        case T::ROOM_LIST: {
            std::string list = Proto::Get(msg, "rooms");
            auto parsed = ParseIdNameList(list);
            m_rooms.clear();
            std::vector<std::string> items;
            for (auto& [id, name] : parsed) {
                m_rooms.emplace_back(id, name);
                items.push_back(name);
            }
            ui.SetListBoxItems(id_cliRoomList, std::move(items));
            break;
        }
        case T::ROOM_USERS: {
            std::string list = Proto::Get(msg, "users");
            auto parsed = ParseIdNameList(list);
            m_roomUsers.clear();
            std::vector<std::string> items;
            for (auto& [id, name] : parsed) {
                m_roomUsers.emplace_back(id, name);
                items.push_back(std::format("[{}] {}", id, name));
            }
            ui.SetListBoxItems(id_cliUserList, std::move(items));
            break;
        }
        case T::ROOM_MSG: {
            std::string nick = Proto::Get(msg, "nick");
            std::string text = Proto::Get(msg, "text");
            _AppendChat(nick + ": " + text);
            break;
        }
        case T::ANNOUNCE: {
            std::string text = Proto::Get(msg, "text");
            _AppendChat("[Announcement] " + text);
            break;
        }
        case T::KICK:
            _AppendChat("[Server] You were kicked.");
            m_client.Disconnect();
            _SwitchTo(Mode::Launcher);
            break;
        case T::DM_INVITE: {
            std::string fromNick = Proto::Get(msg, "nick");
            int fromId = std::stoi(Proto::Get(msg, "from"));
            Uint16 port = static_cast<Uint16>(std::stoi(Proto::Get(msg, "port")));
            std::lock_guard lock(m_client.m_mtx);
            m_client.m_pendingInvite = ChatClient::PendingInvite{fromId, fromNick, port};
            break;
        }
        case T::DM_ACCEPT: {
            // The other side accepted our invite: we should already be listening
            // The DmConn was set up by ListenForDm
            break;
        }
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
            if (ev.key.key == SDL::KEYCODE_RETURN ||
                ev.key.key == SDL::KEYCODE_KP_ENTER) {
                if (m_mode == Mode::Client) _CliSendMsg();
            }
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
