/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author:  Marc Ordinas i Llopis <marc.ordinasillopis@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hd-launcher.h"
#include "hd-launcher-tree.h"
#include "hd-launcher-app.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

#include <dbus/dbus.h>

#include <clutter/clutter.h>
#include <tidy/tidy-finger-scroll.h>

#include "hildon-desktop.h"
#include "hd-launcher-page.h"
#include "hd-gtk-utils.h"

#define I_(str) (g_intern_static_string ((str)))
#define HD_LAUNCHER_TOP_ID "Applications"

struct _HdLauncherPrivate
{
  ClutterActor *group;

  ClutterActor *back_button;
  GData *pages;
  ClutterActor *active_page;

  HdLauncherTree *tree;
};

#define HD_LAUNCHER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HD_TYPE_LAUNCHER, HdLauncherPrivate))

/* Signals */
enum
{
  APP_LAUNCHED,
  HIDDEN,

  LAST_SIGNAL
};

static guint launcher_signals[LAST_SIGNAL] = {0, };

G_DEFINE_TYPE (HdLauncher, hd_launcher, G_TYPE_OBJECT);

/* Forward declarations */
static void hd_launcher_constructed (GObject *gobject);
static void hd_launcher_dispose (GObject *gobject);
static ClutterActor *hd_launcher_create_back_button (const char *icon_name);
static void hd_launcher_back_button_clicked (ClutterActor *,
                                             ClutterEvent *,
                                             gpointer data);
static void hd_launcher_category_tile_clicked (HdLauncherTile *tile,
                                               gpointer data);
static void hd_launcher_application_tile_clicked (HdLauncherTile *tile,
                                                  gpointer data);
static void hd_launcher_populate_tree_finished (HdLauncherTree *tree,
                                                gpointer data);

/* DBus names */
#define OSSO_BUS_ROOT          "com.nokia"
#define OSSO_BUS_TOP           "top_application"
#define PATH_NAME_LEN           255

static void     hd_launcher_launch (HdLauncherApp *item);
static gboolean hd_launcher_service_prestart (const gchar *service,
                                              GError       **error);
static gboolean hd_launcher_service_top (const gchar *service,
                                         GError      **error);
static gboolean hd_launcher_execute (const gchar *exec,
                                     GError **error);

/* The HdLauncher singleton */
static HdLauncher *the_launcher = NULL;

HdLauncher *
hd_launcher_get (void)
{
  if (G_UNLIKELY (!the_launcher))
    the_launcher = g_object_new (HD_TYPE_LAUNCHER, NULL);
  return the_launcher;
}

static void
hd_launcher_class_init (HdLauncherClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (HdLauncherPrivate));

  gobject_class->dispose     = hd_launcher_dispose;
  gobject_class->constructed = hd_launcher_constructed;

  launcher_signals[APP_LAUNCHED] =
    g_signal_new (I_("application-launched"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, NULL);
  launcher_signals[HIDDEN] =
    g_signal_new (I_("launcher-hidden"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, NULL);
}

static void
hd_launcher_init (HdLauncher *self)
{
  HdLauncherPrivate *priv;

  self->priv = priv = HD_LAUNCHER_GET_PRIVATE (self);
  g_datalist_init (&priv->pages);
}

static void hd_launcher_constructed (GObject *gobject)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (gobject);

  priv->tree = hd_launcher_tree_new (NULL);
  g_signal_connect (priv->tree, "finished",
                    G_CALLBACK (hd_launcher_populate_tree_finished),
                    NULL);

  priv->group = clutter_group_new ();
  clutter_actor_hide(priv->group);

  ClutterActor *top_page = hd_launcher_page_new (NULL, NULL);
  g_debug ("%s: top_page: %p\n", __FUNCTION__, top_page);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->group),
                               top_page);
  clutter_actor_hide (top_page);
  priv->active_page = NULL;
  g_datalist_set_data (&priv->pages, HD_LAUNCHER_TOP_ID, top_page);

  /* back button */
  priv->back_button = hd_launcher_create_back_button ("qgn_tswitcher_back");
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->group),
                               priv->back_button);
  clutter_actor_set_reactive (priv->back_button, TRUE);
  clutter_actor_set_name (priv->back_button, "hd_launcher back button");
  g_signal_connect (priv->back_button, "button-release-event",
                    G_CALLBACK (hd_launcher_back_button_clicked), gobject);

  hd_launcher_tree_populate (priv->tree);
}

