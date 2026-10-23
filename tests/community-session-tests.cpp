#include "community/orqa_session.hpp"
#include "community/orqa_workers.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    const std::string fixture = "synthetic-session-fixture-no-real-account";
    auto encrypted = fixture;
    if (!obn::orqa::protect_session(encrypted) || encrypted.find(fixture) != std::string::npos)
        throw std::runtime_error("Session was not protected");
    auto decoded = encrypted;
    if (!obn::orqa::unprotect_session(decoded) || decoded != fixture)
        throw std::runtime_error("Protected session did not round trip");
    encrypted[encrypted.size() / 2] ^= 0x5a;
    if (obn::orqa::unprotect_session(encrypted)) throw std::runtime_error("Corrupt session was accepted");
    decoded = fixture;
    if (obn::orqa::unprotect_session(decoded)) throw std::runtime_error("Unprotected session was accepted");
    obn::orqa::BackgroundTasks tasks;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> completed{0};
    if (!tasks.start([&] { entered.set_value(); gate.wait(); ++completed; })) throw std::runtime_error("Worker did not start");
    entered.get_future().wait();
    auto closing = std::async(std::launch::async, [&] { tasks.close(); });
    if (closing.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready)
        throw std::runtime_error("Shutdown abandoned an active worker");
    release.set_value();
    closing.get();
    if (completed != 1 || tasks.start([] {})) throw std::runtime_error("Worker survived shutdown or restarted");
    std::cout << "Synthetic Windows session encryption, corruption and plaintext-rejection checks passed.\n";
}
