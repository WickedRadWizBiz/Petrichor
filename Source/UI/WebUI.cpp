#include "WebUI.h"
#include <JuceHeader.h>

// Enforce JUCE 8+ for Offline features
#if JUCE_MAJOR_VERSION < 8
    #error "Petrichor requires JUCE 8.0.0 or later to support offline resource serving. Please ensure you are building against the fetched JUCE 8 version, or update your global JUCE installation."
#endif

WebUI::WebUI(PetrichorAudioProcessor& p)
    : juce::WebBrowserComponent(
          juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
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

        auto queryKeys = url.getParameterNames();
        auto queryValues = url.getParameterValues();

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
    juce::ignoreUnused(parameterID, newValue);
}
