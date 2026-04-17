#ifndef SDL3PP_NET_H_
#define SDL3PP_NET_H_

#include "SDL3pp_error.h"
#include "SDL3pp_stdinc.h"
#include "SDL3pp_version.h"

#if defined(SDL3PP_ENABLE_NET) || defined(SDL3PP_DOC)

#include <SDL3_net/SDL_net.h>

namespace SDL {

/**
 * @defgroup CategorySDLNet Networking support
 *
 * Header file for SDL_net library
 *
 * SDL_net is a simple library to help with networking.
 *
 * In current times, it's a relatively thin layer over system-level APIs like
 * BSD Sockets or WinSock. Its primary strength is in making those interfaces
 * less complicated to use, and handling several unexpected corner cases, so
 * the app doesn't have to.
 *
 * @{
 */

// Forward decl
struct Address;

/// Alias to raw representation for Address.
using AddressRaw = NET_Address*;

// Forward decl
struct AddressRef;

// Forward decl
struct StreamSocket;

/// Alias to raw representation for StreamSocket.
using StreamSocketRaw = NET_StreamSocket*;

// Forward decl
struct StreamSocketRef;

// Forward decl
struct Server;

/// Alias to raw representation for Server.
using ServerRaw = NET_Server*;

// Forward decl
struct ServerRef;

// Forward decl
struct DatagramSocket;

/// Alias to raw representation for DatagramSocket.
using DatagramSocketRaw = NET_DatagramSocket*;

// Forward decl
struct DatagramSocketRef;

// Forward decl
struct Datagram;

/// Alias to raw representation for Datagram.
using DatagramRaw = NET_Datagram*;

// Forward decl
struct DatagramRef;

#ifdef SDL3PP_DOC

/**
 * @name SDL_net version
 * @{
 * Printable format: "%d.%d.%d", MAJOR, MINOR, MICRO
 */
#define SDL_NET_MAJOR_VERSION

#define SDL_NET_MINOR_VERSION

#define SDL_NET_MICRO_VERSION

/// @}

/**
 * This is the version number macro for the current SDL_net version.
 */
#define SDL_NET_VERSION \
	SDL_VERSIONNUM(SDL_NET_MAJOR_VERSION, SDL_NET_MINOR_VERSION, SDL_NET_MICRO_VERSION)

/// This macro will evaluate to true if compiled with SDL_net at least X.Y.Z.
#define SDL_NET_VERSION_ATLEAST(X, Y, Z)                        \
	((SDL_NET_MAJOR_VERSION >= X) &&                              \
	 (SDL_NET_MAJOR_VERSION > X || SDL_NET_MINOR_VERSION >= Y) && \
	 (SDL_NET_MAJOR_VERSION > X || SDL_NET_MINOR_VERSION > Y ||   \
		SDL_NET_MICRO_VERSION >= Z))

#endif // SDL3PP_DOC

namespace NET {

/**
 * This function gets the version of the dynamically linked SDL_net library.
 *
 * @returns SDL_net version.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline int Version() { return NET_Version(); }

/**
 * Initialize the SDL_net library.
 *
 * This must be successfully called once before (almost) any other SDL_net
 * function can be used.
 *
 * It is safe to call this multiple times; the library will only initialize
 * once, and won't deinitialize until NET.Quit() has been called a matching
 * number of times.
 *
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 *
 * @sa NET.Quit
 */
inline void Init() { CheckError(NET_Init()); }

/**
 * Deinitialize the SDL_net library.
 *
 * This must be called when done with the library, probably at the end of your
 * program.
 *
 * It is safe to call this multiple times; the library will only deinitialize
 * once, when this function is called the same number of times as NET.Init was
 * successfully called.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 *
 * @sa NET.Init
 */
inline void Quit() { NET_Quit(); }

} // namespace NET

/**
 * A tri-state for asynchronous operations.
 *
 * Lots of tasks in SDL_net are asynchronous, as they can't complete until
 * data passes over a network at some murky future point in time.
 *
 * @since This enum is available since SDL_net 3.0.0.
 */
using Status = NET_Status;

constexpr Status NET_FAILURE_STATUS = NET_FAILURE; ///< Async operation failed.
constexpr Status NET_WAITING_STATUS = NET_WAITING; ///< Async operation in progress.
constexpr Status NET_SUCCESS_STATUS = NET_SUCCESS; ///< Async operation succeeded.

// =============================================================================
// Address
// =============================================================================

/**
 * Opaque representation of a computer-readable network address.
 *
 * SDL_net uses these to identify other servers; you use them to connect to a
 * remote machine, and you use them to find out who connected to you.
 *
 * These are intended to be protocol-independent; a given address might be for
 * IPv4, IPv6, or something more esoteric.
 *
 * @since This datatype is available since SDL_net 3.0.0.
 *
 * @cat resource
 */
class Address {
	AddressRaw m_resource = nullptr;

public:
	/// Default ctor
	constexpr Address(std::nullptr_t = nullptr) noexcept
		: m_resource(nullptr) {
	}

	/**
	 * Constructs from raw Address.
	 *
	 * @param resource a AddressRaw to be wrapped.
	 *
	 * This assumes the ownership, call Release() if you need to take back.
	 */
	constexpr explicit Address(AddressRaw resource) noexcept
		: m_resource(resource) {
	}

	/// Copy constructor — increments the reference count.
	Address(const Address& other) noexcept
		: m_resource(other.m_resource ? NET_RefAddress(other.m_resource) : nullptr) {
	}

	/// Move constructor
	constexpr Address(Address&& other) noexcept
		: Address(other.Release()) {
	}

