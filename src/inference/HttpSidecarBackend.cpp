#include "HttpSidecarBackend.h"
#include <juce_core/juce_core.h>

namespace AIMC {

HttpSidecarBackend::HttpSidecarBackend(juce::File sidecarExe, juce::File modelDir)
    : m_sidecarExe(std::move(sidecarExe)),
      m_modelDir(std::move(modelDir))
{
}

HttpSidecarBackend::~HttpSidecarBackend()
{
    stop();
}

void HttpSidecarBackend::updateStatus(Status s, juce::String msg, float progress)
{
    m_status.store(s);
    if (m_statusCb) {
        StatusUpdate u { s, progress, std::move(msg) };
        juce::MessageManager::callAsync([cb = m_statusCb, u]() { cb(u); });
    }
}

void HttpSidecarBackend::start(StatusCallback onStatus)
{
    m_statusCb = std::move(onStatus);
    updateStatus(Status::Starting, "Launching sidecar...");

    // Spawn sidecar on a background thread so the DAW message thread
    // returns instantly. All filesystem I/O and CreateProcess happen there.
    std::thread([this]() {
        m_sidecar = std::make_unique<SidecarManager>();

        auto logFile = m_modelDir.getParentDirectory().getChildFile("sidecar.log");
        logFile.getParentDirectory().createDirectory();
        logFile.replaceWithText("=== Sidecar log started at "
            + juce::Time::getCurrentTime().toString(true, true) + " ===\n");
        m_logFile = logFile;

        m_sidecar->onLogLine = [this](const juce::String& line) {
            if (m_logFile.existsAsFile())
                m_logFile.appendText(line + "\n");

            if (line.startsWith("PROGRESS download ")) {
                float pct = line.fromFirstOccurrenceOf("PROGRESS download ", false, false).getFloatValue();
                updateStatus(Status::DownloadingModel, "Downloading model...", pct / 100.f);
            } else if (line.startsWith("PROGRESS load ")) {
                float pct = line.fromFirstOccurrenceOf("PROGRESS load ", false, false).getFloatValue();
                updateStatus(Status::LoadingModel, "Loading model into memory...", pct / 100.f);
            } else if (line.startsWith("READY")) {
                // Uvicorn needs ~500-1000ms after printing READY to fully bind its socket.
                // Probe healthz with retries before declaring Ready to the UI.
                std::thread([this]() {
                    juce::Thread::sleep(800);
                    for (int attempt = 0; attempt < 10; ++attempt) {
                        int hCode = 0;
                        auto url = juce::URL("http://127.0.0.1:" + juce::String(m_sidecarPort.load()) + "/healthz");
                        auto stream = url.createInputStream(
                            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                .withConnectionTimeoutMs(2000)
                                .withStatusCode(&hCode));
                        if (stream && hCode == 200) {
                            updateStatus(Status::Ready, "Model ready.", 1.f);
                            return;
                        }
                        juce::Thread::sleep(500);
                    }
                    updateStatus(Status::Failed, "Sidecar printed READY but healthz never responded.\nPort: "
                        + juce::String(m_sidecarPort.load()), 0.f);
                }).detach();
            } else if (line.startsWith("ERROR ")) {
                auto errMsg = line.substring(6);
                errMsg << "\n\nFull sidecar log: " << m_logFile.getFullPathName();
                updateStatus(Status::Failed, errMsg, 0.f);
            }
        };

        SidecarManager::Config cfg;
        cfg.sidecarExecutable = m_sidecarExe;
        cfg.modelDirectory    = m_modelDir;

        if (! m_sidecar->launch(cfg)) {
            auto reason = m_sidecar->lastError();
            updateStatus(Status::Failed,
                         reason.isEmpty() ? juce::String("Failed to launch sidecar process.")
                                          : reason);
            return;
        }
        // Store port atomically - readable from generate() thread without locking
        m_sidecarPort.store(m_sidecar->portInUse());
        juce::MessageManager::callAsync([this]() { startTimer(2000); });
    }).detach();
}

void HttpSidecarBackend::stop()
{
    stopTimer();
    m_sidecarPort.store(0);
    if (m_inflight && m_inflight->joinable())
        m_inflight->join();
    if (m_sidecar) {
        m_sidecar->shutdown();
        m_sidecar.reset();
    }
    updateStatus(Status::NotStarted, {});
}

void HttpSidecarBackend::timerCallback()
{
    // Fallback readiness probe in case we never saw a "READY" log line
    // (e.g. stdout buffering). Stop polling once ready/failed.
    auto s = m_status.load();
    if (s == Status::Ready || s == Status::Failed) {
        stopTimer();
        return;
    }
    if (m_sidecar && m_sidecar->isRunning())
        dispatchHealthCheck();
}

void HttpSidecarBackend::dispatchHealthCheck()
{
    // Non-blocking health check on a throwaway thread
    std::thread([this]() {
        auto url = juce::URL(baseUrl() + "/healthz");
        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(1000)
                .withStatusCode(&statusCode));
        if (stream && statusCode == 200) {
            auto body = stream->readEntireStreamAsString();
            if (body.contains("\"ready\":true"))
                updateStatus(Status::Ready, "Model ready.", 1.f);
        }
    }).detach();
}

