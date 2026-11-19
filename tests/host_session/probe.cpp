#include <nekomote/host_session/monitor.hpp>

#include <dbus/dbus.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
void set_state(const char* action)
{
    const auto* owned = std::getenv("NEKOMOTE_TEST_SESSION_BUS");
    const auto* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!owned || !address || std::strcmp(owned, address))
        throw std::runtime_error("control requires an explicitly owned private bus");
    DBusError error;
    dbus_error_init(&error);
    auto* bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!bus)
        throw std::runtime_error("cannot connect to private bus");
    auto* request = dbus_message_new_method_call("org.freedesktop.ScreenSaver", "/ScreenSaver",
                                                 "nekomote.Test", "Set");
    dbus_message_set_auto_start(request, false);
    dbus_message_append_args(request, DBUS_TYPE_STRING, &action, DBUS_TYPE_INVALID);
    auto* reply = dbus_connection_send_with_reply_and_block(bus, request, 1000, &error);
    dbus_message_unref(request);
    const bool success = reply != nullptr;
    if (reply)
        dbus_message_unref(reply);
    dbus_error_free(&error);
    dbus_connection_close(bus);
    dbus_connection_unref(bus);
    if (!success)
        throw std::runtime_error("private session control failed");
}
void serve()
{
    const auto* owned = std::getenv("NEKOMOTE_TEST_SESSION_BUS");
    const auto* session = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const auto* system = std::getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (!owned || !session || !system || std::strcmp(owned, session) || std::strcmp(owned, system))
        throw std::runtime_error("test server requires an explicitly owned private bus");
    DBusError error;
    dbus_error_init(&error);
    auto* bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!bus)
        throw std::runtime_error("cannot connect to private bus");
    if (dbus_bus_request_name(bus, "org.freedesktop.ScreenSaver", DBUS_NAME_FLAG_DO_NOT_QUEUE,
                              &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        dbus_bus_request_name(bus, "org.freedesktop.login1", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
            DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        throw std::runtime_error("test service names already owned");
    dbus_bool_t active = false, sleeping = false, locked = false;
    while (dbus_connection_read_write(bus, 100))
    {
        auto* message = dbus_connection_pop_message(bus);
        if (!message)
            continue;
        DBusMessage* reply = nullptr;
        if (dbus_message_is_method_call(message, "nekomote.Test", "Set"))
        {
            const char* action = nullptr;
            if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &action,
                                      DBUS_TYPE_INVALID))
            {
                const std::string value(action);
                active = value == "lock" ? true : value == "unlock" ? false : active;
                sleeping = value == "sleep" ? true : value == "wake" ? false : sleeping;
                locked = value == "hint-lock" ? true : value == "hint-unlock" ? false : locked;
                auto* signal = dbus_message_new_signal(
                    "/ScreenSaver", "org.freedesktop.ScreenSaver", "ActiveChanged");
                dbus_message_append_args(signal, DBUS_TYPE_BOOLEAN, &active, DBUS_TYPE_INVALID);
                dbus_connection_send(bus, signal, nullptr);
                dbus_message_unref(signal);
                signal = dbus_message_new_signal(
                    "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "PrepareForSleep");
                dbus_message_append_args(signal, DBUS_TYPE_BOOLEAN, &sleeping, DBUS_TYPE_INVALID);
                dbus_connection_send(bus, signal, nullptr);
                dbus_message_unref(signal);
                // A property invalidation is enough: observers must query LockedHint again.
                signal =
                    dbus_message_new_signal("/org/freedesktop/login1/session/test",
                                            "org.freedesktop.DBus.Properties", "PropertiesChanged");
                const char* interface = "org.freedesktop.login1.Session";
                DBusMessageIter args, changed, invalidated;
                dbus_message_iter_init_append(signal, &args);
                dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface);
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &changed);
                dbus_message_iter_close_container(&args, &changed);
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &invalidated);
                const char* property = "LockedHint";
                dbus_message_iter_append_basic(&invalidated, DBUS_TYPE_STRING, &property);
                dbus_message_iter_close_container(&args, &invalidated);
                dbus_connection_send(bus, signal, nullptr);
                dbus_message_unref(signal);
                reply = dbus_message_new_method_return(message);
            }
        }
        else if (dbus_message_is_method_call(message, "org.freedesktop.ScreenSaver", "GetActive"))
        {
            reply = dbus_message_new_method_return(message);
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &active, DBUS_TYPE_INVALID);
        }
        else if (dbus_message_is_method_call(message, "org.freedesktop.login1.Manager",
                                             "GetSessionByPID"))
        {
            // Model a process started by systemd --user, outside the session scope.
            reply = dbus_message_new_error(message, "org.freedesktop.login1.NoSessionForPID",
                                           "No session for test client");
        }
        else if (dbus_message_is_method_call(message, "org.freedesktop.login1.Manager",
                                             "GetSession"))
        {
            const char* id = nullptr;
            if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID) &&
                std::strcmp(id, "test") == 0)
            {
                const char* path = "/org/freedesktop/login1/session/test";
                reply = dbus_message_new_method_return(message);
                dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
            }
        }
        else if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get"))
        {
            const char* interface = nullptr;
            const char* name = nullptr;
            if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &interface,
                                      DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
            {
                const dbus_bool_t value = std::strcmp(name, "LockedHint") == 0 ? locked : sleeping;
                reply = dbus_message_new_method_return(message);
                DBusMessageIter args, variant;
                dbus_message_iter_init_append(reply, &args);
                dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
                dbus_message_iter_close_container(&args, &variant);
            }
        }
        if (reply)
        {
            dbus_connection_send(bus, reply, nullptr);
            dbus_message_unref(reply);
        }
        dbus_message_unref(message);
    }
    dbus_connection_close(bus);
    dbus_connection_unref(bus);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::strcmp(argv[1], "--set") == 0)
        {
            set_state(argv[2]);
            return 0;
        }
        if (argc == 2 && std::strcmp(argv[1], "--serve") == 0)
        {
            serve();
            return 0;
        }
        nekomote::host_session::Monitor monitor;
        int previous = -1;
        while (true)
        {
            const int unavailable = monitor.poll() ? 1 : 0;
            if (unavailable != previous)
            {
                std::cout << unavailable << std::endl;
                previous = unavailable;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
