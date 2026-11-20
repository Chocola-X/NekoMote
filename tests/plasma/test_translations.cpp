#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QList>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
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

void settle_layout(QQuickWindow& window)
{
    QEventLoop frame;
    QObject::connect(&window, &QQuickWindow::frameSwapped, &frame, &QEventLoop::quit);
    QTimer::singleShot(1000, &frame, &QEventLoop::quit);
    window.update();
    frame.exec();
}

bool links_fit(QQuickItem* links, QQuickItem* view, QQuickItem* page)
{
    const auto top_left = links->mapToItem(page, QPointF{});
    const auto bottom_right = links->mapToItem(page, QPointF(links->width(), links->height()));
    const auto view_top = view->mapToItem(page, QPointF{}).y();
    return top_left.x() >= 16 && bottom_right.x() <= page->width() - 16 &&
           top_left.y() >= view_top && bottom_right.y() <= view_top + view->height() + 1;
}

bool check_settings_layout(QQuickItem* page)
{
    if (!page)
        return false;
    auto* view = qobject_cast<QQuickItem*>(page->property("contentItem").value<QObject*>());
    QQuickItem* links = nullptr;
    QList<QQuickItem*> option_labels;
    for (auto* item : page->findChildren<QQuickItem*>())
    {
        if (item->property("text").toString().contains("<a href="))
            links = item;
        if (item->property("tristate").isValid())
            option_labels.push_back(
                qobject_cast<QQuickItem*>(item->property("contentItem").value<QObject*>()));
    }
    if (!view || !links || option_labels.size() != 2)
    {
        std::fprintf(stderr, "Settings layout is missing its scroll viewport, links or options\n");
        return false;
    }

    QQuickWindow window;
    window.resize(280, 200);
    page->setParentItem(window.contentItem());
    page->setSize(window.size());
    window.show();
    settle_layout(window);

    const auto content_height = view->property("contentHeight").toReal();
    const auto content_width = view->property("contentWidth").toReal();
    const bool overflow = content_height > view->height() &&
                          qAbs(content_width - view->width()) < 1 && !links_fit(links, view, page);
    for (auto* label : option_labels)
    {
        if (!label || !label->property("truncated").isValid() ||
            label->property("truncated").toBool())
        {
            std::fprintf(stderr, "A settings option label was truncated in the narrow layout\n");
            page->setParentItem(nullptr);
            return false;
        }
    }
    view->setProperty("contentY", content_height - view->height());
    settle_layout(window);
    const bool bottom_reachable = links_fit(links, view, page);

    window.resize(620, 800);
    page->setSize(window.size());
    settle_layout(window);
    const bool fits_at_full_size =
        view->property("contentHeight").toReal() <= view->height() && links_fit(links, view, page);
    page->setParentItem(nullptr);
    if (!overflow || !bottom_reachable || !fits_at_full_size)
        std::fprintf(stderr,
                     "Settings layout: small viewport overflow=%d, links reachable=%d, "
                     "fits after expanding=%d\n",
                     overflow, bottom_reachable, fits_at_full_size);
    return overflow && bottom_reachable && fits_at_full_size;
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
        !texts.contains(chinese ? QStringLiteral("渲染后端") : QStringLiteral("Render Backend")) ||
        !texts.contains(chinese ? QStringLiteral("锁屏、休眠或关屏时暂停")
                                : QStringLiteral("Pause on lock, sleep or screen off")))
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
    if (!check_settings_layout(qobject_cast<QQuickItem*>(page.get())))
        return 8;
    std::printf(
        "%s: settings-first import, locale, hints, status, host isolation and layout PASS\n",
        argv[3]);
    return 0;
}
