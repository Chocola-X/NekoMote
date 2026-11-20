#include <nekomote/host_session/monitor.hpp>

#include <dbus/dbus.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>

namespace nekomote::host_session
{
namespace
{
constexpr auto screen = "org.freedesktop.ScreenSaver";
constexpr auto login = "org.freedesktop.login1";
constexpr auto root = "/org/freedesktop/login1";
constexpr auto manager = "org.freedesktop.login1.Manager";
constexpr auto session_iface = "org.freedesktop.login1.Session";
constexpr auto props = DBUS_INTERFACE_PROPERTIES;

// The raw libdbus serial API has no built-in timeout. A late snapshot must not
// authorize; a new owner/notification can start a fresh query.
struct Query
{
    dbus_uint32_t serial = 0;
    std::chrono::steady_clock::time_point deadline{};
    bool matches(dbus_uint32_t reply) const
    {
        return reply && reply == serial && std::chrono::steady_clock::now() < deadline;
    }
};

// Keep last playback indication while refreshing, but never authorize on it.
struct Observation
{
    bool active = false, known = false;
    Query pending;
    void refresh(Query query = {})
    {
        known = false;
        pending = query;
    }
    void update(DBusMessage* message, const char* signature)
    {
        refresh();
        if (!dbus_message_has_signature(message, signature))
            return;
        DBusMessageIter args, variant;
        dbus_message_iter_init(message, &args);
        if (*signature == 'v')
        {
            dbus_message_iter_recurse(&args, &variant);
            args = variant;
        }
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_BOOLEAN)
            return;
        dbus_bool_t value;
        dbus_message_iter_get_basic(&args, &value);
        active = value != 0;
        known = true;
    }
    bool clear() const
    {
        return known && !active;
    }
};

struct Bus
{
    const char* service;
    DBusConnection* connection = nullptr;
    std::string owner;
    Query lookup;
    bool initial_lookup = true;
    // Directed messages need not satisfy our match rule. Bound the unverified
    // input kept across the initial owner lookup; overflow denies this epoch.
    std::set<std::pair<std::string, std::string>> early_vetoes;
    bool overflow = false;
    void stage(const char* sender, const char* path, bool veto)
    {
        if (!sender || !path)
            return;
        if (std::strlen(path) > 4096)
        {
            overflow = true;
            return;
        }
        const auto key = std::make_pair(std::string(sender), std::string(path));
        if (!veto)
            early_vetoes.erase(key);
        else if (early_vetoes.size() < 64)
            early_vetoes.insert(key);
        else
            overflow = true;
    }
    explicit Bus(const char* name) : service(name)
    {
    }
    ~Bus()
    {
        close();
    }
    void close()
    {
        if (connection)
        {
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
            connection = nullptr;
        }
        owner.clear();
        lookup = {};
    }
    template <typename... Args>
    Query call(const char* destination, const char* path, const char* iface, const char* method,
               Args... args)
    {
        auto* msg = dbus_message_new_method_call(destination, path, iface, method);
        if (!msg)
            return {};
        dbus_uint32_t serial = 0;
        dbus_message_set_auto_start(msg, false);
        if (dbus_message_append_args(msg, args..., DBUS_TYPE_INVALID))
            dbus_connection_send(connection, msg, &serial);
        dbus_message_unref(msg);
        return {serial, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
    }
    void open(DBusBusType type)
    {
        if (type == DBUS_BUS_SESSION && !std::getenv("DBUS_SESSION_BUS_ADDRESS"))
            return;
        DBusError error = DBUS_ERROR_INIT;
        connection = dbus_bus_get_private(type, &error);
        if (connection)
        {
            dbus_connection_set_exit_on_disconnect(connection, false);
            auto match = std::string("type='signal',sender='") + service + "'";
            dbus_bus_add_match(connection, match.c_str(), &error);
            match = std::string("type='signal',sender='org.freedesktop.DBus',") +
                    "interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" + service +
                    "'";
            if (!dbus_error_is_set(&error))
                dbus_bus_add_match(connection, match.c_str(), &error);
        }
        if (dbus_error_is_set(&error))
        {
            std::fprintf(stderr, "[host_session] %s unavailable: %s\n", service, error.message);
            dbus_error_free(&error);
            close();
        }
    }
    void lookup_owner()
    {
        lookup = call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner",
                      DBUS_TYPE_STRING, &service);
    }
    int fd() const
    {
        int descriptor = -1;
        return connection && dbus_connection_get_unix_fd(connection, &descriptor) ? descriptor : -1;
    }
};

// Only refresh for a changed/invalidated key on its own interface.
bool touches(DBusMessage* msg, const char* iface, const char* property,
             bool* reported_true = nullptr)
{
    if (!dbus_message_has_signature(msg, "sa{sv}as"))
        return true; // Malformed trusted notification: discard the snapshot.
    DBusMessageIter args, entries;
    dbus_message_iter_init(msg, &args);
    const char* name;
    dbus_message_iter_get_basic(&args, &name);
    if (std::strcmp(name, iface))
        return false;
    while (dbus_message_iter_next(&args))
    {
        dbus_message_iter_recurse(&args, &entries);
        while (dbus_message_iter_get_arg_type(&entries) != DBUS_TYPE_INVALID)
        {
            DBusMessageIter key = entries;
            if (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
                dbus_message_iter_recurse(&entries, &key);
            dbus_message_iter_get_basic(&key, &name);
            if (!std::strcmp(name, property))
            {
                if (reported_true &&
                    dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
                {
                    dbus_message_iter_next(&key);
                    DBusMessageIter contents;
                    dbus_message_iter_recurse(&key, &contents);
                    if (dbus_message_iter_get_arg_type(&contents) == DBUS_TYPE_BOOLEAN)
                    {
                        dbus_bool_t value;
                        dbus_message_iter_get_basic(&contents, &value);
                        *reported_true = value != 0;
                    }
                }
                return true;
            }
            dbus_message_iter_next(&entries);
        }
    }
    return false;
}
} // namespace

struct Monitor::Impl
{
    Bus desktop{screen}, system{login};
    Observation saver, sleep, locked, active;
    bool saver_optional = false, alternate_path = false, saver_signal = false;
    std::string path;
    Query resolving, confirming;
    bool verified = false, display_session = false;
    // Requests may arrive while GetSession is still in flight (no bound path yet).
    std::set<std::string> requested_locks;

    Impl()
    {
        desktop.open(DBUS_BUS_SESSION);
        system.open(DBUS_BUS_SYSTEM);
    }
    void property(Observation& obs, const char* object, const char* iface, const char* key)
    {
        obs.refresh(system.call(system.owner.c_str(), object, props, "Get", DBUS_TYPE_STRING,
                                &iface, DBUS_TYPE_STRING, &key));
    }
    void resolve()
    {
        path.clear();
        locked.refresh();
        active.refresh();
        verified = false;
        confirming = {};
        display_session = false;
        const dbus_uint32_t pid = 0;
        resolving = system.call(system.owner.c_str(), root, manager, "GetSessionByPID",
                                DBUS_TYPE_UINT32, &pid);
    }
    void set_owner(Bus& bus, const char* owner)
    {
        bus.early_vetoes.clear();
        bus.overflow = false;
        bus.lookup = {};
        bus.owner = owner;
        if (&bus == &desktop)
        {
            saver = {};
            saver_optional = alternate_path = saver_signal = false;
            if (*owner)
                saver.refresh(bus.call(owner, "/ScreenSaver", screen, "GetActive"));
        }
        else
        {
            sleep = locked = active = {};
            requested_locks.clear();
            path.clear();
            resolving = confirming = {};
            verified = false;
            if (*owner)
            {
                resolve();
                property(sleep, root, manager, "PreparingForSleep");
            }
        }
    }
    void receive(Bus& bus, DBusMessage* msg)
    {
        const auto reply = dbus_message_get_reply_serial(msg);
        const bool result = dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_RETURN;
        if (dbus_message_has_sender(msg, DBUS_SERVICE_DBUS))
        {
            const char *name, *old_owner, *new_owner;
            if (dbus_message_has_path(msg, DBUS_PATH_DBUS) &&
                dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
                dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING,
                                      &old_owner, DBUS_TYPE_STRING, &new_owner,
                                      DBUS_TYPE_INVALID) &&
                !std::strcmp(name, bus.service))
            {
                set_owner(bus, new_owner);
                // Loss revokes first; distinguish confirmed absence from a failed lookup.
                if (&bus == &desktop && !*new_owner)
                    bus.lookup_owner();
            }
            else if (bus.lookup.matches(reply) && result &&
                     dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &new_owner,
                                           DBUS_TYPE_INVALID))
            {
                // Replay only the vetoes authenticated by this daemon response.
                // NameOwnerChanged instead starts a fresh epoch and discards them.
                auto early = std::move(bus.early_vetoes);
                const bool overflow = bus.overflow;
                set_owner(bus, new_owner);
                bus.overflow = overflow;
                for (const auto& veto : early)
                    if (veto.first == bus.owner)
                    {
                        if (&bus == &system)
                            requested_locks.insert(veto.second);
                        else
                            saver.active = true;
                    }
            }
            else if (&bus == &desktop && bus.lookup.matches(reply) &&
                     dbus_message_is_error(msg, DBUS_ERROR_NAME_HAS_NO_OWNER))
            {
                bus.lookup = {};
                saver_optional = true;
            }
            return;
        }
        if (bus.owner.empty())
        {
            if (bus.lookup.serial)
            {
                const auto* sender = dbus_message_get_sender(msg);
                if (&bus == &system && (dbus_message_is_signal(msg, session_iface, "Lock") ||
                                        dbus_message_is_signal(msg, session_iface, "Unlock")))
                    bus.stage(sender, dbus_message_get_path(msg),
                              dbus_message_is_signal(msg, session_iface, "Lock"));
                else if (&bus == &desktop &&
                         (dbus_message_has_path(msg, "/ScreenSaver") ||
                          dbus_message_has_path(msg, "/org/freedesktop/ScreenSaver")) &&
                         dbus_message_is_signal(msg, screen, "ActiveChanged"))
                {
                    Observation signal;
                    signal.update(msg, "b");
                    bus.stage(sender, "/ScreenSaver", !signal.clear());
                }
            }
            return;
        }
        if (!dbus_message_has_sender(msg, bus.owner.c_str()))
            return;
        if (&bus == &desktop)
        {
            if (saver.pending.matches(reply))
            {
                saver.pending = {};
                if (result)
                {
                    Observation snapshot;
                    snapshot.update(msg, "b");
                    if (!saver_signal)
                        saver.active = snapshot.active;
                    saver.known = snapshot.known;
                }
                else if (dbus_message_is_error(msg, DBUS_ERROR_UNKNOWN_METHOD))
                {
                    if (alternate_path)
                        saver_optional = true;
                    else
                    {
                        alternate_path = true;
                        saver.refresh(bus.call(bus.owner.c_str(), "/org/freedesktop/ScreenSaver",
                                               screen, "GetActive"));
                    }
                }
            }
            else if ((dbus_message_has_path(msg, "/ScreenSaver") ||
                      dbus_message_has_path(msg, "/org/freedesktop/ScreenSaver")) &&
                     dbus_message_is_signal(msg, screen, "ActiveChanged"))
            {
                Observation signal;
                signal.update(msg, "b");
                saver_signal = true;
                // Preserve query readiness independently of the newer playback indication.
                saver.active = !signal.known || signal.active;
            }
            return;
        }
        if (resolving.matches(reply))
        {
            resolving = {};
            const char* object;
            if (result && dbus_message_has_signature(msg, "o") &&
                dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &object,
                                      DBUS_TYPE_INVALID))
            {
                path = object;
                verified = !display_session;
                if (display_session)
                {
                    const char* automatic = "auto";
                    confirming = system.call(system.owner.c_str(), root, manager, "GetSession",
                                             DBUS_TYPE_STRING, &automatic);
                }
                property(locked, object, session_iface, "LockedHint");
                property(active, object, session_iface, "Active");
            }
            else if (!display_session &&
                     dbus_message_is_error(msg, "org.freedesktop.login1.NoSessionForPID"))
            {
                // systemd user services need not belong to a session scope. Let logind
                // resolve the caller's display session; XDG_SESSION_ID may be stale.
                display_session = true;
                const char* id = std::getenv("XDG_SESSION_ID");
                if (id && *id && dbus_validate_utf8(id, nullptr) && std::strcmp(id, "auto") &&
                    std::strcmp(id, "self"))
                    resolving = system.call(system.owner.c_str(), root, manager, "GetSession",
                                            DBUS_TYPE_STRING, &id);
            }
        }
        else if (confirming.matches(reply))
        {
            confirming = {};
            const char* automatic;
            verified = result && dbus_message_has_signature(msg, "o") &&
                       dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &automatic,
                                             DBUS_TYPE_INVALID) &&
                       path == automatic;
        }
        else if (result && sleep.pending.matches(reply))
        {
            const bool was_sleeping = sleep.active;
            sleep.update(msg, "v");
            if (was_sleeping && sleep.clear())
                resolve();
        }
        else if (result && active.pending.matches(reply))
            active.update(msg, "v");
        else if (result && locked.pending.matches(reply))
        {
            locked.update(msg, "v");
            if (locked.known && locked.active)
                requested_locks.erase(path); // The observed lock now holds the veto itself.
        }
        else if (dbus_message_has_path(msg, root) &&
                 dbus_message_is_signal(msg, manager, "PrepareForSleep"))
        {
            sleep.update(msg, "b");
            if (sleep.clear())
                resolve(); // Do not reuse the pre-suspend session snapshot.
        }
        else if (dbus_message_has_path(msg, root) &&
                 (dbus_message_is_signal(msg, manager, "SessionRemoved") ||
                  dbus_message_is_signal(msg, manager, "SessionNew")))
        {
            const char *id, *object;
            if (path.empty() || display_session ||
                !dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &id, DBUS_TYPE_OBJECT_PATH,
                                       &object, DBUS_TYPE_INVALID) ||
                path == object)
                resolve();
        }
        else if ((path.empty() || dbus_message_has_path(msg, path.c_str())) &&
                 (dbus_message_is_signal(msg, session_iface, "Lock") ||
                  dbus_message_is_signal(msg, session_iface, "Unlock")))
        {
            const char* object = dbus_message_get_path(msg);
            if (dbus_message_is_signal(msg, session_iface, "Lock"))
                requested_locks.insert(object);
            else
                requested_locks.erase(object);
            // A request is not a playback indication. Ignore pre-request replies.
            if (!path.empty())
                property(locked, object, session_iface, "LockedHint");
        }
        else if (!path.empty() && dbus_message_has_path(msg, path.c_str()))
        {
            if (dbus_message_is_signal(msg, props, "PropertiesChanged"))
            {
                bool reported_locked = false;
                if (touches(msg, session_iface, "LockedHint", &reported_locked))
                {
                    if (reported_locked)
                    {
                        locked.active = true;
                        requested_locks.erase(path);
                    }
                    property(locked, path.c_str(), session_iface, "LockedHint");
                }
                if (touches(msg, session_iface, "Active"))
                    property(active, path.c_str(), session_iface, "Active");
            }
        }
        else if (dbus_message_is_signal(msg, props, "PropertiesChanged"))
        {
            if (dbus_message_has_path(msg, root) && touches(msg, manager, "PreparingForSleep"))
                property(sleep, root, manager, "PreparingForSleep");
            else if (display_session && touches(msg, "org.freedesktop.login1.User", "Display"))
                resolve();
        }
    }
    void dispatch(Bus& bus)
    {
        if (!bus.connection)
            return;
        if (bus.initial_lookup)
        {
            bus.initial_lookup = false;
            bus.lookup_owner();
        }
        if (!dbus_connection_read_write(bus.connection, 0))
        {
            set_owner(bus, "");
            bus.close();
            return;
        }
        while (auto* message = dbus_connection_pop_message(bus.connection))
        {
            receive(bus, message);
            dbus_message_unref(message);
        }
    }
};
Monitor::Monitor() : impl_(std::make_unique<Impl>())
{
}
Monitor::~Monitor() = default;
bool Monitor::poll()
{
    impl_->dispatch(impl_->desktop);
    impl_->dispatch(impl_->system);
    return impl_->saver.active || impl_->locked.active || impl_->sleep.active;
}
bool Monitor::desktop_gaze_allowed() const
{
    return !impl_->system.overflow && !impl_->desktop.overflow && impl_->verified &&
           impl_->active.known && impl_->active.active && impl_->locked.clear() &&
           impl_->sleep.clear() && !impl_->requested_locks.count(impl_->path) &&
           (impl_->saver_optional || impl_->saver.known) && !impl_->saver.active;
}
std::array<int, 2> Monitor::dbus_fds() const
{
    return {impl_->desktop.fd(), impl_->system.fd()};
}
} // namespace nekomote::host_session
