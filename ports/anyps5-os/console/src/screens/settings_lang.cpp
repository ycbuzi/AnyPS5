#include "screens/settings.h"
#include <map>

namespace ui::sett {

static const std::map<std::string, std::string>& languages() {
    static const std::map<std::string, std::string> m = {
        {"af", "Afrikaans"}, {"ar", "Arabic"}, {"az", "Azerbaijani"}, {"be", "Belarusian"}, {"bg", "Bulgarian"},
        {"bn", "Bangla"}, {"bs", "Bosnian"}, {"ca", "Catalan"}, {"cs", "Czech"}, {"cy", "Welsh"}, {"da", "Danish"},
        {"de", "German"}, {"el", "Greek"}, {"en", "English"}, {"eo", "Esperanto"}, {"es", "Spanish"}, {"et", "Estonian"},
        {"eu", "Basque"}, {"fa", "Persian"}, {"fi", "Finnish"}, {"fil", "Filipino"}, {"fo", "Faroese"}, {"fr", "French"},
        {"ga", "Irish"}, {"gd", "Scottish Gaelic"}, {"gl", "Galician"}, {"gu", "Gujarati"}, {"he", "Hebrew"},
        {"hi", "Hindi"}, {"hr", "Croatian"}, {"hu", "Hungarian"}, {"hy", "Armenian"}, {"id", "Indonesian"},
        {"is", "Icelandic"}, {"it", "Italian"}, {"ja", "Japanese"}, {"ka", "Georgian"}, {"kk", "Kazakh"},
        {"km", "Khmer"}, {"kn", "Kannada"}, {"ko", "Korean"}, {"ku", "Kurdish"}, {"ky", "Kyrgyz"}, {"lb", "Luxembourgish"},
        {"lo", "Lao"}, {"lt", "Lithuanian"}, {"lv", "Latvian"}, {"mk", "Macedonian"}, {"ml", "Malayalam"},
        {"mn", "Mongolian"}, {"mr", "Marathi"}, {"ms", "Malay"}, {"mt", "Maltese"}, {"my", "Burmese"},
        {"nb", "Norwegian Bokmal"}, {"ne", "Nepali"}, {"nl", "Dutch"}, {"nn", "Norwegian Nynorsk"}, {"no", "Norwegian"},
        {"pa", "Punjabi"}, {"pl", "Polish"}, {"ps", "Pashto"}, {"pt", "Portuguese"}, {"ro", "Romanian"}, {"ru", "Russian"},
        {"si", "Sinhala"}, {"sk", "Slovak"}, {"sl", "Slovenian"}, {"sq", "Albanian"}, {"sr", "Serbian"}, {"sv", "Swedish"},
        {"sw", "Swahili"}, {"ta", "Tamil"}, {"te", "Telugu"}, {"tg", "Tajik"}, {"th", "Thai"}, {"tk", "Turkmen"},
        {"tr", "Turkish"}, {"uk", "Ukrainian"}, {"ur", "Urdu"}, {"uz", "Uzbek"}, {"vi", "Vietnamese"}, {"zh", "Chinese"},
        {"zu", "Zulu"}};
    return m;
}

static const std::map<std::string, std::string>& regions() {
    static const std::map<std::string, std::string> m = {
        {"AE", "United Arab Emirates"}, {"AR", "Argentina"}, {"AT", "Austria"}, {"AU", "Australia"}, {"BE", "Belgium"},
        {"BG", "Bulgaria"}, {"BR", "Brazil"}, {"CA", "Canada"}, {"CH", "Switzerland"}, {"CL", "Chile"}, {"CN", "China"},
        {"CO", "Colombia"}, {"CZ", "Czechia"}, {"DE", "Germany"}, {"DK", "Denmark"}, {"EG", "Egypt"}, {"ES", "Spain"},
        {"FI", "Finland"}, {"FR", "France"}, {"GB", "United Kingdom"}, {"GR", "Greece"}, {"HK", "Hong Kong"},
        {"HU", "Hungary"}, {"ID", "Indonesia"}, {"IE", "Ireland"}, {"IL", "Israel"}, {"IN", "India"}, {"IT", "Italy"},
        {"JP", "Japan"}, {"KR", "South Korea"}, {"MX", "Mexico"}, {"MY", "Malaysia"}, {"NL", "Netherlands"},
        {"NO", "Norway"}, {"NZ", "New Zealand"}, {"PE", "Peru"}, {"PH", "Philippines"}, {"PL", "Poland"},
        {"PT", "Portugal"}, {"RO", "Romania"}, {"RU", "Russia"}, {"SA", "Saudi Arabia"}, {"SE", "Sweden"},
        {"SG", "Singapore"}, {"SK", "Slovakia"}, {"TH", "Thailand"}, {"TR", "Turkey"}, {"TW", "Taiwan"},
        {"UA", "Ukraine"}, {"US", "United States"}, {"VN", "Vietnam"}, {"ZA", "South Africa"}};
    return m;
}

std::string langName(const std::string& tag, const std::string& fallback) {
    if (tag.empty()) return fallback;
    std::string lang = tag, reg;
    size_t dash = tag.find_first_of("-_");
    if (dash != std::string::npos) {
        lang = tag.substr(0, dash);
        reg = tag.substr(dash + 1);
    }
    auto l = languages().find(lang);
    if (l == languages().end()) return fallback.empty() ? tag : fallback;
    if (reg.empty()) return l->second;
    auto r = regions().find(reg);
    return l->second + " (" + (r == regions().end() ? reg : r->second) + ")";
}

}
