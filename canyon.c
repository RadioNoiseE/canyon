#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "river-window-management-v1.h"
#include "river-xkb-bindings-v1.h"

struct canyon_wayland_window {
  struct river_window_v1 *window;
  struct river_node_v1   *node;

  bool    new, closed;
  int32_t x, y, width, height;

  struct canyon_wayland_seat *pointer_move_requested, *pointer_resize_requested;
  uint32_t                    pointer_resize_requested_edges;

  struct wl_list link;
};

struct canyon_wayland_output {
  struct river_output_v1 *output;
  bool                    removed;

  struct wl_list link;
};

enum canyon_wayland_seat_action {
  ACTION_CLOSE,
  ACTION_EXIT,
  ACTION_FOCUS_NEXT,
  ACTION_MOVE,
  ACTION_NONE,
  ACTION_RESIZE,
};

struct canyon_wayland_seat_xkb_binding {
  struct river_xkb_binding_v1    *xkb_binding;
  struct canyon_wayland_seat     *seat;
  enum canyon_wayland_seat_action action;

  struct wl_list link;
};

struct canyon_wayland_seat_pointer_binding {
  struct river_pointer_binding_v1 *pointer_binding;
  struct canyon_wayland_seat      *seat;
  enum canyon_wayland_seat_action  action;

  struct wl_list link;
};

enum canyon_wayland_seat_op {
  SEAT_OP_NONE,
  SEAT_OP_MOVE,
  SEAT_OP_RESIZE,
};

struct canyon_wayland_seat {
  struct river_seat_v1 *seat;
  bool                  new, removed;

  struct canyon_wayland_window *focused, *hovered, *interacted;

  struct wl_list                  xkb_bindings, pointer_bindings;
  enum canyon_wayland_seat_action pending_action;

  enum canyon_wayland_seat_op   op;
  struct canyon_wayland_window *op_window;

  int32_t op_start_x, op_start_y, op_dx, op_dy;
  bool    op_release;

  int32_t  op_start_width, op_start_height;
  uint32_t op_edges;

  struct wl_list link;
};

struct canyon_wayland {
  struct wl_list outputs;
  struct wl_list windows;
  struct wl_list seats;

  bool exit;
};

struct river_window_manager_v1 *window_manager;
struct river_xkb_bindings_v1   *xkb_bindings;

const struct river_window_v1_listener window_listener = {
  .closed                     = NULL,
  .dimensions_hint            = NULL,
  .dimensions                 = NULL,
  .app_id                     = NULL,
  .title                      = NULL,
  .parent                     = NULL,
  .decoration_hint            = NULL,
  .pointer_move_requested     = NULL,
  .pointer_resize_requested   = NULL,
  .show_window_menu_requested = NULL,
  .maximize_requested         = NULL,
  .unmaximize_requested       = NULL,
  .fullscreen_requested       = NULL,
  .exit_fullscreen_requested  = NULL,
  .minimize_requested         = NULL,
  .unreliable_pid             = NULL,
  .presentation_hint          = NULL,
  .identifier                 = NULL,
  .capture_sessions           = NULL,
};

static void canyon_window_manage (struct canyon_wayland_window *window) {}

const struct river_output_v1_listener output_listener = {
  .removed          = NULL,
  .wl_output        = NULL,
  .position         = NULL,
  .dimensions       = NULL,
  .capture_sessions = NULL,
};

const struct river_seat_v1_listener seat_listener = {
  .removed                   = NULL,
  .wl_seat                   = NULL,
  .pointer_enter             = NULL,
  .pointer_leave             = NULL,
  .window_interaction        = NULL,
  .shell_surface_interaction = NULL,
  .op_delta                  = NULL,
  .op_release                = NULL,
  .pointer_position          = NULL,
};

static void canyon_seat_manage (struct canyon_wayland_seat *seat) {}

static void canyon_seat_render (struct canyon_wayland_seat *seat) {}

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

