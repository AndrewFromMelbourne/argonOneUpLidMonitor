//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2026 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#include <errno.h>
#include <getopt.h>
#include <syslog.h>
#include <unistd.h>

#include <systemd/sd-journal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <print>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>

#include "argonOneUpLidMonitor.h"
#include "config.h"

//-------------------------------------------------------------------------

using namespace std::chrono_literals;

//=========================================================================

namespace
{

//-------------------------------------------------------------------------

std::string_view
trim(
    const std::string_view str)
{
    auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };

    const auto start = std::find_if_not(begin(str), end(str), isSpace);
    if (start == str.end())
    {
        return {};
    }

    const auto end = std::find_if_not(rbegin(str), rend(str), isSpace);
    return str.substr(start - str.begin(), end.base() - start);
}

//-------------------------------------------------------------------------

} // namespace

//=========================================================================

ArgonOneUpLidMonitor::ArgonOneUpLidMonitor(
    std::atomic<bool>* run)
:
    m_hostname(getHostname()),
    m_programName(),
    m_run(run),
    m_shutdownCommand("shutdown -h now")
{
}

//-------------------------------------------------------------------------

ArgonOneUpLidMonitor::LidState
ArgonOneUpLidMonitor::eventTypeToLidState(
    gpiod::edge_event::event_type eventType)
{
    switch (eventType)
    {
        case gpiod::edge_event::event_type::RISING_EDGE:
            return LidState::OPEN;
        case gpiod::edge_event::event_type::FALLING_EDGE:
            return LidState::CLOSED;
        default:
            return LidState::UNKNOWN;
    }
}

//-------------------------------------------------------------------------

ArgonOneUpLidMonitor::LidState
ArgonOneUpLidMonitor::valueTypeToLidState(
    gpiod::line::value valueType)
{
    switch (valueType)
    {
        case gpiod::line::value::ACTIVE:
            return LidState::OPEN;
        case gpiod::line::value::INACTIVE:
            return LidState::CLOSED;
        default:
            return LidState::UNKNOWN;
    }
}

//-------------------------------------------------------------------------

std::string
ArgonOneUpLidMonitor::getHostname()
{
    char hostname[256];
    if (::gethostname(hostname, sizeof(hostname)) == 0)
    {
        return hostname;
    }
    else
    {
        perrorLog("cannot get hostname");
        return "localhost";
    }
}

//-------------------------------------------------------------------------

std::chrono::seconds
ArgonOneUpLidMonitor::getShutdownTimeout()
{
    std::filesystem::path config("/etc/argononeupd.conf");

    if (std::filesystem::exists(config))
    {
        std::ifstream ifs(config);
        if (ifs.is_open())
        {
            std::string line;
            while (std::getline(ifs, line))
            {
                line = trim(line);

                if (line.starts_with("#") || line.empty())
                {
                    continue;
                }

                const std::regex pattern{R"(\s*lidshutdownsecs\s*=\s*(\d+))"};
                std::smatch matches;
                if (std::regex_search(line, matches, pattern))
                {
                    try
                    {
                        auto timeout = std::stoul(matches[1].str());
                        auto timeoutSeconds = std::chrono::seconds(timeout);

                        messageLog(
                            LOG_INFO,
                            std::format(
                                "shutdown timeout set to {:%M:%S} minutes:seconds",
                                timeoutSeconds));

                        return timeoutSeconds;
                    }
                    catch (const std::exception& e)
                    {
                        messageLog(
                            LOG_ERR,
                            std::format("cannot parse shutdown_timeout: {}", e.what()));
                        return 0s;
                    }
                }
            }
        }
    }

    return 0s;
}

//-------------------------------------------------------------------------

void
ArgonOneUpLidMonitor::lidMonitor()
{
    messageLog(
        LOG_INFO,
        std::format("starting - shutdown command is \"{}\"", m_shutdownCommand));

    auto shutdownTimeout = getShutdownTimeout();
    std::chrono::steady_clock::time_point lidClosedTime;
    LidState state = LidState::UNKNOWN;

    const std::filesystem::path chipPath{"/dev/gpiochip4"};
    const gpiod::line::offset lineOffset{27};

    gpiod::chip chip(chipPath);

    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::INPUT);
    settings.set_edge_detection(gpiod::line::edge::BOTH);
    settings.set_bias(gpiod::line::bias::PULL_UP);

    auto request = chip.prepare_request();
    request.add_line_settings(lineOffset, settings);

    auto lineConfig = request.do_request();
    const auto value = lineConfig.get_value(lineOffset);