static void
hd_launcher_dispose (GObject *gobject)
{
  HdLauncher *self = HD_LAUNCHER (gobject);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (self);

  if (priv->group)
    {
      clutter_actor_destroy (priv->group);
      priv->group = NULL;
    }

  g_datalist_clear (&priv->pages);

  G_OBJECT_CLASS (hd_launcher_parent_class)->dispose (gobject);
}

void
hd_launcher_show (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  ClutterActor *top_page = g_datalist_get_data (&priv->pages,
                                                HD_LAUNCHER_TOP_ID);
  priv->active_page = top_page;

  clutter_actor_show (priv->group);
  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page), 
        HD_LAUNCHER_PAGE_TRANSITION_IN);  
}

void
hd_launcher_hide (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  
  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page), 
        HD_LAUNCHER_PAGE_TRANSITION_OUT);
  priv->active_page = NULL;
}

/* hide the launcher fully. Called from hd-launcher-page 
 * after a transition has finished */
void
hd_launcher_hide_final (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  
  clutter_actor_hide (priv->group);
}

static ClutterActor *
hd_launcher_create_back_button (const char *icon_name)
{
  ClutterActor *icon = NULL;
  GtkIconTheme *icon_theme;
  GtkIconInfo *info;

  icon_theme = gtk_icon_theme_get_default ();
  info = gtk_icon_theme_lookup_icon(icon_theme, icon_name, 48,
                                    GTK_ICON_LOOKUP_NO_SVG);
  if (info != NULL)
    {
      const gchar *fname = gtk_icon_info_get_filename(info);
      g_debug("create_back_button: using %s for %s\n", fname, icon_name);
      icon = clutter_texture_new_from_file(fname, NULL);
      clutter_actor_set_size (icon, 112, 56);

      gtk_icon_info_free(info);
    }
  else
    g_debug("create_back_button: couldn't find icon %s\n", icon_name);

  clutter_actor_set_position (icon, 800 - 112, 0);

  return icon;
}

static void
hd_launcher_back_button_clicked (ClutterActor *actor,
                                 ClutterEvent *event,
                                 gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  ClutterActor *top_page = g_datalist_get_data (&priv->pages,
                                                HD_LAUNCHER_TOP_ID);
  if (priv->active_page == top_page)
    g_signal_emit (data, launcher_signals[HIDDEN], 0);
  else
    {
      g_debug ("%s: Going back\n", __FUNCTION__);
      hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page), 
        HD_LAUNCHER_PAGE_TRANSITION_OUT_SUB);
      hd_launcher_page_transition(HD_LAUNCHER_PAGE(top_page), 
        HD_LAUNCHER_PAGE_TRANSITION_BACK);
      priv->active_page = top_page;
    }
}

ClutterActor *hd_launcher_get_group (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  return priv->group;
}

static void
hd_launcher_category_tile_clicked (HdLauncherTile *tile, gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  ClutterActor *page = CLUTTER_ACTOR (data);

  g_debug ("%s: Tile clicked, text: %s\n", __FUNCTION__,
      hd_launcher_tile_get_text (tile));
  g_debug ("%s: Showing page: %s\n", __FUNCTION__,
      hd_launcher_page_get_text (HD_LAUNCHER_PAGE (page)));

  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page), 
        HD_LAUNCHER_PAGE_TRANSITION_BACK);
  hd_launcher_page_transition(HD_LAUNCHER_PAGE(page), 
        HD_LAUNCHER_PAGE_TRANSITION_IN_SUB);
  priv->active_page = page;
}

static void
hd_launcher_application_tile_clicked (HdLauncherTile *tile,
                                      gpointer data)
{
  HdLauncherApp *app = HD_LAUNCHER_APP (data);

  hd_launcher_launch (app);
}

/*
 * Creating the pages and tiles
 */

typedef struct
{
  GList *items;
} HdLauncherTraverseData;

