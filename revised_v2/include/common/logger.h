/**
 * @file logger.h
 * @brief Minimal thread-safe logger (header-only).
 *
 * Each line is written as "[YYYY-MM-DD HH:MM:SS] [LEVEL] message" to stdout
 * and, if enabled with --log-file=<file>, appended to a log file as well.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <mutex>

/**
 * @brief Log levels in increasing order of severity.
 */
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3
};

/**
 * @brief Process-wide logger (singleton).
 */
class Logger {
public:
    /**
     * @brief Access the single logger instance.
     */
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    /**
     * @brief Suppress messages below this level (default: LOG_INFO).
     */
    void setLevel(LogLevel level) {
        level_ = level;
    }

    /**
     * @brief Additionally append every log line to the given file.
     */
    void enableFileOutput(const std::string& filename) {
        file_output_ = true;
        log_file_.open(filename, std::ios::app);
    }

    /**
     * @brief Stop writing to the log file.
     */
    void disableFileOutput() {
        file_output_ = false;
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }

    void debug(const std::string& message) {
        log(LOG_DEBUG, "DEBUG", message);
    }

    void info(const std::string& message) {
        log(LOG_INFO, "INFO", message);
    }

    void warning(const std::string& message) {
        log(LOG_WARNING, "WARN", message);
    }

    void error(const std::string& message) {
        log(LOG_ERROR, "ERROR", message);
    }

private:
    Logger() : level_(LOG_INFO), file_output_(false) {}
    ~Logger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Prefix the message with a timestamp and level and write it out.
     */
    void log(LogLevel level, const std::string& level_str, const std::string& message) {
        if (level < level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        time_t now = time(nullptr);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        std::stringstream ss;
        ss << "[" << time_str << "] [" << level_str << "] " << message;
        std::string log_message = ss.str();

        std::cout << log_message << std::endl;

        if (file_output_ && log_file_.is_open()) {
            log_file_ << log_message << std::endl;
        }
    }

    LogLevel level_;
    bool file_output_;
    std::ofstream log_file_;
    std::mutex mutex_;
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARNING(msg) Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)

#endif // LOGGER_H