	constexpr Address(const AddressRef& other) = delete;
	constexpr Address(AddressRef&& other) = delete;

	/**
	 * Resolve a human-readable hostname asynchronously.
	 *
	 * Note that resolving an address is an asynchronous operation. This function
	 * will not block. It returns an unresolved Address. Until the address
	 * resolves, it can't be used.
	 *
	 * If you want to block until resolution is finished, call
	 * Address.WaitUntilResolved(). Otherwise, use Address.GetStatus().
	 *
	 * @param host The hostname to resolve.
	 * @post a new Address on success.
	 * @throws Error on failure.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa Address.WaitUntilResolved
	 * @sa Address.GetStatus
	 */
	explicit Address(StringParam host);

	/// Destructor — decrements the reference count.
	~Address() {
		if (m_resource) NET_UnrefAddress(m_resource);
	}

	/// Copy assignment — increments reference count.
	Address& operator=(const Address& other) noexcept {
		if (this != &other) {
			if (m_resource) NET_UnrefAddress(m_resource);
			m_resource = other.m_resource ? NET_RefAddress(other.m_resource) : nullptr;
		}
		return *this;
	}

	/// Move assignment.
	constexpr Address& operator=(Address&& other) noexcept {
		std::swap(m_resource, other.m_resource);
		return *this;
	}

	/// Retrieves underlying AddressRaw.
	constexpr AddressRaw Get() const noexcept { return m_resource; }

	/// Retrieves underlying AddressRaw and clears this (releases ownership).
	constexpr AddressRaw Release() noexcept {
		auto r = m_resource;
		m_resource = nullptr;
		return r;
	}

	/// Comparison
	constexpr auto operator<=>(const Address& other) const noexcept = default;

	/// Converts to bool
	constexpr explicit operator bool() const noexcept { return !!m_resource; }

	/**
	 * Block until an address is resolved.
	 *
	 * @param timeout Number of milliseconds to wait. -1 to wait indefinitely,
	 *                0 to check once without waiting.
	 * @returns NET_SUCCESS if resolved, NET_FAILURE if resolution failed,
	 *          NET_WAITING if still resolving.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa Address.GetStatus
	 */
	Status WaitUntilResolved(Sint32 timeout) const;

	/**
	 * Check if an address is resolved, without blocking.
	 *
	 * @returns NET_SUCCESS if resolved, NET_FAILURE if resolution failed,
	 *          NET_WAITING if still resolving.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa Address.WaitUntilResolved
	 */
	Status GetStatus() const;

	/**
	 * Get a human-readable string from a resolved address.
	 *
	 * Returns a string like "159.203.69.7" or "2604:a880:800:a1::71f:3001".
	 * Returns nullptr if resolution is still in progress or failed.
	 *
	 * Do not free or modify the returned string; it belongs to this Address.
	 *
	 * @returns a string, or nullptr if not yet resolved.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	const char* GetString() const;

	/**
	 * Enable simulated address resolution failures.
	 *
	 * @param percent_loss A number between 0 and 100. Higher means more
	 *                     failures. Zero to disable.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	static void SimulateResolutionLoss(int percent_loss);

	/**
	 * Compare two Address objects.
	 *
	 * @param other the other address to compare with.
	 * @returns a value less than zero if this is "less than" other, greater than
	 *          zero if "greater than", zero if equal.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	int Compare(const Address& other) const;
};

/**
 * Reference for Address.
 *
 * This does not take ownership!
 */
struct AddressRef : Address {
	using Address::Address;

	/**
	 * Constructs from raw Address.
	 *
	 * @param resource a AddressRaw.
	 *
	 * This does not take ownership!
	 */
	constexpr AddressRef(AddressRaw resource) noexcept
		: Address(resource) {
	}

	/**
	 * Constructs from Address.
	 *
	 * @param resource a Address.
	 *
	 * This does not take ownership!
	 */
	constexpr AddressRef(const Address& resource) noexcept
		: Address(resource.Get()) {
	}

	/**
	 * Constructs from Address.
	 *
	 * @param resource a Address.
	 *
	 * This will Release the ownership from resource!
	 */
	constexpr AddressRef(Address&& resource) noexcept
		: Address(std::move(resource).Release()) {
	}

	/// Copy constructor.
	constexpr AddressRef(const AddressRef& other) noexcept
		: Address(other.Get()) {
	}

	/// Move constructor.
	constexpr AddressRef(AddressRef&& other) noexcept
		: Address(other.Get()) {
	}

	/// Destructor — does NOT unref (non-owning).
	~AddressRef() { Release(); }

	/// Assignment operator.
	AddressRef& operator=(const AddressRef& other) noexcept {
		Release();
		Address::operator=(Address(other.Get()));
		return *this;
	}

	/// Converts to AddressRaw
	constexpr operator AddressRaw() const noexcept { return Get(); }
};

// =============================================================================
// StreamSocket
// =============================================================================

/**
 * An object that represents a streaming connection to another system (TCP).
 *
 * Each StreamSocket represents a single connection between systems.
 *
 * @since This datatype is available since SDL_net 3.0.0.
 *
 * @cat resource
 */
class StreamSocket {
	StreamSocketRaw m_resource = nullptr;

public:
	/// Default ctor
	constexpr StreamSocket(std::nullptr_t = nullptr) noexcept
		: m_resource(nullptr) {
	}

	/**
	 * Constructs from raw StreamSocket.
	 *
	 * @param resource a StreamSocketRaw to be wrapped.
	 *
	 * This assumes the ownership, call Release() if you need to take back.
	 */
	constexpr explicit StreamSocket(StreamSocketRaw resource) noexcept
		: m_resource(resource) {
	}

