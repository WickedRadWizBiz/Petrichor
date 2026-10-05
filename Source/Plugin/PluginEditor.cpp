#include "PluginEditor.h"
#include "Parameters.h"
#include "PetrichorFrontend.h"

namespace
{
    std::vector<std::unique_ptr<juce::WebSliderRelay>> makeRelays()
    {
        std::vector<std::unique_ptr<juce::WebSliderRelay>> result;
        for (const auto& spec : petrichor::params::all())
            result.push_back (std::make_unique<juce::WebSliderRelay> (spec.id));
        return result;
    }

    std::vector<std::byte> toBytes (const char* data, int size)
    {
        const auto* begin = reinterpret_cast<const std::byte*> (data);
        return { begin, begin + size };
    }
}

PetrichorAudioProcessorEditor::PetrichorAudioProcessorEditor (PetrichorAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      relays (makeRelays()),
      webView (createWebOptions())
{
    const auto& specs = petrichor::params::all();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        auto* parameter = processorRef.apvts.getParameter (specs[i].id);
        jassert (parameter != nullptr);
        attachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*parameter, *relays[i], nullptr));
    }

    addAndMakeVisible (webView);
    webView.goToURL (pageUrl());

    setResizable (true, true);
    setResizeLimits (900, 600, 2000, 1400);
    setSize (1120, 740);
    startTimerHz (30);
}

PetrichorAudioProcessorEditor::~PetrichorAudioProcessorEditor()
{
    stopTimer();

    // Don't leave notes hanging if the window closes mid-press.
    for (int key = 0; key < 128; ++key)
        if (uiHeldKeys.test ((size_t) key))
            processorRef.keyboardState.noteOff (1, key, 0.0f);
}

juce::WebBrowserComponent::Options PetrichorAudioProcessorEditor::createWebOptions()
{
    auto options = juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("PetrichorPianoWebView"))
                                     .withBackgroundColour (juce::Colour (0xff0b1016))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withNativeFunction ("noteOn", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            if (args.size() >= 2)
            {
                const int key = juce::jlimit (0, 127, (int) args[0]);
                uiHeldKeys.set ((size_t) key);
                processorRef.keyboardState.noteOn (1, key, juce::jlimit (1, 127, (int) args[1]) / 127.0f);
            }
            complete ({});
        })
        .withNativeFunction ("noteOff", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            if (args.size() >= 1)
            {
                const int key = juce::jlimit (0, 127, (int) args[0]);
                uiHeldKeys.reset ((size_t) key);
                processorRef.keyboardState.noteOff (1, key, 0.0f);
            }
            complete ({});
        });

    for (auto& relay : relays)
        options = options.withOptionsFrom (*relay);

    return options;
}

juce::String PetrichorAudioProcessorEditor::pageUrl()
{
   #if JUCE_LINUX
    // JUCE 8's Linux web view relays resource-provider responses to its WebKit helper process
    // through a pipe as JSON, and the helper drops messages that arrive in pieces. The ~1.4 MB
    // encoded page does, which crashes the helper. So on Linux the bundled page is written once to
    // a cache file (named by content hash) and loaded from disk; native integration still works.
    const auto bytes = juce::MemoryBlock (PetrichorFrontend::index_html, (size_t) PetrichorFrontend::index_htmlSize);
    const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
        juce::String::fromUTF8 (PetrichorFrontend::index_html, PetrichorFrontend::index_htmlSize), std::numeric_limits<int>::max()));
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("PetrichorPiano")
                          .getChildFile ("ui-" + hash + ".html");

    if (file.getSize() != (juce::int64) bytes.getSize())
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithData (bytes.getData(), bytes.getSize());
    }

    if (file.existsAsFile())
        return juce::URL (file).toString (false);
   #endif

    return juce::WebBrowserComponent::getResourceProviderRoot();
}

std::optional<juce::WebBrowserComponent::Resource> PetrichorAudioProcessorEditor::getResource (const juce::String& url) const
{
    // The whole UI is one self-contained page (scripts, styles and fonts inlined at build time).
    const auto path = url.fromFirstOccurrenceOf ("/", false, false);
    if (path.isEmpty() || path == "index.html")
        return juce::WebBrowserComponent::Resource { toBytes (PetrichorFrontend::index_html, PetrichorFrontend::index_htmlSize), "text/html" };

    return std::nullopt;
}

void PetrichorAudioProcessorEditor::timerCallback()
{
    const auto& t = processorRef.getTelemetry();
    constexpr auto relaxed = std::memory_order_relaxed;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("windSpeed",   t.windSpeed.load (relaxed));
    obj->setProperty ("windGust",    t.windGust.load (relaxed));
    obj->setProperty ("gustFactor",  t.gustFactor.load (relaxed));
    obj->setProperty ("rainRate",    t.rainRate.load (relaxed));
    obj->setProperty ("lambda",      t.lambda.load (relaxed));
    obj->setProperty ("grainRate",   t.grainRate.load (relaxed));
    obj->setProperty ("meanDropMM",  t.meanDropMM.load (relaxed));
    obj->setProperty ("impactSpeed", t.impactSpeed.load (relaxed));
    obj->setProperty ("activeVoices", t.activeVoices.load (relaxed));
    obj->setProperty ("strikeCount", (int) t.strikeCount.load (std::memory_order_acquire));
    obj->setProperty ("lastStrikeDistance", t.lastStrikeDistance.load (relaxed));
    obj->setProperty ("lastStrikeKey",      t.lastStrikeKey.load (relaxed));
    obj->setProperty ("lastStrikeVelocity", t.lastStrikeVelocity.load (relaxed));

    juce::Array<juce::var> levels;
    levels.ensureStorageAllocated ((int) t.keyLevels.size());
    for (const auto& level : t.keyLevels)
        levels.add (level.load (relaxed));
    obj->setProperty ("keyLevels", levels);

    webView.emitEventIfBrowserIsVisible ("telemetry", juce::var (obj));
}

void PetrichorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b1016));
}

void PetrichorAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}
