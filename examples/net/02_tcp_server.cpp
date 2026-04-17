/**
 * @file 02_tcp_server.cpp
 * @brief Simple TCP echo server using SDL3pp_net.
 *
 * Listens on port 7777, accepts one client at a time, echoes every line back
 * prefixed with "[echo] ", then closes the connection when the client
 * disconnects.
 *
 * Run this first, then connect with 03_tcp_client.cpp or with:
 *   nc 127.0.0.1 7777
 *
 * Press Ctrl-C to stop the server.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <csignal>
#include <string>
#include <vector>

static volatile bool gRunning = true;

static void signalHandler(int) { gRunning = false; }

static constexpr Uint16 PORT = 7777;
static constexpr int    BUFSIZE = 1024;

int main(int /*argc*/, char* /*argv*/[])
{
	std::signal(SIGINT,  signalHandler);
	std::signal(SIGTERM, signalHandler);

	SDL::NET::Init();
	SDL::Log("SDL_net version: {}", SDL::NET::Version());

	// Create a TCP server on all interfaces, port 7777
	SDL::Server server{nullptr, PORT};
	SDL::Log("TCP echo server listening on port {} ...", PORT);
	SDL::Log("Connect with: nc 127.0.0.1 {}", PORT);
	SDL::Log("Press Ctrl-C to stop.\n");

	std::vector<char> buf(BUFSIZE);

	while (gRunning) {
		// ── Wait for a connection (block up to 250 ms so we can check gRunning)
		void* vsock = server.Get();
		SDL::WaitUntilInputAvailable(&vsock, 1, 250);

		SDL::StreamSocket client = server.AcceptClient();
		if (!client) continue; // no connection yet

		SDL::Log("Client connected.");

		// ── Echo loop for this client
		while (gRunning) {
			// Poll for data (100 ms)
			void* cs = client.Get();
			if (SDL::WaitUntilInputAvailable(&cs, 1, 100) < 0) break;

			int n = client.Read(buf.data(), BUFSIZE - 1);
			if (n < 0) {
				SDL::Log("Read error: {}", SDL_GetError());
				break;
			}
			if (n == 0) continue; // nothing to read yet

			buf[n] = '\0';
			SDL::Log("Received {} bytes: {}", n, buf.data());

			// Echo back prefixed
			std::string reply = "[echo] ";
			reply.append(buf.data(), n);
			if (!client.Write(reply.data(), static_cast<int>(reply.size()))) {
				SDL::Log("Write error: {}", SDL_GetError());
				break;
			}

			// Drain before next read
			client.WaitUntilDrained(1000);
		}

		SDL::Log("Client disconnected.\n");
		// client is destroyed here (RAII)
	}

	SDL::Log("Server shutting down.");
	SDL::NET::Quit();
	return 0;
}