	/// Copy constructor
	constexpr StreamSocket(const StreamSocket& other) noexcept = delete;

	/// Move constructor
	constexpr StreamSocket(StreamSocket&& other) noexcept
		: StreamSocket(other.Release()) {
	}

	constexpr StreamSocket(const StreamSocketRef& other) = delete;
	constexpr StreamSocket(StreamSocketRef&& other) = delete;

	/**
	 * Begin connecting a socket as a client to a remote server.
	 *
	 * Connecting is an asynchronous operation; this function does not block.
	 * Use StreamSocket.WaitUntilConnected() or StreamSocket.GetConnectionStatus()
	 * to check when the operation has completed.
	 *
	 * @param address the address of the remote server to connect to.
	 * @param port the port on the remote server to connect to.
	 * @post a new StreamSocket, pending connection, on success.
	 * @throws Error on failure.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.WaitUntilConnected
	 * @sa StreamSocket.GetConnectionStatus
	 */
	StreamSocket(AddressRef address, Uint16 port);

	/// Destructor
	~StreamSocket() { NET_DestroyStreamSocket(m_resource); }

	/// Move assignment.
	constexpr StreamSocket& operator=(StreamSocket&& other) noexcept {
		std::swap(m_resource, other.m_resource);
		return *this;
	}

	/// Copy assignment (deleted)
	StreamSocket& operator=(const StreamSocket& other) = delete;

	/// Retrieves underlying StreamSocketRaw.
	constexpr StreamSocketRaw Get() const noexcept { return m_resource; }

	/// Retrieves underlying StreamSocketRaw and clears this.
	constexpr StreamSocketRaw Release() noexcept {
		auto r = m_resource;
		m_resource = nullptr;
		return r;
	}

	/// Comparison
	constexpr auto operator<=>(const StreamSocket& other) const noexcept = default;

	/// Converts to bool
	constexpr explicit operator bool() const noexcept { return !!m_resource; }

	/**
	 * Dispose of this stream socket.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void Destroy();

	/**
	 * Block until this stream socket has connected to a server.
	 *
	 * @param timeout Number of milliseconds to wait. -1 to wait indefinitely,
	 *                0 to check once without waiting.
	 * @returns NET_SUCCESS if connected, NET_FAILURE if failed, NET_WAITING if
	 *          still connecting.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.GetConnectionStatus
	 */
	Status WaitUntilConnected(Sint32 timeout) const;

	/**
	 * Check if this stream socket is connected, without blocking.
	 *
	 * @returns NET_SUCCESS if connected, NET_FAILURE if failed, NET_WAITING if
	 *          still connecting.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.WaitUntilConnected
	 */
	Status GetConnectionStatus() const;

	/**
	 * Get the remote address of this stream socket.
	 *
	 * This adds a reference to the address; the caller must unref it when done.
	 * The returned Address owns the reference.
	 *
	 * @returns the socket's remote address on success.
	 * @throws Error on failure.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	Address GetAddress() const;

	/**
	 * Send bytes over this stream socket to a remote system.
	 *
	 * This call never blocks. If it can't send the data immediately, the library
	 * will queue it for later transmission.
	 *
	 * @param buf a pointer to the data to send.
	 * @param buflen the size of the data to send, in bytes.
	 * @returns true if data sent or queued, false on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.GetPendingWrites
	 * @sa StreamSocket.WaitUntilDrained
	 * @sa StreamSocket.Read
	 */
	bool Write(const void* buf, int buflen);

	/**
	 * Query bytes still pending transmission on this stream socket.
	 *
	 * @returns number of bytes still pending, -1 on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.Write
	 * @sa StreamSocket.WaitUntilDrained
	 */
	int GetPendingWrites() const;

	/**
	 * Block until all of this stream socket's pending data is sent.
	 *
	 * @param timeout Number of milliseconds to wait. -1 to wait indefinitely,
	 *                0 to check once without waiting.
	 * @returns number of bytes still pending, -1 on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.Write
	 * @sa StreamSocket.GetPendingWrites
	 */
	int WaitUntilDrained(Sint32 timeout);

	/**
	 * Receive bytes that a remote system sent to this stream socket.
	 *
	 * This call never blocks; returns 0 immediately if no data is available.
	 *
	 * @param buf a pointer to a buffer where received data will be stored.
	 * @param buflen the size of the buffer in bytes.
	 * @returns number of bytes read (can be 0 if none available), -1 on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa StreamSocket.Write
	 */
	int Read(void* buf, int buflen);

	/**
	 * Enable simulated stream socket failures.
	 *
	 * @param percent_loss A number between 0 and 100. Zero to disable.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void SimulatePacketLoss(int percent_loss);
};

/**
 * Reference for StreamSocket.
 *
 * This does not take ownership!
 */
struct StreamSocketRef : StreamSocket {
	using StreamSocket::StreamSocket;

	/**
	 * Constructs from raw StreamSocket.
	 *
	 * This does not take ownership!
	 */
	constexpr StreamSocketRef(StreamSocketRaw resource) noexcept
		: StreamSocket(resource) {
	}

	/**
	 * Constructs from StreamSocket.
	 *
	 * This does not take ownership!
	 */
	constexpr StreamSocketRef(const StreamSocket& resource) noexcept
		: StreamSocket(resource.Get()) {
	}

