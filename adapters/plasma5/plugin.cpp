#include <qqml.h>

#include <QQmlExtensionPlugin>
#include <cstring>

#include "wallpaper_item.hpp"

class NekoMoteWallpaperPlugin final : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
  public:
    void registerTypes(const char* uri) override
    {
        Q_ASSERT(std::strcmp(uri, "NekoMote.Wallpaper") == 0);
        qmlRegisterType<nekomote::plasma::NekoWallpaperItem>(uri, 1, 0, "NekoWallpaperItem");
    }
};

#include "plugin.moc"
