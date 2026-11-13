#include "kwin_pointer_provider.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPoint>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <optional>

#include "pointer_geometry.hpp"

namespace nekomote::plasma
{
namespace
{
constexpr auto kServiceName = "org.kde.nekomote.Pointer";
constexpr auto kObjectPath = "/NekoMote/Pointer";
constexpr auto kScriptName = "nekomote-pointer";

const char* const kKWinScript = R"JS(
(function () {
    var service = "org.kde.nekomote.Pointer";
    var path = "/NekoMote/Pointer";
    var iface = "org.kde.nekomote.Pointer";
    var lastCursor = 0;

    function now() {
        return (typeof Date.now === "function") ? Date.now() : new Date().getTime();
    }

    function emitCursor() {
        var t = now();
        if (t - lastCursor < 16) return;
        lastCursor = t;
        var p = workspace.cursorPos;
        callDBus(service, path, iface, "update", p.x, p.y);
    }

    function rectContains(rects, x, y, w, h) {
        for (var i = 0; i < rects.length; ++i) {
            var r = rects[i];
            if (x >= r.x && y >= r.y && x + w <= r.x + r.width && y + h <= r.y + r.height)
                return true;
        }
        return false;
    }

    function coverage(rects, out) {
        var xs = [out.x, out.x + out.width];
        var ys = [out.y, out.y + out.height];
        for (var i = 0; i < rects.length; ++i) {
            xs.push(rects[i].x, rects[i].x + rects[i].width);
            ys.push(rects[i].y, rects[i].y + rects[i].height);
        }
        xs.sort(function (a, b) { return a - b; });
        ys.sort(function (a, b) { return a - b; });
        var ux = [], uy = [];
        for (var i = 0; i < xs.length; ++i) if (i === 0 || xs[i] !== xs[i - 1]) ux.push(xs[i]);
        for (var i = 0; i < ys.length; ++i) if (i === 0 || ys[i] !== ys[i - 1]) uy.push(ys[i]);
        var area = 0;
        for (var i = 0; i + 1 < ux.length; ++i) {
            for (var j = 0; j + 1 < uy.length; ++j) {
                var x = ux[i], y = uy[j];
                var w = ux[i + 1] - x, h = uy[j + 1] - y;
                if (w > 0 && h > 0 && rectContains(rects, x, y, w, h))
                    area += w * h;
            }
        }
        return out.width > 0 && out.height > 0 ? area / (out.width * out.height) : 0;
    }

    function activityVisible(w) {
        if (w.onAllDesktops) return true;
        var acts = w.activities || [];
        if (acts.length === 0) return true;
        return acts.indexOf(workspace.currentActivity) >= 0;
    }

    function desktopVisible(w) {
        if (w.onAllDesktops) return true;
        var desks = w.desktops || [];
        if (desks.length === 0) return true;
        var current = workspace.currentDesktop ? workspace.currentDesktop.id : "";
        for (var i = 0; i < desks.length; ++i)
            if (desks[i].id === current) return true;
        return false;
    }

    function recompute() {
        var list = workspace.windowList();
        var outputs = {};
        for (var i = 0; i < list.length; ++i) {
            var w = list[i];
            if (w.output && w.output.name) outputs[w.output.name] = w.output;
        }
        var names = [];
        for (var name in outputs) names.push(name);
        for (var n = 0; n < names.length; ++n) {
            var output = outputs[names[n]];
            var geom = output.geometry;
            var rects = [];
            for (var i = 0; i < list.length; ++i) {
                var w = list[i];
                if (!w.output || w.output.name !== output.name) continue;
                if (w.desktopWindow) continue;
                if (w.minimized || w.hidden || w.deleted) continue;
                if (w.opacity !== undefined && w.opacity < 1) continue;
                if (!w.dock && (!activityVisible(w) || !desktopVisible(w))) continue;
                var g = w.frameGeometry;
                if (!g) continue;
                if (w.resourceClass === "plasmashell" && w.normalWindow === false &&
                    g.x <= geom.x && g.y <= geom.y &&
                    g.x + g.width >= geom.x + geom.width &&
                    g.y + g.height >= geom.y + geom.height)
                    continue;
                var l = Math.max(g.x, geom.x), t = Math.max(g.y, geom.y);
                var r = Math.min(g.x + g.width, geom.x + geom.width);
                var b = Math.min(g.y + g.height, geom.y + geom.height);
                if (l < r && t < b)
                    rects.push({x: l, y: t, width: r - l, height: b - t});
            }
            var covered = coverage(rects, geom) >= 0.995;
            callDBus(service, path, iface, "setCovered", output.name, covered);
        }
    }

    function connectWindow(w) {
        if (!w) return;
        var signals = ["frameGeometryChanged", "minimizedChanged", "fullScreenChanged",
                       "maximizedChanged", "outputChanged", "skipTaskbarChanged",
                       "activitiesChanged", "moveResizedChanged"];
        for (var i = 0; i < signals.length; ++i)
            if (typeof w[signals[i]] === "function") w[signals[i]].connect(recompute);
    }

    if (typeof workspace.cursorPos !== "undefined") {
        workspace.cursorPosChanged.connect(emitCursor);
        emitCursor();
    }
    var windows = workspace.windowList();
    for (var i = 0; i < windows.length; ++i) connectWindow(windows[i]);
    if (typeof workspace.windowAdded === "function")
        workspace.windowAdded.connect(function (w) { connectWindow(w); recompute(); });
    if (typeof workspace.windowRemoved === "function")
        workspace.windowRemoved.connect(recompute);
    if (typeof workspace.windowActivated === "function")
        workspace.windowActivated.connect(recompute);
    if (typeof workspace.currentDesktopChanged === "function")
        workspace.currentDesktopChanged.connect(recompute);
    if (typeof workspace.currentActivityChanged === "function")
        workspace.currentActivityChanged.connect(recompute);
    recompute();
})();
)JS";

class KWinCursorBridge final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.nekomote.Pointer")
  public:
    explicit KWinCursorBridge(QObject* parent = nullptr) : QObject(parent)
    {
        timer_.setInterval(5000);
        connect(&timer_, &QTimer::timeout, this, [this] { ensure_loaded(false); });
        timer_.start();
        ensure_loaded(true);
    }