	/**
	 * Constructs from StreamSocket.
	 *
	 * This will Release the ownership from resource!
	 */
	constexpr StreamSocketRef(StreamSocket&& resource) noexcept
		: StreamSocket(std::move(resource).Release()) {
	}

	/// Copy constructor.
	constexpr StreamSocketRef(const StreamSocketRef& other) noexcept
		: StreamSocket(other.Get()) {
	}

	/// Move constructor.
	constexpr StreamSocketRef(StreamSocketRef&& other) noexcept
		: StreamSocket(other.Get()) {
	}

	/// Destructor — does NOT destroy (non-owning).
	~StreamSocketRef() { Release(); }

	/// Assignment operator.
	StreamSocketRef& operator=(const StreamSocketRef& other) noexcept {
		Release();
		StreamSocket::operator=(StreamSocket(other.Get()));
		return *this;
	}

	/// Converts to StreamSocketRaw
	constexpr operator StreamSocketRaw() const noexcept { return Get(); }
};

// =============================================================================
// Server
// =============================================================================

/**
 * The receiving end of a stream connection (TCP server / listen socket).
 *
 * Clients attempt to connect to a server, and if the server accepts the
 * connection, will provide the app with a stream socket for communication.
 *
 * @since This datatype is available since SDL_net 3.0.0.
 *
 * @cat resource
 */
class Server {
	ServerRaw m_resource = nullptr;

public:
	/// Default ctor
	constexpr Server(std::nullptr_t = nullptr) noexcept
		: m_resource(nullptr) {
	}

	/**
	 * Constructs from raw Server.
	 *
	 * @param resource a ServerRaw to be wrapped.
	 *
	 * This assumes the ownership, call Release() if you need to take back.
	 */
	constexpr explicit Server(ServerRaw resource) noexcept
		: m_resource(resource) {
	}

	/// Copy constructor
	constexpr Server(const Server& other) noexcept = delete;

	/// Move constructor
	constexpr Server(Server&& other) noexcept
		: Server(other.Release()) {
	}

	constexpr Server(const ServerRef& other) = delete;
	constexpr Server(ServerRef&& other) = delete;

	/**
	 * Create a server that listens for connections to accept.
	 *
	 * Specify a local address to listen on, or nullptr to listen on all
	 * available addresses.
	 *
	 * @param addr the local address to listen on, or nullptr.
	 * @param port the port on the local address to listen on.
	 * @post a new Server on success.
	 * @throws Error on failure.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa Server.AcceptClient
	 */
	Server(AddressRef addr, Uint16 port);

	/// Destructor
	~Server() { NET_DestroyServer(m_resource); }

	/// Move assignment.
	constexpr Server& operator=(Server&& other) noexcept {
		std::swap(m_resource, other.m_resource);
		return *this;
	}

	/// Copy assignment (deleted)
	Server& operator=(const Server& other) = delete;

	/// Retrieves underlying ServerRaw.
	constexpr ServerRaw Get() const noexcept { return m_resource; }

	/// Retrieves underlying ServerRaw and clears this.
	constexpr ServerRaw Release() noexcept {
		auto r = m_resource;
		m_resource = nullptr;
		return r;
	}

	/// Comparison
	constexpr auto operator<=>(const Server& other) const noexcept = default;

	/// Converts to bool
	constexpr explicit operator bool() const noexcept { return !!m_resource; }

	/**
	 * Dispose of this server.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void Destroy();

	/**
	 * Create a stream socket for the next pending client connection.
	 *
	 * This function does not block. If there are no new connections pending,
	 * `client_stream` will be set to nullptr (not an error).
	 *
	 * Call this in a loop until client_stream is nullptr to accept all pending
	 * connections.
	 *
	 * @param client_stream Will be set to a new StreamSocket if a connection was
	 *                      pending, nullptr otherwise.
	 * @returns true on success (even if no new connections were pending), false
	 *          on error.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa Server.AcceptClient (returning variant)
	 */
	bool AcceptClient(StreamSocket& client_stream);

	/**
	 * Accept the next pending client connection.
	 *
	 * @returns a new StreamSocket if a connection was pending, nullptr otherwise.
	 * @throws Error on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	StreamSocket AcceptClient();
};

/**
 * Reference for Server.
 *
 * This does not take ownership!
 */
struct ServerRef : Server {
	using Server::Server;

	/**
	 * Constructs from raw Server.
	 *
	 * This does not take ownership!
	 */
	constexpr ServerRef(ServerRaw resource) noexcept
		: Server(resource) {
	}

	/**
	 * Constructs from Server.
	 *
	 * This does not take ownership!
	 */
	constexpr ServerRef(const Server& resource) noexcept
		: Server(resource.Get()) {
	}

	/**
	 * Constructs from Server.
	 *
	 * This will Release the ownership from resource!
	 */
	constexpr ServerRef(Server&& resource) noexcept
		: Server(std::move(resource).Release()) {
	}

	/// Copy constructor.
	constexpr ServerRef(const ServerRef& other) noexcept
		: Server(other.Get()) {
	}

	/// Move constructor.
	constexpr ServerRef(ServerRef&& other) noexcept
		: Server(other.Get()) {
	}

	/// Destructor — does NOT destroy (non-owning).
	~ServerRef() { Release(); }

	/// Assignment operator.
	ServerRef& operator=(const ServerRef& other) noexcept {
		Release();
		Server::operator=(Server(other.Get()));
		return *this;
	}