static void window_manager_listener_manage_start (
  void *data, struct river_window_manager_v1 *window_manager) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_output *output, *output_tmp;
  wl_list_for_each_safe (output, output_tmp, &wayland->outputs,
                         link) if (output->removed) {
    river_output_v1_destroy (output->output);
    wl_list_remove (&output->link);
    free (output);
  }

  struct canyon_wayland_window *window, *window_tmp;
  wl_list_for_each_safe (window, window_tmp, &wayland->windows,
                         link) if (window->closed) {
    struct canyon_wayland_seat *seat;
    wl_list_for_each (seat, &wayland->seats, link) {
      if (seat->focused == window) seat->focused = NULL;
      if (seat->op_window == window) {
        river_seat_v1_op_end (seat->seat);
        seat->op        = SEAT_OP_NONE;
        seat->op_window = NULL;
      }
    }

    river_window_v1_destroy (window->window);
    wl_list_remove (&window->link);
    free (window);
  }

  struct canyon_wayland_seat *seat, *seat_tmp;
  wl_list_for_each_safe (seat, seat_tmp, &wayland->seats,
                         link) if (seat->removed) {
    struct canyon_wayland_seat_xkb_binding *xkb_binding, *xkb_binding_tmp;
    wl_list_for_each_safe (xkb_binding, xkb_binding_tmp, &seat->xkb_bindings,
                           link) {
      river_xkb_binding_v1_destroy (xkb_binding->xkb_binding);
      wl_list_remove (&xkb_binding->link);
      free (xkb_binding);
    }

    struct canyon_wayland_seat_pointer_binding *pointer_binding,
      *pointer_binding_tmp;
    wl_list_for_each_safe (pointer_binding, pointer_binding_tmp,
                           &seat->pointer_bindings, link) {
      river_pointer_binding_v1_destroy (pointer_binding->pointer_binding);
      wl_list_remove (&pointer_binding->link);
      free (pointer_binding);
    }
  }

  wl_list_for_each (window, &wayland->windows, link)
    canyon_window_manage (window);

  wl_list_for_each (seat, &wayland->seats, link) canyon_seat_manage (seat);

  river_window_manager_v1_manage_finish (window_manager);
}

static void window_manager_listener_render_start (
  void *data, struct river_window_manager_v1 *window_manager) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_seat *seat;
  wl_list_for_each (seat, &wayland->seats, link) canyon_seat_render (seat);

  river_window_manager_v1_render_finish (window_manager);
}

static void window_manager_listener_session_locked (
  void *data, struct river_window_manager_v1 *window_manager) {}

static void window_manager_listener_session_unlocked (
  void *data, struct river_window_manager_v1 *window_manager) {}

static void
window_manager_listener_window (void                           *data,
                                struct river_window_manager_v1 *window_manager,
                                struct river_window_v1         *window) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_window *wayland_window =
    calloc (1, sizeof (struct canyon_wayland_window));
  wayland_window->window = window;
  wayland_window->node   = river_window_v1_get_node (window);
  wayland_window->new    = true;

  river_window_v1_add_listener (window, &window_listener, wayland_window);
  wl_list_insert (wayland->windows.prev, &wayland_window->link);
}

static void
window_manager_listener_output (void                           *data,
                                struct river_window_manager_v1 *window_manager,
                                struct river_output_v1         *output) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_output *wayland_output =
    calloc (1, sizeof (struct canyon_wayland_output));
  wayland_output->output = output;

  river_output_v1_add_listener (output, &output_listener, wayland_output);
  wl_list_insert (wayland->outputs.prev, &wayland_output->link);
}

static void
window_manager_listener_seat (void                           *data,
                              struct river_window_manager_v1 *window_manager,
                              struct river_seat_v1           *seat) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_seat *wayland_seat =
    calloc (1, sizeof (struct canyon_wayland_seat));

  river_seat_v1_add_listener (seat, &seat_listener, wayland_seat);
  wl_list_insert (wayland->seats.prev, &wayland_seat->link);
}

static const struct river_window_manager_v1_listener window_manager_listener = {
  .unavailable      = window_manager_listener_unavailable,
  .finished         = window_manager_listener_finished,
  .manage_start     = window_manager_listener_manage_start,
  .render_start     = window_manager_listener_render_start,
  .session_locked   = window_manager_listener_session_locked,
  .session_unlocked = window_manager_listener_session_unlocked,
  .window           = window_manager_listener_window,
  .output           = window_manager_listener_output,
  .seat             = NULL,
};

static void registry_listener_global (void *data, struct wl_registry *registry,
                                      uint32_t name, const char *interface,
                                      uint32_t version) {
  if (!strcmp (interface, river_window_manager_v1_interface.name))
    window_manager =
      wl_registry_bind (registry, name, &river_window_manager_v1_interface, 5);
  else if (!strcmp (interface, river_xkb_bindings_v1_interface.name))
    xkb_bindings =
      wl_registry_bind (registry, name, &river_xkb_bindings_v1_interface, 3);
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

  wl_list_init (&wayland.outputs);
  wl_list_init (&wayland.windows);
  wl_list_init (&wayland.seats);

  if (window_manager != NULL && xkb_bindings != NULL)
    river_window_manager_v1_add_listener (window_manager,
                                          &window_manager_listener, &wayland);

  while (wl_display_dispatch (display) != -1 && !wayland.exit)
    ;

  wl_registry_destroy (registry);
  wl_display_disconnect (display);
}