state = valueTypeToLidState(value);

    messageLog(LOG_INFO, std::format("lid {}", toString(state)));

    if (state == LidState::CLOSED and shutdownTimeout > 0s)
    {
        lidClosedTime = std::chrono::steady_clock::now();
    }


    while (*m_run)
    {
        if (lineConfig.wait_edge_events(1s))
        {
            gpiod::edge_event_buffer buffer{1};
            lineConfig.read_edge_events(buffer, 1);
            for (const auto& event : buffer)
            {
                state = eventTypeToLidState(event.type());

                messageLog(
                    LOG_INFO,
                    std::format("lid {}", toString(state)));

                if (state == LidState::CLOSED)
                {
                    shutdownTimeout = getShutdownTimeout();
                    lidClosedTime = std::chrono::steady_clock::now();
                }
            }
        }
        else if (state == LidState::CLOSED and shutdownTimeout > 0s)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lidClosedTime >= shutdownTimeout)
            {
                messageLog(
                    LOG_INFO,
                    std::format(
                        "lid has been closed for {} seconds, shutting down",
                        shutdownTimeout.count()));

                ::system(m_shutdownCommand.c_str());
                break;
            }
        }
    }
}

//-------------------------------------------------------------------------

void
ArgonOneUpLidMonitor::messageLog(
    int priority,
    std::string_view message) const
{
    if (getenv("JOURNAL_STREAM") != nullptr)
    {
        std::string messageStr(message);
        sd_journal_print(priority, "%s", messageStr.c_str());
    }
    else
    {
        const auto now = floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const auto localTime = std::chrono::current_zone()->to_local(now);

        std::print(std::cerr, "{:%b %e %T} ", localTime);
        std::print(std::cerr, "{} ", m_hostname);
        std::print(std::cerr, "{}[{}]:", m_programName, getpid());

        const static std::map<int, std::string> priorityMap
        {
            { LOG_EMERG, "emergency" },
            { LOG_ALERT, "alert" },
            { LOG_CRIT, "critical" },
            { LOG_ERR, "error" },
            { LOG_WARNING, "warning" },
            { LOG_NOTICE, "notice" },
            { LOG_INFO, "info" },
            { LOG_DEBUG, "debug" }
        };

        if (const auto it = priorityMap.find(priority); it != priorityMap.end())
        {
            std::print(std::cerr, "{}", it->second);
        }
        else
        {
            std::print(std::cerr, "unknown({})", priority);
        }

        std::println(std::cerr, ":{}", message);
    }
}

//-------------------------------------------------------------------------

std::optional<int>
ArgonOneUpLidMonitor::parseCommandLine(
    int argc,
    char* argv[])
{
    m_programName = std::filesystem::path(argv[0]).filename().string();

    static const char* sopts = "hs:";
    static option lopts[] =
    {
        { "help", no_argument, nullptr, 'h' },
        { "shutdownCommand", required_argument, nullptr, 's' },
        { nullptr, no_argument, nullptr, 0 }
    };

    int opt{0};

    while ((opt = ::getopt_long(argc, argv, sopts, lopts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'h':

            printUsage(std::cout);
            return EXIT_SUCCESS;
            break;

        case 's':

            m_shutdownCommand = optarg;
            break;

        default:

            printUsage(std::cerr);
            return EXIT_FAILURE;
            break;
        }
    }

    return std::nullopt;
}

//-------------------------------------------------------------------------

void
ArgonOneUpLidMonitor::perrorLog(
    std::string_view s) const
{
    if (getenv("JOURNAL_STREAM") != nullptr)
    {
        sd_journal_perror(std::string(s).c_str());
    }
    else
    {
        messageLog(LOG_ERR, std::string(s) + " - " + ::strerror(errno));
    }
}

//-------------------------------------------------------------------------

void
ArgonOneUpLidMonitor::printUsage(
    std::ostream& stream) const
{
    std::println(stream, "");
    std::println(stream, "Usage: {}", m_programName);
    std::println(stream, "");
    std::println(stream, "    --help,-h - print usage and exit");
    std::println(stream, "    --shutdownCommand,-s <command> - command to execute when lid has been closed for the configured number of seconds (default: \"{}\")", m_shutdownCommand);
    std::println(stream, "");
    std::println(stream, "Version: {}", c_projectVersion);
    std::println(stream, "Git commit hash: {}", c_gitCommitHash);
    std::println(stream, "");
}

//-------------------------------------------------------------------------

std::string
ArgonOneUpLidMonitor::toString(
    LidState state)
{
    switch (state)
    {
        case LidState::OPEN:
            return "open";
        case LidState::CLOSED:
            return "closed";
        default:
            return "unknown";
    }
}