	/// Converts to ServerRaw
	constexpr operator ServerRaw() const noexcept { return Get(); }
};

// =============================================================================
// DatagramSocket
// =============================================================================

/**
 * An object that represents a datagram connection (UDP socket).
 *
 * Datagram sockets are not limited to talking to a single other remote system,
 * do not maintain a single "connection", and are more nimble about network
 * failures at the expense of being more complex to use.
 *
 * @since This datatype is available since SDL_net 3.0.0.
 *
 * @cat resource
 */
class DatagramSocket {
	DatagramSocketRaw m_resource = nullptr;

public:
	/// Default ctor
	constexpr DatagramSocket(std::nullptr_t = nullptr) noexcept
		: m_resource(nullptr) {
	}

	/**
	 * Constructs from raw DatagramSocket.
	 *
	 * @param resource a DatagramSocketRaw to be wrapped.
	 *
	 * This assumes the ownership, call Release() if you need to take back.
	 */
	constexpr explicit DatagramSocket(DatagramSocketRaw resource) noexcept
		: m_resource(resource) {
	}

	/// Copy constructor
	constexpr DatagramSocket(const DatagramSocket& other) noexcept = delete;

	/// Move constructor
	constexpr DatagramSocket(DatagramSocket&& other) noexcept
		: DatagramSocket(other.Release()) {
	}

	constexpr DatagramSocket(const DatagramSocketRef& other) = delete;
	constexpr DatagramSocket(DatagramSocketRef&& other) = delete;

	/**
	 * Create and bind a new datagram socket.
	 *
	 * Specify a local address to bind to, or nullptr to listen on all available
	 * local addresses.
	 *
	 * Specify port 0 to let the system pick an unused port (recommended for
	 * clients).
	 *
	 * @param addr the local address to bind on, or nullptr.
	 * @param port the port on the local address to bind on, or 0.
	 * @post a new DatagramSocket on success.
	 * @throws Error on failure.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	DatagramSocket(AddressRef addr, Uint16 port);

	/// Destructor
	~DatagramSocket() { NET_DestroyDatagramSocket(m_resource); }

	/// Move assignment.
	constexpr DatagramSocket& operator=(DatagramSocket&& other) noexcept {
		std::swap(m_resource, other.m_resource);
		return *this;
	}

	/// Copy assignment (deleted)
	DatagramSocket& operator=(const DatagramSocket& other) = delete;

	/// Retrieves underlying DatagramSocketRaw.
	constexpr DatagramSocketRaw Get() const noexcept { return m_resource; }

	/// Retrieves underlying DatagramSocketRaw and clears this.
	constexpr DatagramSocketRaw Release() noexcept {
		auto r = m_resource;
		m_resource = nullptr;
		return r;
	}

	/// Comparison
	constexpr auto operator<=>(const DatagramSocket& other) const noexcept = default;

	/// Converts to bool
	constexpr explicit operator bool() const noexcept { return !!m_resource; }

	/**
	 * Dispose of this datagram socket.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void Destroy();

	/**
	 * Send a new packet over this datagram socket to a remote system.
	 *
	 * This call never blocks.
	 *
	 * @param address the destination NET_Address object.
	 * @param port the destination address port.
	 * @param buf a pointer to the data to send as a single packet.
	 * @param buflen the size of the data to send, in bytes.
	 * @returns true if data sent or queued, false on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa DatagramSocket.Receive
	 */
	bool Send(AddressRef address, Uint16 port, const void* buf, int buflen);

	/**
	 * Receive a new packet from this datagram socket.
	 *
	 * This call never blocks; returns true with nullptr dgram if no data.
	 *
	 * @param dgram a pointer to the datagram packet pointer. Will be set to
	 *              nullptr if no packets are available.
	 * @returns true on success (even if no packets available), false on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 *
	 * @sa DatagramSocket.Send
	 */
	bool Receive(Datagram& dgram);

	/**
	 * Receive a new packet from this datagram socket.
	 *
	 * @returns a new Datagram if one is available, nullptr otherwise.
	 * @throws Error on failure.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	Datagram Receive();

	/**
	 * Enable simulated datagram socket failures.
	 *
	 * @param percent_loss A number between 0 and 100. Zero to disable.
	 *
	 * @threadsafety It is safe to call this function from any thread.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void SimulatePacketLoss(int percent_loss);
};

/**
 * Reference for DatagramSocket.
 *
 * This does not take ownership!
 */
struct DatagramSocketRef : DatagramSocket {
	using DatagramSocket::DatagramSocket;

	/**
	 * Constructs from raw DatagramSocket.
	 *
	 * This does not take ownership!
	 */
	constexpr DatagramSocketRef(DatagramSocketRaw resource) noexcept
		: DatagramSocket(resource) {
	}

	/**
	 * Constructs from DatagramSocket.
	 *
	 * This does not take ownership!
	 */
	constexpr DatagramSocketRef(const DatagramSocket& resource) noexcept
		: DatagramSocket(resource.Get()) {
	}

	/**
	 * Constructs from DatagramSocket.
	 *
	 * This will Release the ownership from resource!
	 */
	constexpr DatagramSocketRef(DatagramSocket&& resource) noexcept
		: DatagramSocket(std::move(resource).Release()) {
	}

	/// Copy constructor.
	constexpr DatagramSocketRef(const DatagramSocketRef& other) noexcept
		: DatagramSocket(other.Get()) {
	}

	/// Move constructor.
	constexpr DatagramSocketRef(DatagramSocketRef&& other) noexcept
		: DatagramSocket(other.Get()) {
	}

	/// Destructor — does NOT destroy (non-owning).
	~DatagramSocketRef() { Release(); }

	/// Assignment operator.
	DatagramSocketRef& operator=(const DatagramSocketRef& other) noexcept {
		Release();
		DatagramSocket::operator=(DatagramSocket(other.Get()));
		return *this;
	}

