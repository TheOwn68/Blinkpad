/* Minimal StatusNotifierItem + DBusMenu implementation.
 *
 * GtkStatusIcon is X11-only, so on Wayland desktops (KDE Plasma, etc.)
 * a tray icon has to be provided over D-Bus. This implements just enough
 * of org.kde.StatusNotifierItem and com.canonical.dbusmenu for a small
 * two-entry tray menu.
 */

#include "tray.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

#define SNI_PATH  "/StatusNotifierItem"
#define MENU_PATH "/MenuBar"

static GDBusConnection *bus;
static GDBusNodeInfo *sni_node;
static GDBusNodeInfo *menu_node;
static guint sni_reg;
static guint menu_reg;
static guint name_owner;
static gboolean registered = FALSE;
static gboolean attempted = FALSE;

static void (*show_cb)(void);
static void (*quit_cb)(void);

static const char *sni_xml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionMovieName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <method name='ContextMenu'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg name='delta' type='i' direction='in'/>"
    "      <arg name='orientation' type='s' direction='in'/>"
    "    </method>"
    "    <signal name='NewTitle'/>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewAttentionIcon'/>"
    "    <signal name='NewOverlayIcon'/>"
    "    <signal name='NewToolTip'/>"
    "    <signal name='NewStatus'>"
    "      <arg name='status' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static const char *menu_xml =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='TextDirection' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconThemePath' type='as' access='read'/>"
    "    <method name='GetLayout'>"
    "      <arg name='parentId' type='i' direction='in'/>"
    "      <arg name='recursionDepth' type='i' direction='in'/>"
    "      <arg name='propertyNames' type='as' direction='in'/>"
    "      <arg name='revision' type='u' direction='out'/>"
    "      <arg name='layout' type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg name='ids' type='ai' direction='in'/>"
    "      <arg name='propertyNames' type='as' direction='in'/>"
    "      <arg name='properties' type='a(ia{sv})' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='name' type='s' direction='in'/>"
    "      <arg name='value' type='v' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='eventId' type='s' direction='in'/>"
    "      <arg name='data' type='v' direction='in'/>"
    "      <arg name='timestamp' type='u' direction='in'/>"
    "    </method>"
    "    <method name='EventGroup'>"
    "      <arg name='events' type='a(isvu)' direction='in'/>"
    "      <arg name='idErrors' type='ai' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='needUpdate' type='b' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShowGroup'>"
    "      <arg name='ids' type='ai' direction='in'/>"
    "      <arg name='updatesNeeded' type='ai' direction='out'/>"
    "      <arg name='idErrors' type='ai' direction='out'/>"
    "    </method>"
    "    <signal name='ItemsPropertiesUpdated'>"
    "      <arg name='updatedProps' type='a(ia{sv})'/>"
    "      <arg name='removedProps' type='a(ias)'/>"
    "    </signal>"
    "    <signal name='LayoutUpdated'>"
    "      <arg name='revision' type='u'/>"
    "      <arg name='parent' type='i'/>"
    "    </signal>"
    "    <signal name='ItemActivationRequested'>"
    "      <arg name='id' type='i'/>"
    "      <arg name='timestamp' type='u'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GVariant *empty_pixmaps(void) {
    return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0);
}

static GVariant *menu_item(int id, const char *label) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label));
    g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));

    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));

    return g_variant_new("(ia{sv}av)", id, &props, &children);
}

/* ---------- StatusNotifierItem ---------- */

static void on_sni_method_call(GDBusConnection *conn, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *method, GVariant *params,
                               GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)params; (void)data;

    if (strcmp(method, "Activate") == 0 || strcmp(method, "SecondaryActivate") == 0) {
        if (show_cb) show_cb();
    }
    g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *on_sni_get_property(GDBusConnection *conn, const gchar *sender,
                                     const gchar *path, const gchar *iface,
                                     const gchar *prop, GError **error,
                                     gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (strcmp(prop, "Category") == 0) return g_variant_new_string("ApplicationStatus");
    if (strcmp(prop, "Id") == 0) return g_variant_new_string("blinkpad");
    if (strcmp(prop, "Title") == 0) return g_variant_new_string("Blinkpad");
    if (strcmp(prop, "Status") == 0) return g_variant_new_string("Active");
    if (strcmp(prop, "WindowId") == 0) return g_variant_new_int32(0);
    if (strcmp(prop, "IconName") == 0) return g_variant_new_string("input-keyboard");
    if (strcmp(prop, "IconPixmap") == 0) return empty_pixmaps();
    if (strcmp(prop, "OverlayIconName") == 0) return g_variant_new_string("");
    if (strcmp(prop, "OverlayIconPixmap") == 0) return empty_pixmaps();
    if (strcmp(prop, "AttentionIconName") == 0) return g_variant_new_string("");
    if (strcmp(prop, "AttentionIconPixmap") == 0) return empty_pixmaps();
    if (strcmp(prop, "AttentionMovieName") == 0) return g_variant_new_string("");
    if (strcmp(prop, "ToolTip") == 0)
        return g_variant_new("(s@a(iiay)ss)", "input-keyboard", empty_pixmaps(),
                             "Blinkpad", "Running in the background");
    if (strcmp(prop, "ItemIsMenu") == 0) return g_variant_new_boolean(FALSE);
    if (strcmp(prop, "Menu") == 0) return g_variant_new_object_path(MENU_PATH);

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown property %s", prop);
    return NULL;
}

/* ---------- DBusMenu ---------- */

static const char *item_label(int id) {
    if (id == 1) return "Show Blinkpad";
    if (id == 2) return "Quit Blinkpad";
    return NULL;
}

