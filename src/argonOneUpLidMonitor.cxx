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
#include <fcntl.h>
#include <getopt.h>
#include <syslog.h>
#include <unistd.h>

#include <bsd/libutil.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>

#include <gpiod.hpp>

//-------------------------------------------------------------------------

using namespace std::chrono_literals;
using pidFile_ptr = std::unique_ptr<pidfh, decltype(&pidfile_remove)>;

//-------------------------------------------------------------------------

namespace
{
static std::string s_hostname{"localhost"};
static bool s_isDaemon{false};
static std::string s_pidFile{};
static std::string s_programName{};
volatile static std::sig_atomic_t s_run{1};
static std::string s_shutdownCommand{"shutdown -h now"};
}

//-------------------------------------------------------------------------

enum class LidAction
{
    UNKNOWN,
    OPENED,
    CLOSED
};

//-------------------------------------------------------------------------

void
messageLog(
    int priority,
    const std::string& message)
{
    if (s_isDaemon)
    {
        ::syslog(LOG_MAKEPRI(LOG_USER, priority), "%s", message.c_str());
    }
    else
    {
        const auto now = floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const auto localTime = std::chrono::current_zone()->to_local(now);

        std::print(std::cerr, "{:%b %e %T} ", localTime);
        std::print(std::cerr, "{} ", s_hostname);
        std::print(std::cerr, "{}[{}]:", s_programName, getpid());

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

LidAction
eventTypeToLidAction(
    gpiod::edge_event::event_type eventType)
{
    switch (eventType)
    {
        case gpiod::edge_event::event_type::RISING_EDGE:
            return LidAction::OPENED;
        case gpiod::edge_event::event_type::FALLING_EDGE:
            return LidAction::CLOSED;
        default:
            return LidAction::UNKNOWN;
    }
}

//-------------------------------------------------------------------------

std::string
toString(
    LidAction action)
{
    switch (action)
    {
        case LidAction::OPENED:
            return "opened";
        case LidAction::CLOSED:
            return "closed";
        default:
            return "unknown";
    }
}

//-------------------------------------------------------------------------

void
perrorLog(
    const std::string& s)
{
    messageLog(LOG_ERR, s + " - " + ::strerror(errno));
}

//-------------------------------------------------------------------------

void
printUsage(
    std::ostream& stream)
{
    std::println(stream, "");
    std::println(stream, "Usage: {}", s_programName);
    std::println(stream, "");
    std::println(stream, "    --daemon,-d - start in the background as a daemon");
    std::println(stream, "    --help,-h - print usage and exit");
    std::println(stream, "    --pidfile,-p <pidfile> - create and lock PID file");
    std::println(stream, "    --shutdownCommand,-s <command> - command to execute when lid has been closed for the configured number of seconds (default: \"{}\")", s_shutdownCommand);
    std::println(stream, "");
}

//-------------------------------------------------------------------------

void
parseCommandLine(
    int argc,
    char* argv[])
{
    static const char* sopts = "dhp:s:";
    static option lopts[] =
    {
        { "daemon", no_argument, nullptr, 'd' },
        { "help", no_argument, nullptr, 'h' },
        { "pidfile", required_argument, nullptr, 'p' },
        { "shutdownCommand", required_argument, nullptr, 's' },
        { nullptr, no_argument, nullptr, 0 }
    };

    int opt{0};

    while ((opt = ::getopt_long(argc, argv, sopts, lopts, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'd':

            s_isDaemon = true;
            break;

        case 'h':

            printUsage(std::cout);
            ::exit(EXIT_SUCCESS);
            break;

        case 'p':

            s_pidFile = optarg;
            break;

        case 's':

            s_shutdownCommand = optarg;
            break;

        default:

            printUsage(std::cerr);
            ::exit(EXIT_FAILURE);
            break;
        }
    }
}

//-------------------------------------------------------------------------

std::string
getHostname()
{
    char hostname[256];
    if (::gethostname(hostname, sizeof(hostname)) == 0)
    {
        return hostname;
    }
    else
    {
        perrorLog("Error getting hostname");
        return "localhost";
    }
}

//-------------------------------------------------------------------------

static void
signalHandler(
    int signalNumber) noexcept
{
    switch (signalNumber)
    {
    case SIGINT:
    case SIGTERM:

        s_run = 0;
        break;
    };
}

//-------------------------------------------------------------------------

void
setSignalHandler()
{
    for (auto signal : { SIGINT, SIGTERM })
    {
        if (std::signal(signal, signalHandler) == SIG_ERR)
        {
            messageLog(
                LOG_ERR,
                std::format(
                    "Error: installing {} signal handler : {}",
                    strsignal(signal),
                    strerror(errno)));

            ::exit(EXIT_FAILURE);
        }
    }
}

//-------------------------------------------------------------------------

pidFile_ptr
daemonize()
{
    pidFile_ptr pfh{nullptr, &pidfile_remove};

    if (not s_pidFile.empty())
    {
        pid_t otherpid;
        pfh.reset(::pidfile_open(s_pidFile.c_str(), 0600, &otherpid));

        if (not pfh)
        {
            messageLog(
                LOG_ERR,
                std::format(
                    "{} is already running with pid {}",
                    s_programName,
                    otherpid));
            ::exit(EXIT_FAILURE);
        }
    }

    if (::daemon(0, 0) == -1)
    {
        messageLog(LOG_ERR, "Cannot daemonize");
        ::exit(EXIT_FAILURE);
    }

    if (pfh)
    {
        ::pidfile_write(pfh.get());
    }

    return pfh;
}

//-------------------------------------------------------------------------

std::string_view
trim(const std::string_view str)
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

std::chrono::seconds
getShutdownTimeout()
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
                            std::format("Error parsing shutdown_timeout: {}", e.what()));
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
lidMonitor()
{
    auto shutdownTimeout = getShutdownTimeout();
    std::chrono::steady_clock::time_point lidClosedTime;
    LidAction action = LidAction::UNKNOWN;

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

    while (s_run)
    {
        if (lineConfig.wait_edge_events(1s))
        {
            gpiod::edge_event_buffer buffer{1};
            lineConfig.read_edge_events(buffer, 1);
            for (const auto& event : buffer)
            {
                action = eventTypeToLidAction(event.type());

                messageLog(
                    LOG_INFO,
                    std::format("lid {}", toString(action)));

                if (action == LidAction::CLOSED)
                {
                    shutdownTimeout = getShutdownTimeout();
                    lidClosedTime = std::chrono::steady_clock::now();
                }
            }
        }
        else if (action == LidAction::CLOSED and shutdownTimeout > 0s)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lidClosedTime >= shutdownTimeout)
            {
                messageLog(
                    LOG_INFO,
                    std::format(
                        "lid has been closed for {} seconds, shutting down",
                        shutdownTimeout.count()));

                ::system(s_shutdownCommand.c_str());
                break;
            }
        }
    }
}

//-------------------------------------------------------------------------

int
main(
    int argc,
    char *argv[])
{
    s_hostname = getHostname();
    s_programName = std::filesystem::path(argv[0]).filename().string();

    //---------------------------------------------------------------------

    parseCommandLine(argc, argv);

    //---------------------------------------------------------------------

    pidFile_ptr pfh{nullptr, &pidfile_remove};

    if (s_isDaemon)
    {
        ::openlog(s_programName.c_str(), LOG_PID, LOG_USER);
        pfh = daemonize();
    }

    //---------------------------------------------------------------------

    setSignalHandler();

    //---------------------------------------------------------------------

    messageLog(
        LOG_INFO,
        std::format("starting - shutdown command is \"{}\"", s_shutdownCommand));

    try
    {
        lidMonitor();
    }
    catch(const std::exception& e)
    {
        messageLog(LOG_ERR, std::format("Error:{}", e.what()));
    }

    messageLog(LOG_INFO, "exiting");
}

