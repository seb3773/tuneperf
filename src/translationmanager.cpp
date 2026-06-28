#include "translationmanager.h"
#include "zx0em_runtime.h"
#include <ntqstringlist.h>
#include <ntqlocale.h>

TranslationManager::TranslationManager()
    : m_currentLang("en")
{
    // Always load English as fallback first
    loadLanguage("en", m_fallbackTranslations);
    
    TQString sysLang = TQLocale::system().name().left(2).lower();
    TQStringList supported;
    supported << "en" << "fr" << "de" << "es" << "it" << "pl";
    if (supported.contains(sysLang)) {
        m_currentLang = sysLang;
        loadLanguage(sysLang, m_translations);
    } else {
        m_translations = m_fallbackTranslations;
    }
}

TranslationManager::~TranslationManager()
{
}

TranslationManager& TranslationManager::instance()
{
    static TranslationManager inst;
    return inst;
}

void TranslationManager::setLanguage(const TQString& langCode)
{
    m_currentLang = langCode;
    if (m_currentLang == "en") {
        m_translations = m_fallbackTranslations;
    } else {
        loadLanguage(langCode, m_translations);
    }
}

TQString TranslationManager::tr(const TQString& key, const TQString& defaultValue)
{
    if (m_translations.contains(key)) {
        return m_translations[key];
    }
    if (m_fallbackTranslations.contains(key)) {
        return m_fallbackTranslations[key];
    }
    return defaultValue.isEmpty() ? key : defaultValue;
}

void TranslationManager::loadLanguage(const TQString& langCode, TQMap<TQString, TQString>& destMap)
{
    destMap.clear();
    int assetId = -1;
    unsigned int assetSize = 0;
    
    if (langCode == "en") { assetId = ZX0EM_en_txt; assetSize = ZX0EM_en_txt_SIZE; }
    else if (langCode == "fr") { assetId = ZX0EM_fr_txt; assetSize = ZX0EM_fr_txt_SIZE; }
    else if (langCode == "de") { assetId = ZX0EM_de_txt; assetSize = ZX0EM_de_txt_SIZE; }
    else if (langCode == "es") { assetId = ZX0EM_es_txt; assetSize = ZX0EM_es_txt_SIZE; }
    else if (langCode == "it") { assetId = ZX0EM_it_txt; assetSize = ZX0EM_it_txt_SIZE; }
    else if (langCode == "pl") { assetId = ZX0EM_pl_txt; assetSize = ZX0EM_pl_txt_SIZE; }

    if (assetId == -1) return;

    char* buf = new char[assetSize + 1];
    int len = zx0em_decode_bpe(assetId, buf, assetSize + 1);
    if (len < 0) {
        delete[] buf;
        return;
    }

    TQString content = TQString::fromUtf8(buf);
    delete[] buf;

    TQStringList lines = TQStringList::split('\n', content);
    for (TQStringList::ConstIterator it = lines.begin(); it != lines.end(); ++it) {
        TQString line = (*it).stripWhiteSpace();
        if (line.isEmpty() || line.startsWith("#")) continue;
        int sep = line.find('|');
        if (sep != -1) {
            TQString key = line.left(sep).stripWhiteSpace();
            TQString desc = line.mid(sep + 1).stripWhiteSpace();
            destMap[key] = desc;
        }
    }
}
