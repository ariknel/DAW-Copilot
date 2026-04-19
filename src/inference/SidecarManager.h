#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <thread>
#include <memory>

namespace AIMC {

class SidecarManager : private juce::Timer
{
public:
    SidecarManager();
    ~SidecarManager() override;

    struct Config {
        juce::File  sidecarExecutable;
        juce::File  modelDirectory;
        int         port = 0;
    };

    bool         launch(const Config& cfg);   // call from background thread
    void         shutdown();                  // safe to call from any thread
    bool         isRunning() const;
    int          portInUse()  const noexcept { return m_port; }
    juce::String baseUrl()    const { return "http://127.0.0.1:" + juce::String(m_port); }
    juce::String lastError()  const { return m_lastError; }

    // Called on the message thread for each line of sidecar output
    std::function<void(const juce::String& line)> onLogLine;

private:
    void timerCallback() override;     // crash detection only - no stdout reads
    void drainStdout();                // runs on m_stdoutThread
    void dispatchLine(const juce::String& line);
    static int pickFreePort();

    // Cross-platform
    std::unique_ptr<juce::ChildProcess> m_proc;
    std::thread      m_stdoutThread;
    std::atomic<bool> m_stopFlag    { false };
    std::atomic<bool> m_readySeen   { false };
    std::atomic<bool> m_crashChecked{ false };
    int              m_port         = 0;
    juce::String     m_lastError;
    juce::String     m_sidecarPath;
    juce::int64      m_launchTimeMs = 0;

   #if JUCE_WINDOWS
    void* m_processHandle = nullptr;   // HANDLE to sidecar process
    void* m_stdoutRead    = nullptr;   // read end of stdout pipe
    void* m_jobHandle     = nullptr;   // Job Object for kill-on-close
   #endif
};

} // namespace AIMC