static void
hd_launcher_create_page (HdLauncherItem *item, gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  if (hd_launcher_item_get_item_type (item) != HD_CATEGORY_LAUNCHER)
    return;

  ClutterActor *newpage = hd_launcher_page_new(
                              hd_launcher_item_get_icon_name (item),
                              _(hd_launcher_item_get_name (item)));
  clutter_actor_hide (newpage);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->group), newpage);

  g_datalist_set_data (&priv->pages, hd_launcher_item_get_id (item), newpage);
}

static gboolean
hd_launcher_lazy_traverse_tree (gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  HdLauncherTraverseData *tdata = data;
  HdLauncherItem *item = tdata->items->data;
  HdLauncherTile *tile;
  HdLauncherPage *page;

  g_debug ("%s: Adding item name: %s, position: %d\n", __FUNCTION__,
           hd_launcher_item_get_name(item),
           hd_launcher_item_get_position (item));

  tile = hd_launcher_tile_new (
      hd_launcher_item_get_icon_name (item),
      _(hd_launcher_item_get_name (item)));

  /* Find in which page it goes */
  if (!hd_launcher_item_get_category (item))
    page = g_datalist_get_data (&priv->pages, HD_LAUNCHER_TOP_ID);
  else
    page = g_datalist_get_data (&priv->pages,
                                hd_launcher_item_get_category (item));
  if (!page)
    /* Put it in the top level. */
    page = g_datalist_get_data (&priv->pages, HD_LAUNCHER_TOP_ID);

  g_debug ("%s: page: %p\n", __FUNCTION__, page);
  hd_launcher_page_add_tile (page, tile);

  if (hd_launcher_item_get_item_type(item) == HD_CATEGORY_LAUNCHER)
    {
      g_signal_connect (tile, "clicked",
                        G_CALLBACK (hd_launcher_category_tile_clicked),
                        g_datalist_get_data (&priv->pages,
                          hd_launcher_item_get_id (item)));
    }
  else if (hd_launcher_item_get_item_type(item) == HD_APPLICATION_LAUNCHER)
    {
      g_signal_connect (tile, "clicked",
                        G_CALLBACK (hd_launcher_application_tile_clicked),
                        item);

      if (hd_launcher_app_get_prestart_mode (HD_LAUNCHER_APP (item)) ==
            HD_APP_PRESTART_ALWAYS)
        {
          hd_launcher_service_prestart (
              hd_launcher_app_get_service(HD_LAUNCHER_APP(item)), NULL);
        }
    }

  if (tdata->items->next)
    {
      tdata->items = tdata->items->next;
      return TRUE;
    }

  return FALSE;
}

static void
hd_launcher_lazy_traverse_cleanup (gpointer data)
{
  g_free (data);
}

#ifndef G_DEBUG_DISABLE
static void
_hd_launcher_show_pages (GQuark key, gpointer data, gpointer user_data)
{
  g_debug ("%s: %s -> %p\n", __FUNCTION__, g_quark_to_string (key), data);
}
#endif

static void
hd_launcher_populate_tree_finished (HdLauncherTree *tree, gpointer data)
{
  HdLauncherTraverseData *tdata = g_new0 (HdLauncherTraverseData, 1);
  tdata->items = hd_launcher_tree_get_items(tree, NULL);

  /* First we traverse the list and create all the categories,
   * so that apps can be correctly put into them.
   */
  g_list_foreach (tdata->items, (GFunc) hd_launcher_create_page, NULL);

#ifndef G_DEBUG_DISABLE
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  g_datalist_foreach (&priv->pages,
      (GDataForeachFunc) _hd_launcher_show_pages, NULL);
#endif

  clutter_threads_add_idle_full (CLUTTER_PRIORITY_REDRAW + 20,
                                 hd_launcher_lazy_traverse_tree,
                                 tdata,
                                 hd_launcher_lazy_traverse_cleanup);
}

/* Application management */

static void
hd_launcher_launch (HdLauncherApp *item)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  gboolean result = FALSE;
  const gchar *service = hd_launcher_app_get_service (item);
  const gchar *exec;

  /* do launch animation */
  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page), 
        HD_LAUNCHER_PAGE_TRANSITION_LAUNCH);
        
  if (service)
    {
      result = hd_launcher_service_top (service, NULL);
    }
  else
    {
      exec = hd_launcher_app_get_exec (item);
      if (exec)
        {
          result = hd_launcher_execute (exec, NULL);
          return;
        }
    }

  if (result)
    g_signal_emit (hd_launcher_get (), launcher_signals[APP_LAUNCHED],
        0, NULL);
  return;
}

