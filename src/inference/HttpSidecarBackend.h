#pragma once
#include "InferenceBackend.h"
#include "SidecarManager.h"
#include <juce_events/juce_events.h>
#include <thread>
#include <atomic>

namespace AIMC {

class HttpSidecarBackend : public InferenceBackend,
                           private juce::Timer
{
public:
    HttpSidecarBackend(juce::File sidecarExe, juce::File modelDir);
    ~HttpSidecarBackend() override;

    void start(StatusCallback onStatus) override;
    void stop() override;
    void generate(Request req, ProgressCallback onProgress, DoneCallback onDone) override;
    Status currentStatus() const override { return m_status.load(); }

private:
    void timerCallback() override;                // health-check polling
    void dispatchHealthCheck();
    void updateStatus(Status s, juce::String msg, float progress = 0.f);

    juce::File                  m_sidecarExe;
    juce::File                  m_modelDir;
    juce::File                  m_logFile;
    std::unique_ptr<SidecarManager> m_sidecar;
    std::atomic<Status>         m_status { Status::NotStarted };
    StatusCallback              m_statusCb;
    std::atomic<int>            m_sidecarPort { 0 };  // set atomically after launch, read from any thread

    juce::String baseUrl() const {
        return "http://127.0.0.1:" + juce::String(m_sidecarPort.load());
    }

    // Single in-flight request thread (MIDI-LLM inference isn't thread-safe anyway).
    std::unique_ptr<std::thread> m_inflight;
    std::atomic<bool>            m_busy { false };
};

} // namespace AIMC
