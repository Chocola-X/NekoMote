#include <QQmlEngineExtensionPlugin>

#include "translations.hpp"

extern void qml_register_types_NekoMote_Wallpaper();

class NekoMote_WallpaperPlugin final : public QQmlEngineExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)
  public:
    explicit NekoMote_WallpaperPlugin(QObject* parent = nullptr) : QQmlEngineExtensionPlugin(parent)
    {
        // Retain the generated type registration even with LTO/dead stripping.
        volatile auto registration = &qml_register_types_NekoMote_Wallpaper;
        Q_UNUSED(registration);
    }
    void initializeEngine(QQmlEngine*, const char*) override
    {
        nekomote::plasma::install_translations();
    }
};

#include "plugin.moc"
