#ifndef TRAY_H
#define TRAY_H

#include <glib.h>

/* Register callbacks and prepare the StatusNotifierItem integration.
   The tray icon is created lazily on first call to tray_ensure(). */
void tray_init(gpointer window, void (*on_show)(void), void (*on_quit)(void));

/* Create/register the tray icon. Returns TRUE when a StatusNotifier host
   accepted the item (i.e. a tray is available). Safe to call repeatedly. */
gboolean tray_ensure(void);

#endif
