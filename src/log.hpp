/*
Copyright (c) 2021, Sergei Ilinykh <rion4ik@gmail.com>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <functional>
#include <string>

#include <boost/log/trivial.hpp>

namespace restio {

class Log {
public:
    using LogHandler = std::function<void(boost::log::trivial::severity_level, std::string &&)>;
    inline void setHandler(LogHandler &&handler) { handler_ = std::move(handler); }

    inline boost::log::trivial::severity_level level() const { return level_; }

    void setLevel(boost::log::trivial::severity_level level) { level_ = level; }

    void log(boost::log::trivial::severity_level level, std::string &&message);

private:
    boost::log::trivial::severity_level level_ = boost::log::trivial::severity_level::debug;
    LogHandler                          handler_;
};

extern Log log;

#define RESTIO_LOG(log_level, logmsg)                                                                                  \
    do {                                                                                                               \
        if (log_level >= ::restio::log.level()) {                                                                      \
            std::stringstream log_stream;                                                                              \
            log_stream << logmsg;                                                                                      \
            ::restio::log.log(log_level, log_stream.str());                                                            \
        }                                                                                                              \
    } while (false)

#define RESTIO_INFO(logmsg) RESTIO_LOG(::boost::log::trivial::severity_level::info, logmsg)
#define RESTIO_WARN(logmsg) RESTIO_LOG(::boost::log::trivial::severity_level::warning, logmsg)
#define RESTIO_ERROR(logmsg) RESTIO_LOG(::boost::log::trivial::severity_level::error, logmsg)
#define RESTIO_DEBUG(logmsg) RESTIO_LOG(::boost::log::trivial::severity_level::debug, logmsg)
#define RESTIO_TRACE(logmsg) RESTIO_LOG(::boost::log::trivial::severity_level::trace, logmsg)

} // namespace restio
