#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
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

void check_instance_isolation()
{
    nekomote::plasma::NekoWallpaperItem first;
    nekomote::plasma::NekoWallpaperItem second;
    const auto first_id = first.createInstanceId();
    const auto second_id = second.createInstanceId();
    first.setInstanceId(first_id);
    second.setInstanceId(second_id);
    const auto first_path = first.wallpaper_state()->config_path;
    const auto second_path = second.wallpaper_state()->config_path;
    const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (first_id == second_id || QUuid(first_id).isNull() || first_path == second_path ||
        first_path != (base + "/nekomote/plasma/" + first_id + ".json").toStdString() ||
        second_path != (base + "/nekomote/plasma/" + second_id + ".json").toStdString())
        throw std::runtime_error("instance isolation: UUID storage paths are not isolated");
    nekomote::plasma::NekoWallpaperItem recreated;
    recreated.setInstanceId(first_id);
    if (recreated.wallpaper_state()->config_path != first_path)
        throw std::runtime_error("instance isolation: recreation changed persisted UUID path");
    first.setInstanceId(QStringLiteral("../../outside"));
    if (first.status() != 2 || first.instanceId() != first_id ||
        first.wallpaper_state()->config_path != first_path)
        throw std::runtime_error("instance isolation: invalid UUID was accepted");
    first.setInstanceId(first_id);
    if (first.status() != 0 || !first.errorString().isEmpty())
        throw std::runtime_error("instance isolation: valid UUID retry did not recover");
    first.wallpaper_state()->config_dirty = false;
    first.setInstanceId(second_id);
    if (!first.wallpaper_state()->config_dirty ||
        first.wallpaper_state()->config_path != second_path)
        throw std::runtime_error("instance isolation: UUID switch did not request rebuild");
    first.setInstanceId({});
    if (!first.wallpaper_state()->config_path.empty())
        throw std::runtime_error("instance isolation: generic host did not retain default storage");
    std::puts("instance isolation: UUID paths, recreation, rejection and switching OK");
}

void check_backend_selection(QQmlEngine& engine)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                      "NekoWallpaperItem { width: 64; height: 64; }",
                      {});
    std::unique_ptr<QObject> object(component.create());
    auto* item = qobject_cast<nekomote::plasma::NekoWallpaperItem*>(object.get());
    if (!item || item->renderBackend() != "opengl")
        throw std::runtime_error("backend selection: default is not OpenGL");
    item->setRenderBackend(QStringLiteral("vulkan"));
    item->setRenderBackend(QStringLiteral("unknown"));
    if (item->status() != 2 || item->errorString().isEmpty() || item->renderBackend() != "vulkan")
        throw std::runtime_error("backend selection: invalid value was not rejected");
    item->setRenderBackend(QStringLiteral("vulkan"));
    if (item->status() != 0 || !item->errorString().isEmpty())
        throw std::runtime_error("backend selection: same-backend retry failed");
    std::puts("backend selection: OpenGL default, invalid value and retry OK");
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
        check_backend_selection(engine);
        check_instance_isolation();
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
