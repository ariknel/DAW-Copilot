#include "SidecarManager.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace AIMC {

SidecarManager::SidecarManager() = default;

SidecarManager::~SidecarManager()
{
    shutdown();
}

int SidecarManager::pickFreePort()
{
    juce::Random r((juce::int64) juce::Time::getHighResolutionTicks());
    return 49152 + r.nextInt(16383);
}

bool SidecarManager::isRunning() const
{
#if JUCE_WINDOWS
    if (m_hProcess == nullptr) return false;
    DWORD code = STILL_ACTIVE;
    ::GetExitCodeProcess(static_cast<HANDLE>(m_hProcess), &code);
    return code == STILL_ACTIVE;
#else
    return m_running.load();
#endif
}

bool SidecarManager::launch(const Config& cfg)
{
    // Called from HttpSidecarBackend's background thread -- safe to block here.
    m_lastError.clear();
    m_launchTimeMs = juce::Time::currentTimeMillis();

    if (! cfg.sidecarExecutable.existsAsFile()) {
        m_lastError = "Sidecar not found:\n" + cfg.sidecarExecutable.getFullPathName()
                    + "\n\nReinstall the plugin.";
        if (onLogLine) onLogLine("[Sidecar] ERROR: " + m_lastError);
        return false;
    }

    cfg.modelDirectory.createDirectory();
    m_port = (cfg.port > 0) ? cfg.port : pickFreePort();
    m_sidecarPath = cfg.sidecarExecutable.getFullPathName();

    const auto workingDir = cfg.sidecarExecutable.getParentDirectory().getFullPathName();

    if (onLogLine) onLogLine("[Sidecar] Launching: " + m_sidecarPath
        + "  port=" + juce::String(m_port));

#if JUCE_WINDOWS
    // Create anonymous pipe for stdout/stderr
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (! ::CreatePipe(&hRead, &hWrite, &sa, 0)) {
        m_lastError = "CreatePipe failed (" + juce::String((int)::GetLastError()) + ")";
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        return false;
    }
    // Make read end non-inheritable
    ::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    juce::String cmdLine = "\"" + m_sidecarPath + "\""
        + " --port " + juce::String(m_port)
        + " --model-dir \"" + cfg.modelDirectory.getFullPathName() + "\"";

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.hStdInput   = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};

    // Use a mutable buffer for lpCommandLine (Windows requirement)
    std::vector<wchar_t> cmdBuf(cmdLine.toWideCharPointer(),
                                cmdLine.toWideCharPointer() + cmdLine.length() + 1);

    BOOL ok = ::CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr,
        TRUE,                    // inherit handles (pipe)
        CREATE_NO_WINDOW,        // no console window
        nullptr,
        workingDir.toWideCharPointer(),
        &si, &pi);

    // Close child's write end immediately - we only read
    ::CloseHandle(hWrite);

    if (! ok) {
        ::CloseHandle(hRead);
        m_lastError = "CreateProcess failed (error " + juce::String((int)::GetLastError()) + ").\n"
                    "Antivirus may be blocking sidecar.exe.";
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        return false;
    }

    ::CloseHandle(pi.hThread);
    m_hProcess  = pi.hProcess;
    m_hStdoutRd = hRead;

    // Attach to Job Object so sidecar dies if DAW crashes
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION ji = {};
        ji.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ji, sizeof(ji));
        ::AssignProcessToJobObject(job, pi.hProcess);
        m_hJob = job;
    }

    if (onLogLine) onLogLine("[Sidecar] Process spawned (PID "
        + juce::String((int)pi.dwProcessId) + "), reading stdout...");

    // Launch reader thread - reads pipe blocking, posts lines via callAsync
    m_running = true;
    m_readerThread = std::thread([this]() { readerThreadBody(); });
    return true;

#else
    // Non-Windows stub
    m_lastError = "SidecarManager only implemented for Windows.";
    return false;
#endif
}

void SidecarManager::readerThreadBody()
{
#if JUCE_WINDOWS
    char buf[1024];
    juce::String lineBuffer;

    while (true) {
        DWORD bytesRead = 0;
        BOOL ok = ::ReadFile(static_cast<HANDLE>(m_hStdoutRd),
                             buf, sizeof(buf) - 1, &bytesRead, nullptr);
        if (! ok || bytesRead == 0) break;   // pipe closed or process exited

        buf[bytesRead] = '\0';
        lineBuffer += juce::String(buf);

        int nl;
        while ((nl = lineBuffer.indexOfChar('\n')) >= 0) {
            auto line = lineBuffer.substring(0, nl).trim();
            lineBuffer = lineBuffer.substring(nl + 1);
            if (line.isNotEmpty()) {
                // Post to message thread -- never block message thread from here
                juce::MessageManager::callAsync([this, line]() {
                    if (onLogLine) onLogLine(line);
                });
            }
        }
    }

    // Pipe closed -- process exited
    const auto elapsed = juce::Time::currentTimeMillis() - m_launchTimeMs;
    DWORD exitCode = 1;
    if (m_hProcess) ::GetExitCodeProcess(static_cast<HANDLE>(m_hProcess), &exitCode);

    juce::String msg = "[Sidecar] Process exited after " + juce::String(elapsed)
                     + "ms, code=" + juce::String((int)exitCode);
    juce::MessageManager::callAsync([this, msg]() {
        if (onLogLine) onLogLine(msg);
    });
#endif

    m_running = false;
}

void SidecarManager::shutdown()
{
    m_running = false;

#if JUCE_WINDOWS
    if (m_hProcess != nullptr) {
        ::TerminateProcess(static_cast<HANDLE>(m_hProcess), 1);
        ::WaitForSingleObject(static_cast<HANDLE>(m_hProcess), 2000);
        ::CloseHandle(static_cast<HANDLE>(m_hProcess));
        m_hProcess = nullptr;
    }
    if (m_hStdoutRd != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_hStdoutRd));
        m_hStdoutRd = nullptr;
    }
    if (m_hJob != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_hJob));
        m_hJob = nullptr;
    }
#endif

    if (m_readerThread.joinable())
        m_readerThread.join();
}

} // namespace AIMC
