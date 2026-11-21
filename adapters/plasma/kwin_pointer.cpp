#include "kwin_pointer.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QStandardPaths>

namespace nekomote::plasma
{
namespace
{
const auto kKWin = QStringLiteral("org.kde.KWin");
const auto kService = QStringLiteral("uk.nekopara.NekoMote.Gaze");
const auto kObject = QStringLiteral("/Gaze");
const auto kScript = QStringLiteral("uk.nekopara.nekomote.gaze");

QDBusPendingCall call_scripting(const QString& owner, const QString& method,
                                const QVariantList& args)
{
    auto message = QDBusMessage::createMethodCall(owner, QStringLiteral("/Scripting"),
                                                  QStringLiteral("org.kde.kwin.Scripting"), method);
    message.setArguments(args);
    return QDBusConnection::sessionBus().asyncCall(message, 3000);
}
} // namespace

KWinPointer::KWinPointer(QObject* parent) : QObject(parent)
{
    script_path_ = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/wallpapers/uk.nekopara.nekomote/contents/kwin/gaze.js"));
    if (script_path_.isEmpty())
        return;

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() ||
        !bus.registerObject(kObject, this, QDBusConnection::ExportScriptableSlots))
        return;
    registered_ = bus.registerService(kService);
    if (!registered_)
    {
        bus.unregisterObject(kObject);
        return;
    }

    auto* watcher =
        new QDBusServiceWatcher(kKWin, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner)
            {
                ++generation_;
                owner_ = owner;
                pending_ = false;
                loaded_ = false;
                position_.reset();
                start_requested_ = users_ != 0;
                synchronize();
            });
    owner_ = bus.interface()->serviceOwner(kKWin).value();
}

KWinPointer::~KWinPointer()
{
    if (!owner_.isEmpty() && (loaded_ || pending_))
        call_scripting(owner_, QStringLiteral("unloadScript"), {kScript});
    if (registered_)
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterService(kService);
        bus.unregisterObject(kObject);
    }
}

void KWinPointer::subscribe()
{
    if (++users_ == 1)
    {
        start_requested_ = true;
        synchronize();
    }
}

void KWinPointer::unsubscribe()
{
    if (--users_ == 0)
    {
        position_.reset();
        start_requested_ = false;
        synchronize();
    }
}

void KWinPointer::Update(int x, int y)
{
    if (users_ && calledFromDBus() && message().service() == owner_)
        position_ = QPointF(x, y);
}

void KWinPointer::synchronize()
{
    if (!registered_ || pending_ || owner_.isEmpty())
        return;
    if (loaded_ && !users_)
        unload();
    else if (!loaded_ && start_requested_)
    {
        start_requested_ = false;
        // Remove any script left by a previous plasmashell crash before loading.
        unload();
    }
}

void KWinPointer::unload()
{
    pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        call_scripting(owner_, QStringLiteral("unloadScript"), {kScript}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation = generation_](QDBusPendingCallWatcher* reply)
            {
                reply->deleteLater();
                if (generation != generation_)
                    return;
                pending_ = false;
                loaded_ = false;
                if (users_)
                {
                    start_requested_ = false;
                    start();
                }
            });
}

void KWinPointer::start()
{
    pending_ = true;
    auto* watcher = new QDBusPendingCallWatcher(
        call_scripting(owner_, QStringLiteral("loadScript"), {script_path_, kScript}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation = generation_](QDBusPendingCallWatcher* reply)
            {
                const QDBusPendingReply<int> result = *reply;
                reply->deleteLater();
                if (generation != generation_)
                    return;
                pending_ = false;
                loaded_ = !result.isError() && result.value() >= 0;
                if (loaded_ && users_)
                {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    const auto path = QStringLiteral("/") + QString::number(result.value());
#else
                    const auto path = QStringLiteral("/Scripting/Script") +
                                      QString::number(result.value());
#endif
                    const auto run = QDBusMessage::createMethodCall(
                        owner_, path, QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
                    QDBusConnection::sessionBus().asyncCall(run, 3000);
                }
                synchronize();
            });
}
} // namespace nekomote::plasma
