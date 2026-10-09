#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>
#include <cmath>
#include <cstdio>
#include <fstream>
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

void check_input_isolation(QQmlEngine& engine, const QString& model, const QString& session)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport NekoMote.Wallpaper\n"
                      "NekoWallpaperItem { width: 64; height: 64; modelFile: '" +
                          QUrl::fromLocalFile(model).toString().toUtf8() + "'; sessionFile: '" +
                          QUrl::fromLocalFile(session).toString().toUtf8() + "'; }",
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
    const auto missing = QUrl::fromLocalFile(dir.filePath("missing.json")).toString();
    const auto bad_file = dir.filePath("bad.json");
    std::ofstream(bad_file.toStdString()) << "{\"schema_version\":999999}";
    const auto bad = QUrl::fromLocalFile(bad_file).toString();
    try
    {
        // Configuration rejection: no model/session
        check_status(engine, {}, 0, "empty config");
        check_status(engine, ("modelFile: '" + missing.toUtf8() + "';").constData(), 0,
                     "only model");
        check_status(engine, ("sessionFile: '" + missing.toUtf8() + "';").constData(), 0,
                     "only session");
        // Missing files
        check_status(
            engine,
            ("modelFile: '" + missing.toUtf8() + "'; sessionFile: '" + missing.toUtf8() + "';")
                .constData(),
            2, "missing files");
        // Bad JSON
        check_status(
            engine,
            ("modelFile: '" + bad.toUtf8() + "'; sessionFile: '" + bad.toUtf8() + "';").constData(),
            2, "bad JSON");
        // Invalid scale
        check_status(engine, "scale: 0;", 2, "scale zero");
        check_status(engine, "scale: -1;", 2, "scale negative");
        // Non-finite origin
        check_status(engine, "originX: NaN;", 2, "originX NaN");
        check_status(engine, "originY: Infinity;", 2, "originY infinity");
        // Valid model/session for input isolation
        const auto model = qEnvironmentVariable("NEKOMOTE_TEST_MODEL");
        const auto session = qEnvironmentVariable("NEKOMOTE_TEST_SESSION");
        if (!model.isEmpty() && !session.isEmpty())
            check_input_isolation(engine, model, session);
        else
            std::puts("input isolation: SKIPPED (set NEKOMOTE_TEST_MODEL/SESSION)");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
