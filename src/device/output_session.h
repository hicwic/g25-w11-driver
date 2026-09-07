// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "protocol/logitech_protocol.h"

namespace g25 {
class ReportWriter {
public:
    virtual ~ReportWriter() = default;
    virtual void send(const Command& command) = 0;
};

// Construct before ANY write, so even a failed initial send triggers cleanup.
// finish() reports failures to the caller; destructor retries without throwing.
class OutputSession {
public:
    explicit OutputSession(ReportWriter& writer) noexcept : writer_(writer) {}
    ~OutputSession();
    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;
    void initialize();
    void finish();
private:
    ReportWriter& writer_;
    bool finished_{};
};
}
