#include "WebUI.h"

WebUI::WebUI(PetrichorAudioProcessor& p)
    : juce::WebBrowserComponent(
          juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              // .withWinWebView2Options(...) // Removed to fix build error; default behavior is fine.
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

        // Corrected API for JUCE URL parameter access
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
                    // Update parameter
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
    // Fix unused parameter warning
    juce::ignoreUnused(parameterID, newValue);

    // C++ -> JS
    // juce::String js = "window.dispatchEvent(new CustomEvent('parameterUpdate', { detail: { name: '" + parameterID + "', value: " + juce::String(newValue) + " } }));";
    // evaluateJavascript(js);
}