void HttpSidecarBackend::generate(Request req,
                                  ProgressCallback onProgress,
                                  DoneCallback onDone)
{
    if (m_busy.exchange(true)) {
        GenerationResult r;
        r.success = false;
        r.errorMessage = "Another generation is already in progress.";
        juce::MessageManager::callAsync([cb = std::move(onDone), r]() { cb(r); });
        return;
    }
    if (m_inflight && m_inflight->joinable()) m_inflight->join();

    m_inflight = std::make_unique<std::thread>([this, req,
                                                onProgress = std::move(onProgress),
                                                onDone     = std::move(onDone)]() mutable {
        auto finish = [this, &onDone](GenerationResult r) mutable {
            m_busy.store(false);
            juce::MessageManager::callAsync([cb = onDone, r]() mutable { cb(r); });
        };

        if (m_status.load() != Status::Ready) {
            GenerationResult r;
            r.success = false;
            r.errorMessage = "Model is not ready yet.";
            finish(r);
            return;
        }

        if (m_sidecarPort.load() == 0) {
            GenerationResult r;
            r.success = false;
            r.errorMessage = "Sidecar port not assigned. Try restarting the plugin.";
            finish(r);
            return;
        }

        juce::var body(new juce::DynamicObject());
        body.getDynamicObject()->setProperty("prompt",          req.prompt);
        body.getDynamicObject()->setProperty("guidance_scale",  (double) req.guidanceScale);
        if (req.duration > 0.f)
            body.getDynamicObject()->setProperty("duration", (double) req.duration);

        auto generationUrl = baseUrl() + "/generate";
        auto bodyStr = juce::JSON::toString(body);
        auto url = juce::URL(generationUrl).withPOSTData(bodyStr);

        // Confirm sidecar is reachable before sending the full request
        bool reachable = false;
        for (int attempt = 0; attempt < 3 && !reachable; ++attempt) {
            if (attempt > 0) juce::Thread::sleep(500);
            int hCode = 0;
            auto hStream = juce::URL(baseUrl() + "/healthz").createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(3000)
                    .withStatusCode(&hCode));
            reachable = (hStream && hCode == 200);
        }
        if (!reachable) {
            GenerationResult r; r.success = false;
            r.errorMessage = "Sidecar not reachable at " + baseUrl()
                + "\nCheck sidecar.log for details.";
            finish(r); return;
        }

        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders("Content-Type: application/json")
                .withConnectionTimeoutMs(10 * 60 * 1000)
                .withStatusCode(&statusCode));

        if (! stream) {
            GenerationResult r; r.success = false;
            r.errorMessage = "Lost connection to sidecar at " + baseUrl() + " during generation.";
            finish(r); return;
        }
        if (statusCode != 200) {
            GenerationResult r; r.success = false;
            r.errorMessage = "Sidecar returned HTTP " + juce::String(statusCode);
            finish(r); return;
        }

        auto respStr = stream->readEntireStreamAsString();
        auto parsed  = juce::JSON::parse(respStr);
        if (! parsed.isObject()) {
            GenerationResult r; r.success = false;
            r.errorMessage = "Malformed sidecar response.";
            finish(r); return;
        }

        GenerationResult r;
        r.success           = parsed.getProperty("success", false);
        r.errorMessage      = parsed.getProperty("error", {}).toString();
        r.detectedKey       = parsed.getProperty("key", {}).toString();
        r.detectedTempo     = parsed.getProperty("tempo", {}).toString();
        r.detectedTimeSig   = parsed.getProperty("time_signature", {}).toString();
        r.assistantSummary  = parsed.getProperty("summary", {}).toString();
        r.generationSeconds = parsed.getProperty("seconds", 0.0);

        // WAV audio (new in v2 with ACE-Step)
        auto wavB64 = parsed.getProperty("wav_base64", {}).toString();
        if (wavB64.isNotEmpty()) {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64(mos, wavB64);
            r.wavBytes.setSize(mos.getDataSize());
            std::memcpy(r.wavBytes.getData(), mos.getData(), mos.getDataSize());
        }

        // MIDI from Basic Pitch transcription
        auto midiB64 = parsed.getProperty("midi_base64", {}).toString();
        if (midiB64.isNotEmpty()) {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64(mos, midiB64);
            r.combinedMidiBytes.setSize(mos.getDataSize());
            std::memcpy(r.combinedMidiBytes.getData(), mos.getData(), mos.getDataSize());
        }

        finish(r);
    });
}

} // namespace AIMC
