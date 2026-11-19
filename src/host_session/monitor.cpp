#include <nekomote/host_session/monitor.hpp>

#include <dbus/dbus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace nekomote::host_session
{
namespace
{
constexpr const char* kScreenService = "org.freedesktop.ScreenSaver";
constexpr const char* kLoginService = "org.freedesktop.login1";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";

bool boolean(DBusMessageIter value, bool& result)
{
    if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_VARIANT)
    {
        DBusMessageIter contents;
        dbus_message_iter_recurse(&value, &contents);
        value = contents;
    }
    if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_BOOLEAN)
        return false;
    dbus_bool_t raw = false;
    dbus_message_iter_get_basic(&value, &raw);
    result = raw != 0;
    return true;
}
struct Bus
{
    DBusConnection* connection = nullptr;
    ~Bus()
    {
        if (connection)
        {
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
        }
    }
    void open(DBusBusType type, const char* service)
    {
        // libdbus may otherwise autolaunch a session bus from a bare DISPLAY.
        if (type == DBUS_BUS_SESSION && !std::getenv("DBUS_SESSION_BUS_ADDRESS"))
        {
            std::fprintf(stderr,
                         "[host_session] no session bus address; lock observation unavailable\n");
            return;
        }
        DBusError error;
        dbus_error_init(&error);
        connection = dbus_bus_get_private(type, &error);
        if (!connection)
        {
            std::fprintf(stderr, "[host_session] %s observation unavailable: %s\n", service,
                         error.message ? error.message : "no bus");
            dbus_error_free(&error);
            return;
        }
        dbus_connection_set_exit_on_disconnect(connection, false);
        const std::string match = std::string("type='signal',sender='") + service + "'";
        dbus_bus_add_match(connection, match.c_str(), &error);
        if (!dbus_error_is_set(&error))
        {
            const std::string owner =
                std::string("type='signal',sender='org.freedesktop.DBus',") +
                "interface='org.freedesktop.DBus',member='NameOwnerChanged'," + "arg0='" + service +
                "'";
            dbus_bus_add_match(connection, owner.c_str(), &error);
        }
        if (dbus_error_is_set(&error))
            std::fprintf(stderr, "[host_session] signal subscription failed: %s\n", error.message);
        dbus_error_free(&error);
    }
    dbus_uint32_t send(DBusMessage* message)
    {
        if (!message)
            return 0;
        dbus_uint32_t serial = 0;
        dbus_message_set_auto_start(message, false);
        if (connection)
            dbus_connection_send(connection, message, &serial);
        dbus_message_unref(message);
        return serial;
    }
};
} // namespace

struct Monitor::Impl
{
    Bus session, system;
    bool screen_active = false, locked = false, sleeping = false;
    dbus_uint32_t screen_query = 0, session_query = 0, locked_query = 0, sleep_query = 0;
    std::string session_path;
    bool tried_display_session = false;

