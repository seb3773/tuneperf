#ifndef TRANSLATIONMANAGER_H
#define TRANSLATIONMANAGER_H

#include <ntqstring.h>
#include <ntqmap.h>

class TranslationManager {
public:
    static TranslationManager& instance();

    void setLanguage(const TQString& langCode);
    TQString getLanguage() const { return m_currentLang; }

    TQString tr(const TQString& key, const TQString& defaultValue = "");

private:
    TranslationManager();
    ~TranslationManager();
    TranslationManager(const TranslationManager&);
    TranslationManager& operator=(const TranslationManager&);

    TQString m_currentLang;
    TQMap<TQString, TQString> m_translations;
    TQMap<TQString, TQString> m_fallbackTranslations;

    void loadLanguage(const TQString& langCode, TQMap<TQString, TQString>& destMap);
};

#define TRANS(key, ...) TranslationManager::instance().tr(key, ##__VA_ARGS__)

#endif // TRANSLATIONMANAGER_H