    bool has_cursor() const noexcept
    {
        return has_cursor_;
    }
    QPoint cursor() const noexcept
    {
        return cursor_;
    }

    bool covered_for(const QString& output) const
    {
        const auto it = covered_.constFind(output);
        if (it != covered_.constEnd())
            return it.value();
        // Keep the single-output case working when KWin and Qt spell the
        // connector name differently.
        if (covered_.size() == 1)
            return covered_.constBegin().value();
        return false;
    }

  public slots:
    void update(int x, int y)
    {
        cursor_ = QPoint(x, y);
        has_cursor_ = true;
    }

    void setCovered(const QString& output, bool covered)
    {
        covered_.insert(output, covered);
    }

  private:
    QString script_path()
    {
        QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (base.isEmpty())
            base = QDir::homePath() + QStringLiteral("/.cache");
        const QString dir = base + QStringLiteral("/nekomote");
        if (!QDir().mkpath(dir))
            return {};
        return dir + QStringLiteral("/kwin-pointer.js");
    }

    bool write_script(const QString& path)
    {
        if (path.isEmpty())
            return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        return file.write(kKWinScript) >= 0;
    }

    void ensure_loaded(bool force)
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            return;
        if (!registered_)
        {
            if (!bus.registerService(QString::fromLatin1(kServiceName)))
                return;
            if (!bus.registerObject(QString::fromLatin1(kObjectPath), this,
                                    QDBusConnection::ExportAllSlots))
            {
                bus.unregisterService(QString::fromLatin1(kServiceName));
                return;
            }
            registered_ = true;
        }

        const QString path = script_path();
        if (!write_script(path))
            return;
        QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                 QStringLiteral("org.kde.kwin.Scripting"), bus);
        if (!scripting.isValid())
            return;
        if (force)
            scripting.call(QStringLiteral("unloadScript"), QString::fromLatin1(kScriptName));
        const QDBusReply<bool> loaded =
            scripting.call(QStringLiteral("isScriptLoaded"), QString::fromLatin1(kScriptName));
        if (loaded.isValid() && loaded.value())
            return;
        const QDBusReply<int> script =
            scripting.call(QStringLiteral("loadScript"), path, QString::fromLatin1(kScriptName));
        if (!script.isValid())
            return;
        scripting.call(QStringLiteral("start"));
    }

    QTimer timer_;
    bool registered_{false};
    bool has_cursor_{false};
    QPoint cursor_;
    QHash<QString, bool> covered_;
};

class KWinPointerProvider final : public PointerProvider
{
  public:
    explicit KWinPointerProvider(KWinCursorBridge* bridge) : bridge_(bridge)
    {
    }

    PointerScope scope() const noexcept override
    {
        return bridge_ && bridge_->has_cursor() ? PointerScope::GlobalDesktop
                                                : PointerScope::Unavailable;
    }

    std::optional<QPointF> positionFor(const QQuickItem& item) override
    {
        auto* window = item.window();
        if (!bridge_ || !bridge_->has_cursor() || !window)
            return std::nullopt;
        const auto* screen = window->screen();
        if (!screen)
            return std::nullopt;
        // workspace.cursorPos and QScreen::geometry use the compositor's logical
        // coordinate space, so no device-pixel conversion is needed here.
        const QPoint cursor = bridge_->cursor();
        const QRect bounds = screen->geometry();
        const auto local =
            wayland_local_logical({cursor.x(), cursor.y()},
                                  {bounds.left(), bounds.top(), bounds.width(), bounds.height()},
                                  {window->width(), window->height()});
        return item.mapFromScene(QPointF(local.x, local.y));
    }

  private:
    KWinCursorBridge* bridge_;
};

class KWinOcclusionProvider final : public OcclusionProvider
{
  public:
    explicit KWinOcclusionProvider(KWinCursorBridge* bridge) : bridge_(bridge)
    {
    }

    bool fullyObscured(const QQuickItem& item) override
    {
        auto* window = item.window();
        if (!bridge_ || !window)
            return false;
        const auto* screen = window->screen();
        if (!screen)
            return false;
        return bridge_->covered_for(screen->name());
    }

  private:
    KWinCursorBridge* bridge_;
};

KWinCursorBridge* bridge_instance()
{
    static KWinCursorBridge* instance = new KWinCursorBridge(qApp);
    return instance;
}
} // namespace

std::unique_ptr<PointerProvider> make_kwin_pointer_provider()
{
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return {};
    return std::make_unique<KWinPointerProvider>(bridge_instance());
}

std::unique_ptr<OcclusionProvider> make_kwin_occlusion_provider()
{
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return {};
    return std::make_unique<KWinOcclusionProvider>(bridge_instance());
}
} // namespace nekomote::plasma

#include "kwin_pointer_provider.moc"
