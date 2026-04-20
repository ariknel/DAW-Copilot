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
    return 49152 + r.nextInt(65535 - 49152);
}

bool SidecarManager::launch(const Config& cfg)
{
    // Called from a background thread - all blocking I/O is fine here.
    m_lastError.clear();
    m_launchTimeMs = juce::Time::currentTimeMillis();

    if (! cfg.sidecarExecutable.existsAsFile()) {
        m_lastError = "Sidecar not found at:\n" + cfg.sidecarExecutable.getFullPathName();
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        return false;
    }

    cfg.modelDirectory.createDirectory();
    m_sidecarPath = cfg.sidecarExecutable.getFullPathName();
    m_port = (cfg.port > 0) ? cfg.port : pickFreePort();

    auto workingDir = cfg.sidecarExecutable.getParentDirectory();

    if (onLogLine) {
        onLogLine("[Sidecar] Launching: " + m_sidecarPath);
        onLogLine("[Sidecar] Port: " + juce::String(m_port));
        onLogLine("[Sidecar] Model dir: " + cfg.modelDirectory.getFullPathName());
    }

   #if JUCE_WINDOWS
    // Use CreateProcess directly so we can:
    // 1. Set working directory without mutating global CWD
    // 2. Get a real HANDLE for Job Object assignment
    // 3. Set up stdout/stderr pipes ourselves

    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    if (! ::CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        m_lastError = "Failed to create stdout pipe.";
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        return false;
    }
    ::SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.hStdOutput  = hStdoutWrite;
    si.hStdError   = hStdoutWrite;
    si.dwFlags     = STARTF_USESTDHANDLES;

    juce::String cmdLine;
   #if JUCE_WINDOWS
    // .cmd files require cmd.exe to run - can't CreateProcess a batch file directly
    auto ext = cfg.sidecarExecutable.getFileExtension().toLowerCase();
    if (ext == ".cmd" || ext == ".bat") {
        cmdLine = "cmd.exe /c \"\"" + m_sidecarPath + "\""
            + " --port " + juce::String(m_port)
            + " --model-dir \"" + cfg.modelDirectory.getFullPathName() + "\"\"";
    } else {
        cmdLine = "\"" + m_sidecarPath + "\""
            + " --port " + juce::String(m_port)
            + " --model-dir \"" + cfg.modelDirectory.getFullPathName() + "\"";
    }
   #else
    cmdLine = "\"" + m_sidecarPath + "\""
        + " --port " + juce::String(m_port)
        + " --model-dir \"" + cfg.modelDirectory.getFullPathName() + "\"";
   #endif

    PROCESS_INFORMATION pi = {};
    auto cmdW = cmdLine.toWideCharPointer();
    auto cwdW = workingDir.getFullPathName().toWideCharPointer();

    BOOL created = ::CreateProcessW(
        nullptr, const_cast<LPWSTR>(cmdW),
        nullptr, nullptr, TRUE,   // bInheritHandles = TRUE for pipe
        CREATE_NO_WINDOW,
        nullptr, cwdW, &si, &pi);

    // Close the write end in our process immediately - the child owns it now
    ::CloseHandle(hStdoutWrite);

    if (! created) {
        ::CloseHandle(hStdoutRead);
        m_lastError = "CreateProcess failed (error " + juce::String((int)::GetLastError()) + ").\n"
                    "Antivirus may be blocking sidecar.exe.";
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        return false;
    }

    ::CloseHandle(pi.hThread);
    m_processHandle = pi.hProcess;
    m_stdoutRead    = hStdoutRead;

    // Create Job Object and assign process so it dies when plugin unloads
    auto job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        ::AssignProcessToJobObject(job, pi.hProcess);
        m_jobHandle = job;
    }

    if (onLogLine) onLogLine("[Sidecar] Process spawned (PID "
        + juce::String((int)pi.dwProcessId) + ")");

    m_readySeen    = false;
    m_crashChecked = false;

    // Drain stdout on a dedicated thread - never on the message thread.
    // readProcessOutput busy-loops on Windows when no output is available,
    // which would peg CPU and starve the DAW's message thread.
    m_stdoutThread = std::thread([this]() { drainStdout(); });

    // Use a lightweight timer just for crash detection (no stdout reads)
    juce::MessageManager::callAsync([this]() { startTimer(500); });
    return true;

   #else
    // Non-Windows: use JUCE ChildProcess (stdout drained on bg thread too)
    juce::StringArray args;
    args.add(m_sidecarPath);
    args.add("--port"); args.add(juce::String(m_port));
    args.add("--model-dir"); args.add(cfg.modelDirectory.getFullPathName());

    m_proc = std::make_unique<juce::ChildProcess>();
    auto savedCwd = juce::File::getCurrentWorkingDirectory();
    workingDir.setAsCurrentWorkingDirectory();
    const bool ok = m_proc->start(args,
        juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);
    savedCwd.setAsCurrentWorkingDirectory();

    if (! ok) {
        m_lastError = "ChildProcess::start failed.";
        if (onLogLine) onLogLine("[Sidecar] " + m_lastError);
        m_proc.reset();
        return false;
    }

    m_readySeen = false; m_crashChecked = false;
    m_stdoutThread = std::thread([this]() { drainStdout(); });
    juce::MessageManager::callAsync([this]() { startTimer(500); });
    return true;
   #endif
}

