#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "wallpaper_item.hpp"

namespace
{
void check_status(QQmlEngine& engine, const QByteArray& properties, int expected, const char* label)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                       "NekoWallpaperItem { width: 64; height: 64; " +
                           properties + " }",
                       {});
    std::unique_ptr<QObject> object(component.create());
    auto* item = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(object.get());
    if (!item)
        throw std::runtime_error(std::string(label) + ": QML creation failed");
    if (item->status() != expected)
        throw std::runtime_error(std::string(label) +
                                 ": expected status=" + std::to_string(expected) +
                                 " got=" + std::to_string(item->status()));
    std::printf("%s: status=%d OK\n", label, expected);
}

void check_input_isolation(QQmlEngine& engine, const QString& assets_dir)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                       "NekoWallpaperItem { width: 64; height: 64; assetsDir: '" +
                           assets_dir.toUtf8() + "'; }",
                       {});
    std::unique_ptr<QObject> object(component.create());
    auto* item = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(object.get());
    if (!item || item->status() != 1)
        throw std::runtime_error("input isolation: item did not load");
    if (!item->configure_diagnostics(0, false, 1))
        throw std::runtime_error("input isolation: configure_diagnostics failed");
    if (item->acceptHoverEvents() || item->acceptedMouseButtons() != Qt::NoButton)
        throw std::runtime_error("input isolation: diagnostic mode did not disable input");
    std::puts("input isolation: hover/mouse disabled in diagnostic mode OK");
}
} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
#ifdef NEKOMOTE_QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(NEKOMOTE_QML_IMPORT_PATH));
#endif
    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    try
    {
        check_status(engine, {}, 0, "empty config");
        const auto invalid_assets = QUrl::fromLocalFile(dir.filePath("nonexistent")).toString();
        check_status(engine, ("assetsDir: '" + invalid_assets.toUtf8() + "';").constData(), 1,
                     "with assets dir");
        const auto assets = qEnvironmentVariable("NEKOMOTE_TEST_ASSETS_DIR");
        if (!assets.isEmpty())
            check_input_isolation(engine, assets);
        else
            std::puts("input isolation: SKIPPED (set NEKOMOTE_TEST_ASSETS_DIR)");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
