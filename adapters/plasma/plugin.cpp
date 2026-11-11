#include <qqml.h>

#include <QQmlExtensionPlugin>

#include "translations.hpp"
#include "wallpaper_item.hpp"

extern void qml_register_types_NekoMote_Wallpaper();

class NekoMote_WallpaperPlugin final : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
  public:
    explicit NekoMote_WallpaperPlugin(QObject* parent = nullptr) : QQmlExtensionPlugin(parent)
    {
        // Retain the generated type registration even with LTO/dead stripping.
        volatile auto registration = &qml_register_types_NekoMote_Wallpaper;
        Q_UNUSED(registration);
    }
    void registerTypes(const char* uri) override
    {
        Q_ASSERT(uri && *uri);
        // The generated registrar handles the named module used by source/distro
        // installs. Directory imports have a host-dependent URI, so register the
        // same type there explicitly; Qt 6.4 cannot alias a named module to it.
        if (qstrcmp(uri, "NekoMote.Wallpaper") != 0)
            qmlRegisterType<nekomote::plasma::NekoWallpaperItem>(uri, 1, 0, "NekoWallpaperItem");
    }
    void initializeEngine(QQmlEngine*, const char*) override
    {
        nekomote::plasma::install_translations();
    }
};

#include "plugin.moc"
