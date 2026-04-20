#include "PluginProcessor.h"
#include "../inference/HttpSidecarBackend.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace AIMC {

juce::AudioProcessorEditor* createEditorFor(PluginProcessor& p);

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties())
    , m_apvts(*this, nullptr, "Params", createLayout())
{
}

PluginProcessor::~PluginProcessor()
{
    if (m_backend) m_backend->stop();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midi)
{
    juce::ignoreUnused(buffer, midi);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return createEditorFor(*this);
}

void PluginProcessor::initBackend()
{
    if (m_backend) return;
    ensureSessionDir();
    m_backend = std::make_unique<HttpSidecarBackend>(
        locateBundledSidecar(), resolveModelCacheDir());
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto root = std::make_unique<juce::XmlElement>("AIMCState");
    if (auto stateTree = m_apvts.copyState().createXml())
        root->addChildElement(stateTree.release());
    root->addChildElement(m_history.toXml().release());
    copyXmlToBinary(*root, dest);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    if (auto* p = xml->getChildByName(m_apvts.state.getType()))
        m_apvts.replaceState(juce::ValueTree::fromXml(*p));
    if (auto* ch = xml->getChildByName("ChatHistory"))
        m_history.fromXml(*ch);
}

void PluginProcessor::ensureSessionDir()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("AIMidiComposer").getChildFile("sessions");
    base.createDirectory();
    auto name = juce::Time::getCurrentTime().formatted("session_%Y%m%d_%H%M%S_")
              + juce::String::toHexString(juce::Random::getSystemRandom().nextInt());
    m_sessionDir = base.getChildFile(name);
    m_sessionDir.createDirectory();
}

juce::File PluginProcessor::locateBundledSidecar() const
{
    auto programFiles = juce::File::getSpecialLocation(
        juce::File::globalApplicationsDirectory);

    // v2: venv mode - launch sidecar.cmd which activates venv and runs main.py
    auto installed = programFiles
        .getChildFile("AIMidiComposer")
        .getChildFile("sidecar")
        .getChildFile("sidecar.cmd");
    if (installed.existsAsFile()) return installed;

   #if JUCE_WINDOWS
    HMODULE hModule = nullptr;
    if (::GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&locateBundledSidecarAddressTag),
            &hModule) && hModule != nullptr)
    {
        char path[MAX_PATH] = {};
        if (::GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
            auto beside = juce::File(juce::String(path))
                              .getParentDirectory()
                              .getChildFile("sidecar")
                              .getChildFile("sidecar.cmd");
            if (beside.existsAsFile()) return beside;
        }
    }
   #endif

    return installed;
}

void PluginProcessor::locateBundledSidecarAddressTag() {}

juce::File PluginProcessor::resolveModelCacheDir() const
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("AIMidiComposer").getChildFile("models");
    base.createDirectory();
    return base;
}

} // namespace AIMC

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIMC::PluginProcessor();
}
