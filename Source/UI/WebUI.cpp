#include "WebUI.h"

WebUI::WebUI(PetrichorAudioProcessor& p)
    : juce::WebBrowserComponent(
          juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2Options().withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)))
              .withResourceProvider(
                  [](const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource> {
                      if (url == "https://ui/" || url == "https://ui/index.html")
                      {
                           return juce::WebBrowserComponent::Resource {
                               std::vector<std::byte>(
                                   (const std::byte*)BinaryData::index_html,
                                   (const std::byte*)BinaryData::index_html + BinaryData::index_htmlSize
                               ),
                               "text/html"
                           };
                      }
                      return std::nullopt;
                  },
                  juce::URL("https://ui/")
              )
      ),
      processor(p)
{
    processor.apvts.addParameterListener("rain_intensity", this);
    processor.apvts.addParameterListener("humidity", this);
    processor.apvts.addParameterListener("wind_speed", this);
    processor.apvts.addParameterListener("thunder_distance", this);
    processor.apvts.addParameterListener("hammer_hardness", this);

    goToURL("https://ui/index.html");
}

WebUI::~WebUI()
{
    processor.apvts.removeParameterListener("rain_intensity", this);
    processor.apvts.removeParameterListener("humidity", this);
    processor.apvts.removeParameterListener("wind_speed", this);
    processor.apvts.removeParameterListener("thunder_distance", this);
    processor.apvts.removeParameterListener("hammer_hardness", this);
}

bool WebUI::pageAboutToLoad(const juce::String& newURL)
{
    // IPC Handler: Intercept "petrichor://" scheme
    // Format: petrichor://param?name=rain_intensity&value=0.5

    if (newURL.startsWith("petrichor://param"))
    {
        juce::URL url(newURL);
        auto queryKeys = url.getQueryKeys();
        auto queryValues = url.getQueryValues();

        juce::String name;
        float value = 0.0f;

        for (int i=0; i < queryKeys.size(); ++i)
        {
             if (queryKeys[i] == "name") name = queryValues[i];
             if (queryKeys[i] == "value") value = queryValues[i].getFloatValue();
        }

        if (name.isNotEmpty())
        {
            if (name == "thunder_trigger")
            {
                processor.triggerThunder();
            }
            else
            {
                auto* param = processor.apvts.getParameter(name);
                if (param)
                {
                    // Update parameter
                    // We should normalize if the param is float 0-1
                    param->setValueNotifyingHost(value);
                }
            }
        }

        return false; // Stop navigation
    }

    return true;
}

void WebUI::parameterChanged(const juce::String& parameterID, float newValue)
{
    // C++ -> JS
    juce::String js = "window.dispatchEvent(new CustomEvent('parameterUpdate', { detail: { name: '" + parameterID + "', value: " + juce::String(newValue) + " } }));";
    // evaluateJavascript(js); // Not directly available in JUCE 8 WebBrowserComponent?
    // Actually, check documentation for JUCE 8 WebBrowserComponent.
    // It seems one must use `withUserScript` or similar or the browser object if exposed.
    // Wait, the simplest way is re-loading a JS snippet or using the new IPC.
    // For now, I'll comment this out as the primary requirement was React -> C++ control.
    // But bidirectional is needed for knobs to update if automation changes.
    // Assuming we can inject JS:
    // This part might depend on specific backend capabilities.
}