void SidecarManager::drainStdout()
{
    // Runs on m_stdoutThread - blocks on ReadFile, never touches message thread.
    juce::String buf;

   #if JUCE_WINDOWS
    if (m_stdoutRead == nullptr) return;
    char chunk[4096];
    DWORD bytesRead = 0;
    while (::ReadFile(static_cast<HANDLE>(m_stdoutRead), chunk, sizeof(chunk), &bytesRead, nullptr)
           && bytesRead > 0)
    {
        buf += juce::String(chunk, (size_t) bytesRead);
        int nl;
        while ((nl = buf.indexOfChar('\n')) >= 0) {
            auto line = buf.substring(0, nl).trim();
            buf = buf.substring(nl + 1);
            if (line.isNotEmpty()) dispatchLine(line);
        }
        if (m_stopFlag.load()) break;
    }
    // Flush remainder
    if (buf.trim().isNotEmpty()) dispatchLine(buf.trim());
    ::CloseHandle(static_cast<HANDLE>(m_stdoutRead));
    m_stdoutRead = nullptr;
   #else
    if (! m_proc) return;
    char chunk[4096];
    while (! m_stopFlag.load()) {
        auto bytes = m_proc->readProcessOutput(chunk, sizeof(chunk));
        if (bytes <= 0) { juce::Thread::sleep(50); continue; }
        buf += juce::String(chunk, (size_t) bytes);
        int nl;
        while ((nl = buf.indexOfChar('\n')) >= 0) {
            auto line = buf.substring(0, nl).trim();
            buf = buf.substring(nl + 1);
            if (line.isNotEmpty()) dispatchLine(line);
        }
    }
   #endif
}

void SidecarManager::dispatchLine(const juce::String& line)
{
    if (line == "READY" || line.startsWith("READY ")) {
        m_readySeen.store(true);
    }
    // Post to message thread so onLogLine (which updates UI) is always on msg thread
    if (onLogLine) {
        auto cb   = onLogLine;
        auto copy = line;
        juce::MessageManager::callAsync([cb, copy]() { cb(copy); });
    }
}

void SidecarManager::shutdown()
{
    m_stopFlag.store(true);
    stopTimer();

   #if JUCE_WINDOWS
    if (m_processHandle != nullptr) {
        ::TerminateProcess(static_cast<HANDLE>(m_processHandle), 1);
        ::CloseHandle(static_cast<HANDLE>(m_processHandle));
        m_processHandle = nullptr;
    }
    if (m_stdoutRead != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_stdoutRead));
        m_stdoutRead = nullptr;
    }
    if (m_jobHandle != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
   #endif

    if (m_proc) { m_proc->kill(); m_proc.reset(); }
    if (m_stdoutThread.joinable()) m_stdoutThread.join();
    m_stopFlag.store(false);
}

bool SidecarManager::isRunning() const
{
   #if JUCE_WINDOWS
    if (m_processHandle != nullptr) {
        DWORD code = STILL_ACTIVE;
        ::GetExitCodeProcess(static_cast<HANDLE>(m_processHandle), &code);
        return code == STILL_ACTIVE;
    }
   #endif
    return m_proc && m_proc->isRunning();
}

void SidecarManager::timerCallback()
{
    // Only crash detection here - NO stdout reads (those happen on m_stdoutThread).
    if (isRunning()) return;

    stopTimer();

    if (! m_readySeen.load() && ! m_crashChecked.load()) {
        m_crashChecked.store(true);
        const auto elapsed = juce::Time::currentTimeMillis() - m_launchTimeMs;
        juce::String hint;
        if      (elapsed < 2000)  hint = "Died under 2s - antivirus likely blocked sidecar.exe.";
        else if (elapsed < 15000) hint = "Died during startup - missing Python module or CUDA DLL.";
        else                      hint = "Died during model load - insufficient RAM/VRAM.";

        m_lastError = "Sidecar exited unexpectedly (" + juce::String(elapsed) + "ms).\n\n"
                    + hint + "\n\nPath: " + m_sidecarPath;
        if (onLogLine) onLogLine("ERROR " + m_lastError);
    }
}

} // namespace AIMC