	/// Converts to DatagramSocketRaw
	constexpr operator DatagramSocketRaw() const noexcept { return Get(); }
};

// =============================================================================
// Datagram
// =============================================================================

/**
 * The data provided for new incoming packets from DatagramSocket.Receive().
 *
 * @since This datatype is available since SDL_net 3.0.0.
 *
 * @cat resource
 */
class Datagram {
	DatagramRaw m_resource = nullptr;

public:
	/// Default ctor
	constexpr Datagram(std::nullptr_t = nullptr) noexcept
		: m_resource(nullptr) {
	}

	/**
	 * Constructs from raw Datagram.
	 *
	 * @param resource a DatagramRaw to be wrapped.
	 *
	 * This assumes the ownership, call Release() if you need to take back.
	 */
	constexpr explicit Datagram(DatagramRaw resource) noexcept
		: m_resource(resource) {
	}

	/// Copy constructor
	constexpr Datagram(const Datagram& other) noexcept = delete;

	/// Move constructor
	constexpr Datagram(Datagram&& other) noexcept
		: Datagram(other.Release()) {
	}

	constexpr Datagram(const DatagramRef& other) = delete;
	constexpr Datagram(DatagramRef&& other) = delete;

	/// Destructor
	~Datagram() { NET_DestroyDatagram(m_resource); }

	/// Move assignment.
	constexpr Datagram& operator=(Datagram&& other) noexcept {
		std::swap(m_resource, other.m_resource);
		return *this;
	}

	/// Copy assignment (deleted)
	Datagram& operator=(const Datagram& other) = delete;

	/// Retrieves underlying DatagramRaw.
	constexpr DatagramRaw Get() const noexcept { return m_resource; }

	/// Retrieves underlying DatagramRaw and clears this.
	constexpr DatagramRaw Release() noexcept {
		auto r = m_resource;
		m_resource = nullptr;
		return r;
	}

	/// Comparison
	constexpr auto operator<=>(const Datagram& other) const noexcept = default;

	/// Converts to bool
	constexpr explicit operator bool() const noexcept { return !!m_resource; }

	/**
	 * Dispose of this datagram packet.
	 *
	 * @since This function is available since SDL_net 3.0.0.
	 */
	void Destroy();

	/**
	 * Get the sender's address.
	 *
	 * The returned AddressRef is valid as long as this Datagram is alive.
	 * If you want to keep it beyond that, use Address(addr.Get()) with
	 * NET_RefAddress, or just copy via Address::Address(const Address&).
	 *
	 * @returns AddressRef for the sender's address (non-owning).
	 */
	AddressRef GetAddress() const;

	/**
	 * Get the sender's port.
	 *
	 * @returns the sender's port in host byte order.
	 */
	Uint16 GetPort() const;

	/**
	 * Get a pointer to the packet payload.
	 *
	 * @returns pointer to the packet data.
	 */
	const Uint8* GetData() const;

	/**
	 * Get the size of the packet payload in bytes.
	 *
	 * @returns the number of bytes in the payload.
	 */
	int GetSize() const;
};

/**
 * Reference for Datagram.
 *
 * This does not take ownership!
 */
struct DatagramRef : Datagram {
	using Datagram::Datagram;

	/**
	 * Constructs from raw Datagram.
	 *
	 * This does not take ownership!
	 */
	constexpr DatagramRef(DatagramRaw resource) noexcept
		: Datagram(resource) {
	}

	/**
	 * Constructs from Datagram.
	 *
	 * This does not take ownership!
	 */
	constexpr DatagramRef(const Datagram& resource) noexcept
		: Datagram(resource.Get()) {
	}

	/**
	 * Constructs from Datagram.
	 *
	 * This will Release the ownership from resource!
	 */
	constexpr DatagramRef(Datagram&& resource) noexcept
		: Datagram(std::move(resource).Release()) {
	}

	/// Copy constructor.
	constexpr DatagramRef(const DatagramRef& other) noexcept
		: Datagram(other.Get()) {
	}

	/// Move constructor.
	constexpr DatagramRef(DatagramRef&& other) noexcept
		: Datagram(other.Get()) {
	}

	/// Destructor — does NOT destroy (non-owning).
	~DatagramRef() { Release(); }

	/// Assignment operator.
	DatagramRef& operator=(const DatagramRef& other) noexcept {
		Release();
		Datagram::operator=(Datagram(other.Get()));
		return *this;
	}

