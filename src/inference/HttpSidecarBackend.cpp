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
                updateStatus(Status::Ready, "Model ready.", 1.f);
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
        }
        // SidecarManager owns its own crash-detection timer.
        // HttpSidecarBackend's healthz timer is a fallback for buffered stdout.
        juce::MessageManager::callAsync([this]() { startTimer(2000); });
    }).detach();
}

void HttpSidecarBackend::stop()
{
    stopTimer();
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
        auto url = juce::URL(m_sidecar->baseUrl() + "/healthz");
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

        juce::var body(new juce::DynamicObject());
        body.getDynamicObject()->setProperty("prompt",      req.prompt);
        body.getDynamicObject()->setProperty("temperature", (double) req.temperature);
        body.getDynamicObject()->setProperty("top_p",       (double) req.topP);
        body.getDynamicObject()->setProperty("max_tokens",  req.maxTokens);

        auto bodyStr = juce::JSON::toString(body);
        auto url = juce::URL(m_sidecar->baseUrl() + "/generate").withPOSTData(bodyStr);

        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders("Content-Type: application/json")
                .withConnectionTimeoutMs(10 * 60 * 1000)       // generations can be slow on CPU
                .withStatusCode(&statusCode));

        if (! stream) {
            GenerationResult r; r.success = false;
            r.errorMessage = "Could not connect to sidecar.";
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

        // MIDI comes back as base64 in the JSON (keeps the protocol plain-text
        // friendly and debuggable with curl).
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
