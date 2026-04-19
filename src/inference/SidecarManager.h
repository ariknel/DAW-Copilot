#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <thread>
#include <memory>

namespace AIMC {

class SidecarManager
{
public:
    SidecarManager();
    ~SidecarManager();

    struct Config {
        juce::File  sidecarExecutable;
        juce::File  modelDirectory;
        int         port = 0;
    };

    bool         launch(const Config& cfg);
    void         shutdown();
    bool         isRunning() const;
    int          portInUse()  const noexcept { return m_port; }
    juce::String baseUrl()    const { return "http://127.0.0.1:" + juce::String(m_port); }
    juce::String lastError()  const { return m_lastError; }

    std::function<void(const juce::String& line)> onLogLine;

private:
    static int pickFreePort();
    void       readerThreadBody();

    juce::String  m_lastError;
    juce::String  m_sidecarPath;
    int           m_port        = 0;
    juce::int64   m_launchTimeMs = 0;

    // All HANDLE members are HANDLE on Windows, void* elsewhere for compilation
#if JUCE_WINDOWS
    void* m_hProcess  = nullptr;
    void* m_hJob      = nullptr;
    void* m_hStdoutRd = nullptr;   // read end of stdout pipe (our side)
    void* m_hStdoutWr = nullptr;   // write end of stdout pipe (child side, closed after spawn)
#endif

    std::thread       m_readerThread;
    std::atomic<bool> m_running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidecarManager)
};

} // namespace AIMC
