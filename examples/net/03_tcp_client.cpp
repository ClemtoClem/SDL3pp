/**
 * @file 03_tcp_client.cpp
 * @brief Simple TCP client using SDL3pp_net.
 *
 * Connects to the echo server at 127.0.0.1:7777 (run 02_tcp_server.cpp first),
 * sends several messages and prints the echoed replies.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <string>
#include <vector>

static constexpr Uint16 PORT    = 7777;
static constexpr int    BUFSIZE = 1024;

int main(int /*argc*/, char* /*argv*/[])
{
	SDL::NET::Init();
	SDL::Log("SDL_net version: {}", SDL::NET::Version());

	// ── Resolve the server address ────────────────────────────────────────────
	SDL::Address serverAddr{"127.0.0.1"};

	SDL::Log("Resolving 127.0.0.1 ...");
	SDL::Status resolved = serverAddr.WaitUntilResolved(5000);
	if (resolved != SDL::NET_SUCCESS_STATUS) {
		SDL::Log("Failed to resolve address: {}", SDL_GetError());
		SDL::NET::Quit();
		return 1;
	}
	SDL::Log("Resolved: {}", serverAddr.GetString());

	// ── Connect ───────────────────────────────────────────────────────────────
	SDL::StreamSocket sock{serverAddr, PORT};

	SDL::Log("Connecting to {}:{} ...", serverAddr.GetString(), PORT);
	SDL::Status connected = sock.WaitUntilConnected(5000);
	if (connected != SDL::NET_SUCCESS_STATUS) {
		SDL::Log("Failed to connect: {}", SDL_GetError());
		SDL::NET::Quit();
		return 1;
	}
	SDL::Log("Connected!");

	// ── Send/receive loop ─────────────────────────────────────────────────────
	const std::vector<std::string> messages = {
		"Hello, SDL3pp_net!",
		"This is a TCP test.",
		"Stream sockets are reliable.",
	};

	std::vector<char> buf(BUFSIZE);

	for (const auto& msg : messages) {
		SDL::Log("\nSending: {}", msg.c_str());

		if (!sock.Write(msg.data(), static_cast<int>(msg.size()))) {
			SDL::Log("Write failed: {}", SDL_GetError());
			break;
		}

		// Wait for the echo
		void* vsock = sock.Get();
		int ready = SDL::WaitUntilInputAvailable(&vsock, 1, 2000);
		if (ready <= 0) {
			SDL::Log("Timeout waiting for reply.");
			break;
		}

		int n = sock.Read(buf.data(), BUFSIZE - 1);
		if (n < 0) {
			SDL::Log("Read failed: {}", SDL_GetError());
			break;
		}
		if (n > 0) {
			buf[n] = '\0';
			SDL::Log("Received: {}", buf.data());
		}

		SDL_Delay(100);
	}

	// ── Query remote address ──────────────────────────────────────────────────
	SDL::Address remote = sock.GetAddress();
	SDL::Log("\nRemote address: {}", remote.GetString());

	SDL::Log("\nDisconnecting.");
	SDL::NET::Quit();
	return 0;
}
