/**
 * @file client_main.cpp
 * @brief Client entry point: parses the command line and runs the text menu.
 */

#include "client/flight_client.h"
#include "client/client_ui.h"
#include "common/cli_options.h"
#include "common/logger.h"
#include <iostream>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

namespace {

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --server=<ip>        Server IPv4 address (default: 127.0.0.1)\n"
              << "  --port=<port>        Server port (default: 8080)\n"
              << "  --semantics=<s>      Invocation semantics (default: at-most-once); use the same\n"
              << "                       value as the server:\n"
              << "                         at-most-once / at-least-once: retransmit after a timeout\n"
              << "                         maybe: send each request once (baseline, no retransmission)\n"
              << "  --timeout-ms=<ms>    Time to wait for a reply before retransmitting (default: 2000)\n"
              << "  --retries=<n>        Retransmissions after the first attempt (default: 5)\n"
              << "  --loss-rate=<p>      Probability 0.0-1.0 that a request is dropped (simulated request loss)\n"
              << "  --seed=<n>           Seed for the loss simulation, to repeat an experiment\n"
              << "  --log-file=<file>    Also append the log to <file>\n"
              << "  --help               Show this help\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string server_ip = "127.0.0.1";
    long long port = 8080;
    Semantics semantics = Semantics::AT_MOST_ONCE;
    long long timeout_ms = 2000;
    long long retries = 5;
    double loss_rate = 0.0;
    long long seed = static_cast<long long>(time(nullptr) & 0xffffffff);
    std::string log_file;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        std::string value;
        bool valid = true;

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (cli::matchOption(arg, "--server=", value)) {
            server_ip = value;
            valid = !value.empty();
        } else if (cli::matchOption(arg, "--port=", value)) {
            valid = cli::parseInteger(value, 1, 65535, port);
        } else if (cli::matchOption(arg, "--semantics=", value)) {
            valid = parseSemantics(value, semantics);
        } else if (cli::matchOption(arg, "--timeout-ms=", value)) {
            valid = cli::parseInteger(value, 10, 60000, timeout_ms);
        } else if (cli::matchOption(arg, "--retries=", value)) {
            valid = cli::parseInteger(value, 0, 100, retries);
        } else if (cli::matchOption(arg, "--loss-rate=", value)) {
            valid = cli::parseReal(value, 0.0, 1.0, loss_rate);
        } else if (cli::matchOption(arg, "--seed=", value)) {
            valid = cli::parseInteger(value, 0, 4294967295LL, seed);
        } else if (cli::matchOption(arg, "--log-file=", value)) {
            log_file = value;
            valid = !value.empty();
        } else {
            valid = false;
        }

        if (!valid) {
            std::cerr << "Invalid option: " << arg << "\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (!log_file.empty()) {
        Logger::getInstance().enableFileOutput(log_file);
    }

    auto client = std::make_shared<FlightClient>(server_ip, static_cast<uint16_t>(port));
    if (!client->isValid()) {
        LOG_ERROR("Could not set up the client (check the --server address)");
        return 1;
    }
    client->setSemantics(semantics);
    client->setTimeoutMs(static_cast<int>(timeout_ms));
    client->setMaxRetries(static_cast<int>(retries));
    client->setLossRate(loss_rate);
    client->setSeed(static_cast<uint32_t>(seed));

    char loss_text[32];
    snprintf(loss_text, sizeof(loss_text), "%.2f", loss_rate);

    LOG_INFO("Starting flight client (local UDP port " + std::to_string(client->localPort()) + ")");
    LOG_INFO("Server: " + server_ip + ":" + std::to_string(port));
    if (semantics == Semantics::MAYBE) {
        LOG_INFO("Invocation semantics: maybe (each request is sent once, no retransmission)");
    } else {
        LOG_INFO(std::string("Invocation semantics: ") + getSemanticsName(semantics) +
                 " (retransmit after " + std::to_string(timeout_ms) + " ms, up to " +
                 std::to_string(retries) + " times; start the server with the same --semantics)");
    }
    LOG_INFO(std::string("Simulated request loss rate: ") + loss_text + " (seed " + std::to_string(seed) + ")");
    if (!log_file.empty()) {
        LOG_INFO("Log file: " + log_file);
    }

    ClientUI ui(client);
    ui.run();

    return 0;
}
