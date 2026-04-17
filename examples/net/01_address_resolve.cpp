/**
 * @file 01_address_resolve.cpp
 * @brief Demonstrate asynchronous hostname resolution with SDL3pp_net.
 *
 * This example resolves several hostnames and prints their IP addresses.
 * It shows both blocking (WaitUntilResolved) and polling (GetStatus) approaches.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <string_view>
#include <vector>

int main(int /*argc*/, char* /*argv*/[])
{
	// Initialise SDL_net
	SDL::NET::Init();

	SDL::Log(std::format("SDL_net version: {}", SDL::NET::Version()));

	// ── 1. Blocking resolution ────────────────────────────────────────────────
	SDL::Log("\n--- Blocking resolution ---");

	const std::vector<std::string_view> hosts = {
		"libsdl.org", "8.8.8.8", "invalid.host.that.does.not.exist"
	};

	for (auto& host : hosts) {
		SDL::Address addr{host.data()};

		SDL::Status status = addr.WaitUntilResolved(5000); // 5 s timeout

		if (status == SDL::NET_SUCCESS_STATUS) {
			SDL::Log(std::format("  {:->40} -> {}", host.data(), addr.GetString()));
		} else if (status == SDL::NET_FAILURE_STATUS) {
			SDL::Log(std::format("  {:->40} -> FAILED: {}", host.data(), SDL_GetError()));
		} else {
			SDL::Log(std::format("  {:->40} -> TIMEOUT (still waiting)", host.data()));
		}
	}

	// ── 2. Non-blocking polling ───────────────────────────────────────────────
	SDL::Log("\n--- Non-blocking polling ---");

	SDL::Address pollAddr{"www.libsdl.org"};

	int tries = 0;
	while (pollAddr.GetStatus() == SDL::NET_WAITING_STATUS && tries < 50) {
		SDL_Delay(100); // wait 100 ms between polls
		++tries;
	}

	if (pollAddr.GetStatus() == SDL::NET_SUCCESS_STATUS) {
		SDL::Log(std::format("  www.libsdl.org resolved in %d polls -> {}",
				 tries, pollAddr.GetString()));
	} else {
		SDL::Log("  www.libsdl.org failed to resolve");
	}

	// ── 3. Address copy (ref-counted) ─────────────────────────────────────────
	SDL::Log("\n--- Address copy (ref-counting) ---");
	{
		SDL::Address original = SDL::ResolveHostname("8.8.4.4");
		original.WaitUntilResolved(-1);

		SDL::Address copy = original; // increments ref-count
		SDL::Log(std::format("  Original: {}", original.GetString()));
		SDL::Log(std::format("  Copy    : {}", copy.GetString()));
	} // both addresses go out of scope here, last unref frees memory

	// ── 4. Local addresses ───────────────────────────────────────────────────
	SDL::Log("\n--- Local addresses ---");
	{
		int count = 0;
		SDL::AddressRaw* local = SDL::GetLocalAddresses(&count);
		SDL::Log(std::format("  Found {} local address(es):", count));
		for (int i = 0; i < count; ++i) {
			// Wait for each to resolve (they usually resolve immediately)
			NET_WaitUntilResolved(local[i], 1000);
			SDL::Log(std::format("    [{}] {}", i, NET_GetAddressString(local[i])));
		}
		SDL::FreeLocalAddresses(local);
	}

	// ── 5. Simulate resolution loss ──────────────────────────────────────────
	SDL::Log("\n--- Simulated 100%% resolution failure ---");
	SDL::Address::SimulateResolutionLoss(100);
	{
		SDL::Address bad{"google.com"};
		SDL::Status s = bad.WaitUntilResolved(1000);
		SDL::Log(std::format("  google.com with 100% loss -> {}",
				 s == SDL::NET_FAILURE_STATUS ? "FAILED (expected)" : "unexpected success"));
	}
	SDL::Address::SimulateResolutionLoss(0); // reset

	SDL::NET::Quit();
	SDL::Log("\nDone.");
	return 0;
}