static void on_menu_method_call(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (strcmp(method, "GetLayout") == 0) {
        GVariantBuilder root_props;
        g_variant_builder_init(&root_props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&root_props, "{sv}", "children-display",
                              g_variant_new_string("submenu"));

        GVariantBuilder children;
        g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        g_variant_builder_add(&children, "v", menu_item(1, item_label(1)));
        g_variant_builder_add(&children, "v", menu_item(2, item_label(2)));

        GVariant *layout = g_variant_new("(ia{sv}av)", 0, &root_props, &children);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(u@(ia{sv}av))", (guint32)1, layout));
        return;
    }

    if (strcmp(method, "GetProperty") == 0) {
        gint32 id = 0;
        const gchar *name = NULL;
        g_variant_get(params, "(i&s)", &id, &name);
        const char *label = item_label(id);
        GVariant *val = NULL;
        if (label && strcmp(name, "label") == 0)
            val = g_variant_new_string(label);
        else if (label && (strcmp(name, "enabled") == 0 || strcmp(name, "visible") == 0))
            val = g_variant_new_boolean(TRUE);
        if (!val) val = g_variant_new_string("");
        g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", val));
        return;
    }

    if (strcmp(method, "GetGroupProperties") == 0) {
        GVariant *ids = g_variant_get_child_value(params, 0);
        GVariantIter iter;
        GVariantBuilder out;
        g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));
        gint32 id;
        g_variant_iter_init(&iter, ids);
        while (g_variant_iter_next(&iter, "i", &id)) {
            const char *label = item_label(id);
            if (!label) continue;
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label));
            g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
            g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
            g_variant_builder_add(&out, "(ia{sv})", id, &props);
        }
        g_variant_unref(ids);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&out)));
        return;
    }

    if (strcmp(method, "Event") == 0) {
        gint32 id = 0;
        const gchar *event_id = NULL;
        GVariant *data = NULL;
        guint32 timestamp = 0;
        g_variant_get(params, "(i&svu)", &id, &event_id, &data, &timestamp);
        if (data) g_variant_unref(data);
        if (strcmp(event_id, "clicked") == 0) {
            if (id == 1 && show_cb) show_cb();
            else if (id == 2 && quit_cb) quit_cb();
        }
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }

    if (strcmp(method, "EventGroup") == 0) {
        GVariantBuilder empty;
        g_variant_builder_init(&empty, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(@ai)", g_variant_builder_end(&empty)));
        return;
    }

    if (strcmp(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
        return;
    }

    if (strcmp(method, "AboutToShowGroup") == 0) {
        GVariantBuilder a, b;
        g_variant_builder_init(&a, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(@ai@ai)", g_variant_builder_end(&a), g_variant_builder_end(&b)));
        return;
    }

    g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *on_menu_get_property(GDBusConnection *conn, const gchar *sender,
                                      const gchar *path, const gchar *iface,
                                      const gchar *prop, GError **error,
                                      gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (strcmp(prop, "Version") == 0) return g_variant_new_uint32(3);
    if (strcmp(prop, "TextDirection") == 0) return g_variant_new_string("ltr");
    if (strcmp(prop, "Status") == 0) return g_variant_new_string("normal");
    if (strcmp(prop, "IconThemePath") == 0)
        return g_variant_new_strv(NULL, 0);

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown property %s", prop);
    return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = on_sni_method_call,
    .get_property = on_sni_get_property,
};

static const GDBusInterfaceVTable menu_vtable = {
    .method_call = on_menu_method_call,
    .get_property = on_menu_get_property,
};

/* ---------- registration ---------- */

static gboolean register_with_watcher(const char *service) {
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(bus,
        service, "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        g_variant_new("(s)", SNI_PATH),
        NULL, G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &err);
    if (!res) {
        if (err) g_error_free(err);
        return FALSE;
    }
    g_variant_unref(res);
    return TRUE;
}

gboolean tray_ensure(void) {
    if (registered) return TRUE;
    if (attempted) return FALSE;
    attempted = TRUE;

    GError *err = NULL;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        if (err) g_error_free(err);
        return FALSE;
    }

    sni_node = g_dbus_node_info_new_for_xml(sni_xml, &err);
    menu_node = g_dbus_node_info_new_for_xml(menu_xml, &err);
    if (!sni_node || !menu_node) {
        if (err) g_error_free(err);
        return FALSE;
    }

    sni_reg = g_dbus_connection_register_object(bus, SNI_PATH,
        sni_node->interfaces[0], &sni_vtable, NULL, NULL, &err);
    if (!sni_reg) {
        if (err) g_error_free(err);
        return FALSE;
    }
    menu_reg = g_dbus_connection_register_object(bus, MENU_PATH,
        menu_node->interfaces[0], &menu_vtable, NULL, NULL, &err);
    if (!menu_reg) {
        if (err) g_error_free(err);
        return FALSE;
    }

    char name[64];
    snprintf(name, sizeof(name), "org.kde.StatusNotifierItem-%d-1", (int)getpid());
    name_owner = g_bus_own_name_on_connection(bus, name,
        G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);

    gboolean kde_ok = register_with_watcher("org.kde.StatusNotifierWatcher");
    gboolean fd_ok = kde_ok ? FALSE : register_with_watcher("org.freedesktop.StatusNotifierWatcher");
    if (kde_ok || fd_ok) {
        registered = TRUE;
        return TRUE;
    }
    /* No StatusNotifier host: undo the registration so we can retry later. */
    g_dbus_connection_unregister_object(bus, sni_reg);
    g_dbus_connection_unregister_object(bus, menu_reg);
    if (name_owner) g_bus_unown_name(name_owner);
    name_owner = 0;
    attempted = FALSE;
    return FALSE;
}

void tray_init(gpointer window, void (*on_show)(void), void (*on_quit)(void)) {
    (void)window;
    show_cb = on_show;
    quit_cb = on_quit;
}
