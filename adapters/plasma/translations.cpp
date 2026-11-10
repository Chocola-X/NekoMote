#include "translations.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QPointer>
#include <QTranslator>
#include <memory>

namespace nekomote::plasma
{
void install_translations()
{
    static QPointer<QTranslator> installed;
    if (installed)
        return;
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QLocale(), QStringLiteral("nekomote_plasma"), QStringLiteral("_"),
                          QStringLiteral(":/nekomote/plasma/i18n")))
        return; // Unsupported locales retain the English source strings.
    if (QCoreApplication::installTranslator(translator.get()))
    {
        translator->setParent(QCoreApplication::instance());
        installed = translator.release();
    }
}
} // namespace nekomote::plasma