static gboolean
hd_launcher_service_prestart (const gchar *service, GError **error)
{
  DBusError derror;
  DBusConnection *conn;
  gboolean res;

  g_debug ("%s: service=%s\n", __FUNCTION__, service);

  dbus_error_init (&derror);
  conn = dbus_bus_get (DBUS_BUS_SESSION, &derror);
  if (dbus_error_is_set (&derror))
  {
    g_warning ("could not start: %s: %s", service, derror.message);
    dbus_error_free (&derror);
    return FALSE;
  }

  res = dbus_bus_start_service_by_name (conn, service, 0, NULL, &derror);
  if (dbus_error_is_set (&derror))
  {
    g_warning ("could not start: %s: %s", service, derror.message);
    dbus_error_free (&derror);
  }

  return res;
}

static gboolean
hd_launcher_service_top (const gchar *service, GError **error)
{
  gchar path[PATH_NAME_LEN];
  DBusMessage *msg = NULL;
  DBusError derror;
  DBusConnection *conn;

  gchar *tmp = g_strdelimit(g_strdup (service), ".", '/');
  g_snprintf (path, PATH_NAME_LEN, "/%s", tmp);
  g_free (tmp);

  g_debug ("%s: service:%s path:%s interface:%s\n", __FUNCTION__,
    service, path, service);

  dbus_error_init (&derror);
  conn = dbus_bus_get (DBUS_BUS_SESSION, &derror);
  if (dbus_error_is_set (&derror))
  {
    g_warning ("could not start: %s: %s", service, derror.message);
    dbus_error_free (&derror);
    return FALSE;
  }

  msg = dbus_message_new_method_call (service, path, service, OSSO_BUS_TOP);
  if (msg == NULL)
  {
    g_warning ("failed to create message");
    return FALSE;
  }

  if (!dbus_connection_send (conn, msg, NULL))
    {
      dbus_message_unref (msg);
      g_warning ("dbus_connection_send failed");
      return FALSE;
    }

  dbus_message_unref (msg);
  return TRUE;
}

#define OOM_DISABLE "0"

static void
_hd_launcher_child_setup(gpointer user_data)
{
  int priority;
  int fd;

  /* If the child process inherited desktop's high priority,
   * give child default priority */
  priority = getpriority (PRIO_PROCESS, 0);

  if (!errno && priority < 0)
  {
    setpriority (PRIO_PROCESS, 0, 0);
  }

  /* Unprotect from OOM */
  fd = open ("/proc/self/oom_adj", O_WRONLY);
  if (fd >= 0)
  {
    write (fd, OOM_DISABLE, sizeof (OOM_DISABLE));
    close (fd);
  }
}

static gboolean
hd_launcher_execute (const gchar *exec, GError **error)
{
  gboolean res = FALSE;
  gchar *space = strchr (exec, ' ');
  gchar *exec_cmd;
  gint argc;
  gchar **argv = NULL;
  GPid child_pid;
  GError *internal_error = NULL;

  g_debug ("Executing `%s'", exec);

  if (space)
  {
    gchar *cmd = g_strndup (exec, space - exec);
    gchar *exc = g_find_program_in_path (cmd);

    exec_cmd = g_strconcat (exc, space, NULL);

    g_free (cmd);
    g_free (exc);
  }
  else
    exec_cmd = g_find_program_in_path (exec);

  if (!g_shell_parse_argv (exec_cmd, &argc, &argv, &internal_error))
  {
    g_propagate_error (error, internal_error);

    g_free (exec_cmd);
    if (argv)
      g_strfreev (argv);

    return FALSE;
  }

  res = g_spawn_async (NULL,
                       argv, NULL,
                       0,
                       _hd_launcher_child_setup, NULL,
                       &child_pid,
                       &internal_error);
  if (internal_error)
    g_propagate_error (error, internal_error);

  g_free (exec_cmd);

  if (argv)
    g_strfreev (argv);

  return res;
}