	/// Converts to DatagramRaw
	constexpr operator DatagramRaw() const noexcept { return Get(); }
};

// =============================================================================
// Free functions
// =============================================================================

/**
 * Resolve a human-readable hostname asynchronously.
 *
 * @param host The hostname to resolve.
 * @returns a new Address on success.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 *
 * @sa Address.WaitUntilResolved
 * @sa Address.GetStatus
 */
inline Address ResolveHostname(StringParam host) { return Address(std::move(host)); }

/**
 * Compare two Address objects.
 *
 * @param a first address.
 * @param b second address.
 * @returns a value less than zero, zero, or greater than zero.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline int CompareAddresses(const Address& a, const Address& b) {
	return NET_CompareAddresses(a.Get(), b.Get());
}

/**
 * Obtain a list of local addresses on the system.
 *
 * The returned array is NULL-terminated. Pass it to FreeLocalAddresses when
 * done. You can call NET_RefAddress() on any addresses you want to keep.
 *
 * @param num_addresses on exit, set to the number of addresses returned.
 *                      Can be nullptr.
 * @returns a NULL-terminated array of AddressRaw pointers.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 *
 * @sa FreeLocalAddresses
 */
inline AddressRaw* GetLocalAddresses(int* num_addresses) {
	return CheckError(NET_GetLocalAddresses(num_addresses));
}

/**
 * Free the results from GetLocalAddresses().
 *
 * @param addresses A pointer returned by GetLocalAddresses().
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 *
 * @sa GetLocalAddresses
 */
inline void FreeLocalAddresses(AddressRaw* addresses) {
	NET_FreeLocalAddresses(addresses);
}

/**
 * Create a TCP client socket connecting to a remote server.
 *
 * @param address the address of the remote server.
 * @param port the port on the remote server.
 * @returns a new StreamSocket, pending connection.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline StreamSocket CreateClient(AddressRef address, Uint16 port) {
	return StreamSocket(std::move(address), port);
}

/**
 * Create a TCP server that listens for connections.
 *
 * @param addr the local address to listen on, or nullptr.
 * @param port the port on the local address to listen on.
 * @returns a new Server.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline Server CreateServer(AddressRef addr, Uint16 port) {
	return Server(std::move(addr), port);
}

/**
 * Create a UDP datagram socket.
 *
 * @param addr the local address to bind on, or nullptr.
 * @param port the port on the local address, or 0 for system-chosen.
 * @returns a new DatagramSocket.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline DatagramSocket CreateDatagramSocket(AddressRef addr, Uint16 port) {
	return DatagramSocket(std::move(addr), port);
}

/**
 * Block on multiple sockets until at least one has data available.
 *
 * The `vsockets` array can contain pointers to Server, StreamSocket, or
 * DatagramSocket objects, each cast to void*.
 *
 * @param vsockets an array of void* pointers to socket-like objects.
 * @param numsockets the number of pointers in the array.
 * @param timeout Number of milliseconds to wait. -1 to wait indefinitely,
 *                0 to check once without waiting.
 * @returns the number of items that have new input, or -1 on error.
 *
 * @since This function is available since SDL_net 3.0.0.
 */
inline int WaitUntilInputAvailable(void** vsockets, int numsockets,
																	 Sint32 timeout) {
	return NET_WaitUntilInputAvailable(vsockets, numsockets, timeout);
}

// =============================================================================
// Inline implementations
// =============================================================================

inline Address::Address(StringParam host)
	: m_resource(CheckError(NET_ResolveHostname(host))) {
}

inline Status Address::WaitUntilResolved(Sint32 timeout) const {
	return NET_WaitUntilResolved(m_resource, timeout);
}

inline Status Address::GetStatus() const {
	return NET_GetAddressStatus(m_resource);
}

inline const char* Address::GetString() const {
	return NET_GetAddressString(m_resource);
}

inline void Address::SimulateResolutionLoss(int percent_loss) {
	NET_SimulateAddressResolutionLoss(percent_loss);
}

inline int Address::Compare(const Address& other) const {
	return NET_CompareAddresses(m_resource, other.m_resource);
}

inline StreamSocket::StreamSocket(AddressRef address, Uint16 port)
	: m_resource(CheckError(NET_CreateClient(address.Get(), port))) {
}

inline void StreamSocket::Destroy() { NET_DestroyStreamSocket(Release()); }

inline Status StreamSocket::WaitUntilConnected(Sint32 timeout) const {
	return NET_WaitUntilConnected(m_resource, timeout);
}

inline Status StreamSocket::GetConnectionStatus() const {
	return NET_GetConnectionStatus(m_resource);
}

inline Address StreamSocket::GetAddress() const {
	return Address(CheckError(NET_GetStreamSocketAddress(m_resource)));
}

inline bool StreamSocket::Write(const void* buf, int buflen) {
	return NET_WriteToStreamSocket(m_resource, buf, buflen);
}

inline int StreamSocket::GetPendingWrites() const {
	return NET_GetStreamSocketPendingWrites(m_resource);
}

inline int StreamSocket::WaitUntilDrained(Sint32 timeout) {
	return NET_WaitUntilStreamSocketDrained(m_resource, timeout);
}

inline int StreamSocket::Read(void* buf, int buflen) {
	return NET_ReadFromStreamSocket(m_resource, buf, buflen);
}

inline void StreamSocket::SimulatePacketLoss(int percent_loss) {
	NET_SimulateStreamPacketLoss(m_resource, percent_loss);
}

inline Server::Server(AddressRef addr, Uint16 port)
	: m_resource(CheckError(NET_CreateServer(addr.Get(), port))) {
}

inline void Server::Destroy() { NET_DestroyServer(Release()); }

inline bool Server::AcceptClient(StreamSocket& client_stream) {
	StreamSocketRaw raw = nullptr;
	bool ok = NET_AcceptClient(m_resource, &raw);
	client_stream = StreamSocket(raw);
	return ok;
}

inline StreamSocket Server::AcceptClient() {
	StreamSocketRaw raw = nullptr;
	CheckError(NET_AcceptClient(m_resource, &raw));
	return StreamSocket(raw);
}

inline DatagramSocket::DatagramSocket(AddressRef addr, Uint16 port)
	: m_resource(CheckError(NET_CreateDatagramSocket(addr.Get(), port))) {
}

inline void DatagramSocket::Destroy() { NET_DestroyDatagramSocket(Release()); }

inline bool DatagramSocket::Send(AddressRef address, Uint16 port,
																 const void* buf, int buflen) {
	return NET_SendDatagram(m_resource, address.Get(), port, buf, buflen);
}

inline bool DatagramSocket::Receive(Datagram& dgram) {
	DatagramRaw raw = nullptr;
	bool ok = NET_ReceiveDatagram(m_resource, &raw);
	dgram = Datagram(raw);
	return ok;
}

inline Datagram DatagramSocket::Receive() {
	DatagramRaw raw = nullptr;
	CheckError(NET_ReceiveDatagram(m_resource, &raw));
	return Datagram(raw);
}

inline void DatagramSocket::SimulatePacketLoss(int percent_loss) {
	NET_SimulateDatagramPacketLoss(m_resource, percent_loss);
}

inline void Datagram::Destroy() { NET_DestroyDatagram(Release()); }

inline AddressRef Datagram::GetAddress() const {
	return AddressRef(m_resource ? m_resource->addr : nullptr);
}

inline Uint16 Datagram::GetPort() const {
	return m_resource ? m_resource->port : 0;
}

inline const Uint8* Datagram::GetData() const {
	return m_resource ? m_resource->buf : nullptr;
}

inline int Datagram::GetSize() const {
	return m_resource ? m_resource->buflen : 0;
}

// ── MessageFramer ─────────────────────────────────────────────────────────────

/**
 * @brief Length-prefixed message framing over a TCP stream socket.
 *
 * Wraps a `StreamSocketRef` with simple framing: each message is preceded by
 * a 4-byte little-endian `uint32_t` containing the payload length.  This lets
 * you send/receive discrete messages over a byte-stream without worrying about
 * partial reads.
 *
 * ### Sending
 * ```cpp
 * MessageFramer framer;
 * framer.Send(sock, msg.data(), static_cast<int>(msg.size()));
 * ```
 *
 * ### Receiving (non-blocking poll loop)
 * ```cpp
 * MessageFramer framer;
 * std::vector<Uint8> out;
 * while (running) {
 *     framer.Fill(sock);          // pump bytes from socket into internal buffer
 *     while (framer.Receive(out)) // extract complete messages one by one
 *         handle(out);
 * }
 * ```
 */
struct MessageFramer {
	static constexpr int MAX_MSG_SIZE = 1 * 1024 * 1024; ///< 1 MiB hard limit

