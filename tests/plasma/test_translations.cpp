#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <cstdio>
#include <memory>

namespace
{
void collect_text(QObject* object, QStringList& texts)
{
    const auto text = object->property("text");
    if (text.isValid())
        texts.push_back(text.toString());
    for (auto* child : object->children())
        collect_text(child, texts);
}
} // namespace

// Import the packaged settings page first: no native wallpaper item or model
// exists to install translations on its behalf. Each locale uses a new process.
int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (argc != 4)
        return 1;
    const QString locale = QString::fromLatin1(argv[3]);
    QLocale::setDefault(QLocale(locale));
    const bool chinese = locale == "zh_CN";
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[1]));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
    std::unique_ptr<QObject> page(component.create());
    if (!page || component.isError())
    {
        std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        return 1;
    }
    QStringList texts;
    collect_text(page.get(), texts);
    if (!texts.contains(chinese ? QStringLiteral("资产目录")
                                : QStringLiteral("Assets Directory")) ||
        !texts.contains(chinese ? QStringLiteral("渲染后端") : QStringLiteral("Render Backend")))
        return 2;
    const auto loading = QCoreApplication::translate("NekoMoteWallpaperMain", "Loading…");
    if (loading != (chinese ? QStringLiteral("正在加载…") : QStringLiteral("Loading…")))
        return 3;
    if (QCoreApplication::translate("config", "Assets Directory") != "Assets Directory" ||
        QCoreApplication::translate("main", "Loading…") != QStringLiteral("Loading…"))
        return 4; // Generic host contexts must remain untouched.
    bool assets_hint = false, backend_hint = false;
    for (const auto& text : texts)
    {
        if (text.startsWith(chinese ? QStringLiteral("选择 NekoMote 资产库")
                                    : QStringLiteral("Select the root directory")))
            assets_hint = text.contains('\n') && !text.contains(QStringLiteral("\\n"));
        if (text.startsWith(chinese ? QStringLiteral("选择 NekoMote 绘制")
                                    : QStringLiteral("Select the graphics API")))
            backend_hint = text.contains('\n') && !text.contains(QStringLiteral("\\n"));
    }
    if (!assets_hint || !backend_hint)
        return 5;
    QQmlEngine second_engine;
    second_engine.addImportPath(QString::fromLocal8Bit(argv[1]));
    QQmlComponent second_component(&second_engine,
                                   QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
    std::unique_ptr<QObject> second_page(second_component.create());
    if (!second_page || second_component.isError())
        return 6;
    QStringList second_texts;
    collect_text(second_page.get(), second_texts);
    if (!second_texts.contains(chinese ? QStringLiteral("资产目录")
                                       : QStringLiteral("Assets Directory")))
        return 7;
    std::printf("%s: settings-first import, locale, hints, status and host isolation PASS\n",
                argv[3]);
    return 0;
}
