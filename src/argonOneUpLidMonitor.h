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

#pragma once

//-------------------------------------------------------------------------

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <bsd/libutil.h>

#include <gpiod.hpp>

//-------------------------------------------------------------------------

using pidFile_ptr = std::unique_ptr<pidfh, decltype(&pidfile_remove)>;

//-------------------------------------------------------------------------

class ArgonOneUpLidMonitor
{
public:

    enum class LidState
    {
        UNKNOWN,
        OPEN,
        CLOSED
    };

    explicit ArgonOneUpLidMonitor(std::atomic<bool>* run);

    void lidMonitor();
    void messageLog(int priority, std::string_view message) const;
    std::optional<int> parseCommandLine(int argc, char* argv[]);
    void perrorLog(std::string_view s) const;
    [[nodiscard]] std::string programName() const noexcept { return m_programName; }
    std::string toString(LidState state);

private:

    static LidState eventTypeToLidState(gpiod::edge_event::event_type eventType);
    static LidState valueTypeToLidState(gpiod::line::value valueType);
    std::string getHostname();
    std::chrono::seconds getShutdownTimeout();
    void printUsage(std::ostream& stream) const;

    std::string m_hostname{};
    std::string m_programName{};
    std::atomic<bool>* m_run{nullptr};
    std::string m_shutdownCommand{};
};

//-------------------------------------------------------------------------

