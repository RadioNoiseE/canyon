#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "river-window-management-v1.h"

struct river_window_manager_v1 *window_manager;

static void registry_listener_global (void *data, struct wl_registry *registry,
                                      uint32_t name, const char *interface,
                                      uint32_t version) {
  if (strcmp (interface, river_window_manager_v1_interface.name) == 0)
    window_manager =
      wl_registry_bind (registry, name, &river_window_manager_v1_interface, 5);
}

static void registry_listener_global_remove (void               *data,
                                             struct wl_registry *registry,
                                             uint32_t            name) {}

static const struct wl_registry_listener registry_listener = {
  .global        = registry_listener_global,
  .global_remove = registry_listener_global_remove,
};

int main (void) {
  struct wl_display  *display  = wl_display_connect (NULL);
  struct wl_registry *registry = wl_display_get_registry (display);

  unsetenv ("WAYLAND_DEBUG");
  signal (SIGCHLD, SIG_IGN);

  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);

  while (wl_display_dispatch (display) != -1)
    ;
}