	/// Internal receive accumulator
	std::vector<Uint8> m_buf;

	/**
	 * @brief Send @p data as a framed message on @p sock.
	 * @param sock  Destination stream socket (must be connected).
	 * @param data  Pointer to payload bytes.
	 * @param len   Payload length in bytes.
	 * @return `true` on success, `false` on write error (check `SDL_GetError()`).
	 */
	bool Send(StreamSocketRef sock, const void* data, int len);

	/**
	 * @brief Read available bytes from @p sock into the internal buffer.
	 *
	 * This is non-blocking in the sense that it reads whatever bytes the socket
	 * has buffered right now (via `StreamSocket::Read`) and appends them to the
	 * internal accumulator.  Call it repeatedly in your poll loop before calling
	 * `Receive()`.
	 *
	 * @param sock  Source stream socket.
	 * @return `true` if at least one byte was read, `false` otherwise (including
	 *         on error — check `SDL_GetError()`).
	 */
	bool Fill(StreamSocketRef sock);

	/**
	 * @brief Extract the next complete message from the internal buffer.
	 *
	 * If a full length-prefixed message is available in `m_buf`, the payload is
	 * moved into @p out and the consumed bytes are removed from `m_buf`.
	 *
	 * @param out  Destination vector; resized and filled with the message payload.
	 * @return `true` when a complete message was extracted, `false` when more
	 *         bytes are needed.
	 */
	bool Receive(std::vector<Uint8>& out);
};

inline bool MessageFramer::Send(StreamSocketRef sock, const void* data, int len)
{
	// Write 4-byte LE length prefix
	Uint8 hdr[4] = {
		static_cast<Uint8>(static_cast<Uint32>(len) & 0xFF),
		static_cast<Uint8>((static_cast<Uint32>(len) >> 8) & 0xFF),
		static_cast<Uint8>((static_cast<Uint32>(len) >> 16) & 0xFF),
		static_cast<Uint8>((static_cast<Uint32>(len) >> 24) & 0xFF),
	};
	if (!sock.Write(hdr, 4)) return false;
	if (len > 0 && !sock.Write(data, len)) return false;
	return true;
}

inline bool MessageFramer::Fill(StreamSocketRef sock)
{
	Uint8 tmp[4096];
	int n = sock.Read(tmp, static_cast<int>(sizeof tmp));
	if (n <= 0) return false;
	m_buf.insert(m_buf.end(), tmp, tmp + n);
	return true;
}

inline bool MessageFramer::Receive(std::vector<Uint8>& out)
{
	if (m_buf.size() < 4) return false;

	Uint32 len =
		static_cast<Uint32>(m_buf[0]) |
		(static_cast<Uint32>(m_buf[1]) << 8) |
		(static_cast<Uint32>(m_buf[2]) << 16) |
		(static_cast<Uint32>(m_buf[3]) << 24);

	if (len > static_cast<Uint32>(MAX_MSG_SIZE)) {
		// Malformed frame — discard everything
		m_buf.clear();
		return false;
	}

	if (m_buf.size() < 4 + len) return false;

	out.assign(m_buf.begin() + 4, m_buf.begin() + 4 + len);
	m_buf.erase(m_buf.begin(), m_buf.begin() + 4 + len);
	return true;
}

/// @}

} // namespace SDL

#endif // defined(SDL3PP_ENABLE_NET) || defined(SDL3PP_DOC)

#endif /* SDL3PP_NET_H_ */
