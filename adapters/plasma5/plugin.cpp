#include <qqml.h>

#include <QQmlExtensionPlugin>

#include "translations.hpp"
#include "wallpaper_item.hpp"

class NekoMoteWallpaperPlugin final : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
  public:
    void registerTypes(const char* uri) override
    {
        // Qt5 also supplies a filesystem URI for package-local directory imports.
        Q_ASSERT(uri && *uri);

        qmlRegisterType<nekomote::plasma::NekoWallpaperItem>(uri, 1, 0, "NekoWallpaperItem");
    }
    void initializeEngine(QQmlEngine*, const char*) override
    {
        nekomote::plasma::install_translations();
    }
};

#include "plugin.moc"
