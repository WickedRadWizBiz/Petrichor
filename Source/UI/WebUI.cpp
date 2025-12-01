#include "WebUI.h"
#include <JuceHeader.h>
#include "../FrontendAssets.h" // New generated header

#if JUCE_MAJOR_VERSION < 8
    #error "Petrichor requires JUCE 8.0.0 or later to support offline resource serving."
#endif

WebUI::WebUI(PetrichorAudioProcessor& p)
    : juce::WebBrowserComponent(
          juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withResourceProvider(
                  [](const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource> {
                      if (url == "https://ui/" || url == "https://ui/index.html")
                      {
                           // Use the embedded C++ array
                           return juce::WebBrowserComponent::Resource {
                               std::vector<std::byte>(
                                   (const std::byte*)FrontendAssets::index_html_data,
                                   (const std::byte*)FrontendAssets::index_html_data + FrontendAssets::index_html_size
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

        return false;
    }

    return true;
}

void WebUI::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(parameterID, newValue);
}