    Impl()
    {
        session.open(DBUS_BUS_SESSION, kScreenService);
        system.open(DBUS_BUS_SYSTEM, kLoginService);
        query_screen();
        query_login();
    }
    void query_screen()
    {
        screen_query = session.send(dbus_message_new_method_call(kScreenService, "/ScreenSaver",
                                                                 kScreenService, "GetActive"));
    }
    dbus_uint32_t query_property(const char* path, const char* interface, const char* name)
    {
        auto* message = dbus_message_new_method_call(kLoginService, path, kProperties, "Get");
        if (message && !dbus_message_append_args(message, DBUS_TYPE_STRING, &interface,
                                                 DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
        {
            dbus_message_unref(message);
            return 0;
        }
        return system.send(message);
    }
    void query_login()
    {
        session_path.clear();
        tried_display_session = false;
        auto* message =
            dbus_message_new_method_call(kLoginService, "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager", "GetSessionByPID");
        const dbus_uint32_t pid = 0; // logind resolves the sender's session.
        if (message &&
            !dbus_message_append_args(message, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID))
        {
            dbus_message_unref(message);
            message = nullptr;
        }
        session_query = system.send(message);
        locked_query = 0;
        sleep_query = query_property("/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                                     "PreparingForSleep");
    }
    void receive(DBusMessage* message, bool is_system)
    {
        DBusMessageIter args;
        if (!dbus_message_iter_init(message, &args))
            return;
        if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
        {
            if (is_system)
            {
                locked = sleeping = false;
                query_login();
            }
            else
            {
                screen_active = false;
                query_screen();
            }
            return;
        }
        const auto reply = dbus_message_get_reply_serial(message);
        // systemd user units are outside the session scope even though niri imported
        // the graphical session ID. Observe that session, never guess a different seat.
        if (is_system && reply && reply == session_query && !tried_display_session &&
            dbus_message_is_error(message, "org.freedesktop.login1.NoSessionForPID"))
        {
            tried_display_session = true;
            if (const char* id = std::getenv("XDG_SESSION_ID"); id && *id)
            {
                auto* query =
                    dbus_message_new_method_call(kLoginService, "/org/freedesktop/login1",
                                                 "org.freedesktop.login1.Manager", "GetSession");
                if (query &&
                    dbus_message_append_args(query, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
                {
                    session_query = system.send(query);
                    return;
                }
                if (query)
                    dbus_message_unref(query);
            }
        }
        if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR && reply &&
            ((!is_system && reply == screen_query) || (is_system && reply == session_query)))
            std::fprintf(stderr, "[host_session] %s observation unavailable: %s\n",
                         is_system ? "logind session" : "ScreenSaver",
                         dbus_message_get_error_name(message));
        if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
        {
            if (!is_system && reply && reply == screen_query)
                boolean(args, screen_active);
            else if (is_system && reply && reply == sleep_query)
                boolean(args, sleeping);
            else if (is_system && reply && reply == locked_query)
                boolean(args, locked);
            else if (is_system && reply && reply == session_query &&
                     dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_OBJECT_PATH)
            {
                const char* path = nullptr;
                dbus_message_iter_get_basic(&args, &path);
                session_path = path;
                locked_query = query_property(path, "org.freedesktop.login1.Session", "LockedHint");
            }
            return;
        }
        if (!is_system && dbus_message_is_signal(message, kScreenService, "ActiveChanged"))
        {
            screen_query = 0; // A delayed initial reply cannot overwrite this newer signal.
            boolean(args, screen_active);
        }
        else if (is_system && dbus_message_is_signal(message, "org.freedesktop.login1.Manager",
                                                     "PrepareForSleep"))
        {
            sleep_query = 0;
            boolean(args, sleeping);
        }
        else if (is_system && !session_path.empty() &&
                 dbus_message_is_signal(message, kProperties, "PropertiesChanged") &&
                 dbus_message_has_path(message, session_path.c_str()))
        {
            // Re-query on invalidation too. No property cache is trusted after a signal.
            locked_query = query_property(session_path.c_str(), "org.freedesktop.login1.Session",
                                          "LockedHint");
        }
    }
    void dispatch(Bus& bus, bool is_system)
    {
        if (!bus.connection)
            return;
        if (!dbus_connection_read_write(bus.connection, 0))
        {
            if (is_system)
                locked = sleeping = false;
            else
                screen_active = false;
            return;
        }
        while (auto* message = dbus_connection_pop_message(bus.connection))
        {
            receive(message, is_system);
            dbus_message_unref(message);
        }
    }
    bool poll()
    {
        dispatch(session, false);
        dispatch(system, true);
        return screen_active || locked || sleeping;
    }
};
Monitor::Monitor() : impl_(std::make_unique<Impl>())
{
}
Monitor::~Monitor() = default;
bool Monitor::poll()
{
    return impl_->poll();
}
} // namespace nekomote::host_session
