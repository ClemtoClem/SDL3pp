/**
 * @file 04_udp.cpp
 * @brief UDP ping-pong using SDL3pp_net.
 *
 * Spawns a "server" and a "client" in the same process using two threads.
 * The client sends 5 datagrams; the server receives and replies to each.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <vector>

static constexpr Uint16 SERVER_PORT  = 9001;
static constexpr Uint16 CLIENT_PORT  = 9002;
static constexpr int    NUM_MESSAGES = 5;
static constexpr int    BUFSIZE      = 256;

static std::atomic<bool> gServerReady{false};
static std::atomic<int>  gRepliesReceived{0};

// ── Server thread: receives datagrams and echoes them back ───────────────────
static void serverThread()
{
	SDL::DatagramSocket sock{nullptr, SERVER_PORT};
	SDL::Log(std::format("[server] Listening on UDP port {}", SERVER_PORT));
	gServerReady = true;

	int received = 0;
	while (received < NUM_MESSAGES) {
		void* vsock = sock.Get();
		if (SDL::WaitUntilInputAvailable(&vsock, 1, 500) <= 0) continue;

		SDL::Datagram dgram = sock.Receive();
		if (!dgram) continue;

		++received;
		SDL::Log(std::format("[server] Received packet {}/{} from port {}: {:.{}}",
			received, NUM_MESSAGES, dgram.GetPort(),
			reinterpret_cast<const char*>(dgram.GetData()), 
			dgram.GetSize()));

		// Echo the datagram back to the sender.
		// The datagram is still alive here so the AddressRef is valid.
		Uint16 senderPort = dgram.GetPort();
		std::string reply = std::format("[pong {}]", received);
		sock.Send(dgram.GetAddress(), senderPort, reply.data(),
				  static_cast<int>(reply.size()));
	}

	SDL::Log(std::format("[server] Done, received all {} datagrams.", NUM_MESSAGES));
}

// ── Client thread: sends datagrams and waits for replies ─────────────────────
static void clientThread()
{
	// Wait for the server to be ready
	while (!gServerReady) SDL_Delay(10);

	// Resolve loopback
	SDL::Address serverAddr{"127.0.0.1"};
	serverAddr.WaitUntilResolved(-1);

	// Bind client to a specific port so server knows where to reply
	SDL::DatagramSocket sock{nullptr, CLIENT_PORT};
	SDL::Log(std::format("[client] Bound to UDP port {}", CLIENT_PORT));

	std::vector<char> buf(BUFSIZE);

	for (int i = 1; i <= NUM_MESSAGES; ++i) {
		std::string msg = std::format("[ping {}]", i);
		SDL::Log(std::format("[client] Sending: {}", msg.c_str()));

		sock.Send(serverAddr, SERVER_PORT, msg.data(), static_cast<int>(msg.size()));

		// Wait for the echo reply
		void* vsock = sock.Get();
		if (SDL::WaitUntilInputAvailable(&vsock, 1, 1000) > 0) {
			SDL::Datagram reply = sock.Receive();
			if (reply) {
				SDL::Log(std::format("[client] Reply from port {}: {:.{}}",
					reply.GetPort(),
					reinterpret_cast<const char*>(reply.GetData()),
					reply.GetSize()).c_str());
				++gRepliesReceived;
			}
		} else {
			SDL::Log(std::format("[client] Timeout waiting for reply to ping {}", i));
		}

		SDL_Delay(50);
	}

	SDL::Log(std::format("[client] Done. Received {}/{} replies.", gRepliesReceived.load(), NUM_MESSAGES));
}

int main(int /*argc*/, char* /*argv*/[])
{
	SDL::NET::Init();
	SDL::Log(std::format("SDL_net version: {}\n", SDL::NET::Version()));

	// ── Simulate 20% packet loss to test robustness ──────────────────────────
	// (commented out by default — uncomment to test)
	// SDL::Address::SimulateResolutionLoss(0);

	std::thread server(serverThread);
	std::thread client(clientThread);

	client.join();
	server.join();

	SDL::Log(std::format("\nResult: {}/{} replies received.", gRepliesReceived.load(), NUM_MESSAGES));
	SDL::NET::Quit();
	return (gRepliesReceived == NUM_MESSAGES) ? 0 : 1;
}
