#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "river-window-management-v1.h"

struct canyon_wayland {
  bool exit;
};

struct river_window_manager_v1 *window_manager;

static void window_manager_listener_unavailable (
  void *data, struct river_window_manager_v1 *window_manager) {
  river_window_manager_v1_destroy (window_manager);

  struct canyon_wayland *wayland = data;
  wayland->exit                  = true;
}

static void window_manager_listener_finished (
  void *data, struct river_window_manager_v1 *window_manager) {
  river_window_manager_v1_destroy (window_manager);

  struct canyon_wayland *wayland = data;
  wayland->exit                  = true;
}

static const struct river_window_manager_v1_listener window_manager_listener = {
  .unavailable      = window_manager_listener_unavailable,
  .finished         = window_manager_listener_finished,
  .manage_start     = NULL,
  .render_start     = NULL,
  .session_locked   = NULL,
  .session_unlocked = NULL,
  .window           = NULL,
  .output           = NULL,
  .seat             = NULL,
};

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
  struct canyon_wayland wayland = {0};

  struct wl_display  *display  = wl_display_connect (NULL);
  struct wl_registry *registry = wl_display_get_registry (display);

  unsetenv ("WAYLAND_DEBUG");
  signal (SIGCHLD, SIG_IGN);

  wl_registry_add_listener (registry, &registry_listener, &wayland);
  wl_display_roundtrip (display);

  river_window_manager_v1_add_listener (window_manager,
                                        &window_manager_listener, &wayland);

  while (wl_display_dispatch (display) != -1 && !wayland.exit)
    ;

  wl_registry_destroy (registry);
  wl_display_disconnect (display);
}
