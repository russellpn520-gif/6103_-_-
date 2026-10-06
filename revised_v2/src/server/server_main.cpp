/**
 * @file server_main.cpp
 * @brief Server entry point: parses the command line and starts the server.
 */

#include "server/flight_server.h"
#include "common/cli_options.h"
#include "common/logger.h"
#include <iostream>
#include <cstdio>
#include <ctime>
#include <string>

namespace {

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --port=<port>        UDP port to listen on (default: 8080)\n"
              << "  --semantics=<s>      Invocation semantics (default: at-most-once)\n"
              << "                         at-most-once : filter duplicate requests and re-send the\n"
              << "                                        stored reply instead of executing again\n"
              << "                         at-least-once: execute every request received, duplicates included\n"
              << "  --loss-rate=<p>      Probability 0.0-1.0 that a reply is dropped (simulated reply loss)\n"
              << "  --seed=<n>           Seed for the loss simulation, to repeat an experiment\n"
              << "  --log-file=<file>    Also append the log to <file>\n"
              << "  --help               Show this help\n";
}

} // namespace

int main(int argc, char* argv[]) {
    long long port = 8080;
    Semantics semantics = Semantics::AT_MOST_ONCE;
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
        } else if (cli::matchOption(arg, "--port=", value)) {
            valid = cli::parseInteger(value, 1, 65535, port);
        } else if (cli::matchOption(arg, "--semantics=", value)) {
            // "maybe" only changes client behaviour (no retransmission), so the
            // server accepts the two semantics that differ on the server side.
            valid = parseSemantics(value, semantics) && semantics != Semantics::MAYBE;
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

    char loss_text[32];
    snprintf(loss_text, sizeof(loss_text), "%.2f", loss_rate);

    LOG_INFO("Starting flight server...");
    LOG_INFO(std::string("Invocation semantics: ") + getSemanticsName(semantics) +
             (semantics == Semantics::AT_MOST_ONCE
                  ? " (duplicate requests are answered from the reply history, never re-executed)"
                  : " (every request received is executed, duplicates included)"));
    LOG_INFO(std::string("Simulated reply loss rate: ") + loss_text + " (seed " + std::to_string(seed) + ")");
    if (!log_file.empty()) {
        LOG_INFO("Log file: " + log_file);
    }

    FlightServer server(static_cast<uint16_t>(port));
    server.setSemantics(semantics);
    server.setLossRate(loss_rate);
    server.setSeed(static_cast<uint32_t>(seed));

    return server.start() ? 0 : 1;
}
