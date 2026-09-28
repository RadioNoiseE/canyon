#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

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
  ACTION_SPAWN_FOOT,
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

static void window_listener_closed (void                   *data,
                                    struct river_window_v1 *window) {
  struct canyon_wayland_window *wayland_window = data;
  wayland_window->closed                       = true;
}

static void
window_listener_dimensions_hint (void *data, struct river_window_v1 *window,
                                 int32_t min_width, int32_t min_height,
                                 int32_t max_width, int32_t max_height) {}

static void window_listener_dimensions (void                   *data,
                                        struct river_window_v1 *window,
                                        int32_t width, int32_t height) {
  struct canyon_wayland_window *wayland_window = data;
  wayland_window->width                        = width;
  wayland_window->height                       = height;
}

static void window_listener_app_id (void *data, struct river_window_v1 *window,
                                    const char *app_id) {}

static void window_listener_title (void *data, struct river_window_v1 *window,
                                   const char *title) {}

static void window_listener_parent (void *data, struct river_window_v1 *window,
                                    struct river_window_v1 *parent) {}

static void window_listener_decoration_hint (void                   *data,
                                             struct river_window_v1 *window,
                                             uint32_t                hint) {}

static void window_listener_pointer_move_requested (
  void *data, struct river_window_v1 *window, struct river_seat_v1 *seat) {
  struct canyon_wayland_window *wayland_window = data;
  wayland_window->pointer_move_requested = river_seat_v1_get_user_data (seat);
}

static void window_listener_pointer_resize_requested (
  void *data, struct river_window_v1 *window, struct river_seat_v1 *seat,
  uint32_t edges) {
  struct canyon_wayland_window *wayland_window = data;
  wayland_window->pointer_resize_requested = river_seat_v1_get_user_data (seat);
  wayland_window->pointer_resize_requested_edges = edges;
}

static void window_listener_show_window_menu_requested (
  void *data, struct river_window_v1 *window, int32_t x, int32_t y) {}

static void
window_listener_maximize_requested (void                   *data,
                                    struct river_window_v1 *window) {}

static void
window_listener_unmaximize_requested (void                   *data,
                                      struct river_window_v1 *window) {}

static void window_listener_fullscreen_requested (
  void *data, struct river_window_v1 *window, struct river_output_v1 *output) {}

static void
window_listener_exit_fullscreen_requested (void                   *data,
                                           struct river_window_v1 *window) {}

static void
window_listener_minimize_requested (void                   *data,
                                    struct river_window_v1 *window) {}

static void window_listener_unreliable_pid (void                   *data,
                                            struct river_window_v1 *window,
                                            int32_t unreliable_pid) {}

static void window_listener_presentation_hint (void                   *data,
                                               struct river_window_v1 *window,
                                               uint32_t                hint) {}

static void window_listener_identifier (void                   *data,
                                        struct river_window_v1 *window,
                                        const char             *identifier) {}

static void window_listener_capture_sessions (void                   *data,
                                              struct river_window_v1 *window,
                                              uint32_t                count) {}

const struct river_window_v1_listener window_listener = {
  .closed                     = window_listener_closed,
  .dimensions_hint            = window_listener_dimensions_hint,
  .dimensions                 = window_listener_dimensions,
  .app_id                     = window_listener_app_id,
  .title                      = window_listener_title,
  .parent                     = window_listener_parent,
  .decoration_hint            = window_listener_decoration_hint,
  .pointer_move_requested     = window_listener_pointer_move_requested,
  .pointer_resize_requested   = window_listener_pointer_resize_requested,
  .show_window_menu_requested = window_listener_show_window_menu_requested,
  .maximize_requested         = window_listener_maximize_requested,
  .unmaximize_requested       = window_listener_unmaximize_requested,
  .fullscreen_requested       = window_listener_fullscreen_requested,
  .exit_fullscreen_requested  = window_listener_exit_fullscreen_requested,
  .minimize_requested         = window_listener_minimize_requested,
  .unreliable_pid             = window_listener_unreliable_pid,
  .presentation_hint          = window_listener_presentation_hint,
  .identifier                 = window_listener_identifier,
  .capture_sessions           = window_listener_capture_sessions,
};

static void canyon_seat_pointer_move (struct canyon_wayland        *wayland,
                                      struct canyon_wayland_seat   *seat,
                                      struct canyon_wayland_window *window);

static void canyon_seat_pointer_resize (struct canyon_wayland        *wayland,
                                        struct canyon_wayland_seat   *seat,
                                        struct canyon_wayland_window *window,
                                        uint32_t                      edges);

static void canyon_window_manage (struct canyon_wayland        *wayland,
                                  struct canyon_wayland_window *window) {
  if (window->new) {
    window->new = false;
    river_node_v1_set_position (window->node, 0, 0);
    window->x = window->y = 0;
  }

  if (window->pointer_move_requested != NULL) {
    canyon_seat_pointer_move (wayland, window->pointer_move_requested, window);
    window->pointer_move_requested = NULL;
  }

  if (window->pointer_resize_requested != NULL) {
    canyon_seat_pointer_resize (wayland, window->pointer_resize_requested,
                                window, window->pointer_resize_requested_edges);
    window->pointer_resize_requested = NULL;
  }
}

static void output_listener_removed (void                   *data,
                                     struct river_output_v1 *output) {
  struct canyon_wayland_output *wayland_output = data;
  wayland_output->removed                      = true;
}

static void output_listener_wl_output (void                   *data,
                                       struct river_output_v1 *output,
                                       uint32_t                name) {}

static void output_listener_position (void                   *data,
                                      struct river_output_v1 *output, int32_t x,
                                      int32_t y) {}

static void output_listener_dimensions (void                   *data,
                                        struct river_output_v1 *output,
                                        int32_t width, int32_t height) {}

static void output_listener_capture_sessions (void                   *data,
                                              struct river_output_v1 *output,
                                              uint32_t                count) {}

const struct river_output_v1_listener output_listener = {
  .removed          = output_listener_removed,
  .wl_output        = output_listener_wl_output,
  .position         = output_listener_position,
  .dimensions       = output_listener_dimensions,
  .capture_sessions = output_listener_capture_sessions,
};

static void
xkb_binding_listener_pressed (void                        *data,
                              struct river_xkb_binding_v1 *xkb_binding) {
  struct canyon_wayland_seat_xkb_binding *wayland_xkb_binding = data;
  wayland_xkb_binding->seat->pending_action = wayland_xkb_binding->action;
}

static void
xkb_binding_listener_released (void                        *data,
                               struct river_xkb_binding_v1 *xkb_binding) {}

static void
xkb_binding_listener_stop_repeat (void                        *data,
                                  struct river_xkb_binding_v1 *xkb_binding) {}

const struct river_xkb_binding_v1_listener xkb_binding_listener = {
  .pressed     = xkb_binding_listener_pressed,
  .released    = xkb_binding_listener_released,
  .stop_repeat = xkb_binding_listener_stop_repeat,
};

static void canyon_xkb_binding_create (struct canyon_wayland      *wayland,
                                       struct canyon_wayland_seat *seat,
                                       uint32_t mods, xkb_keysym_t keysym,
                                       enum canyon_wayland_seat_action action) {
  struct canyon_wayland_seat_xkb_binding *xkb_binding =
    calloc (1, sizeof (struct canyon_wayland_seat_xkb_binding));
  xkb_binding->xkb_binding = river_xkb_bindings_v1_get_xkb_binding (
    xkb_bindings, seat->seat, keysym, mods);
  xkb_binding->seat   = seat;
  xkb_binding->action = action;

  river_xkb_binding_v1_add_listener (xkb_binding->xkb_binding,
                                     &xkb_binding_listener, xkb_binding);
  river_xkb_binding_v1_enable (xkb_binding->xkb_binding);
  wl_list_insert (seat->xkb_bindings.prev, &xkb_binding->link);
}

static void pointer_binding_listener_pressed (
  void *data, struct river_pointer_binding_v1 *pointer_binding) {
  struct canyon_wayland_seat_pointer_binding *wayland_pointer_binding = data;
  wayland_pointer_binding->seat->pending_action =
    wayland_pointer_binding->action;
}

static void pointer_binding_listener_released (
  void *data, struct river_pointer_binding_v1 *pointer_binding) {}

const struct river_pointer_binding_v1_listener pointer_binding_listener = {
  .pressed  = pointer_binding_listener_pressed,
  .released = pointer_binding_listener_released,
};

static void canyon_pointer_binding_create (
  struct canyon_wayland *wayland, struct canyon_wayland_seat *seat,
  uint32_t mods, uint32_t button, enum canyon_wayland_seat_action action) {
  struct canyon_wayland_seat_pointer_binding *pointer_binding =
    calloc (1, sizeof (struct canyon_wayland_seat_pointer_binding));
  pointer_binding->pointer_binding =
    river_seat_v1_get_pointer_binding (seat->seat, button, mods);
  pointer_binding->seat   = seat;
  pointer_binding->action = action;

  river_pointer_binding_v1_add_listener (pointer_binding->pointer_binding,
                                         &pointer_binding_listener,
                                         pointer_binding);
  river_pointer_binding_v1_enable (pointer_binding->pointer_binding);
  wl_list_insert (seat->pointer_bindings.prev, &pointer_binding->link);
}

static void seat_listener_removed (void *data, struct river_seat_v1 *seat) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->removed                    = true;
}

static void seat_listener_wl_seat (void *data, struct river_seat_v1 *seat,
                                   uint32_t name) {}

static void seat_listener_pointer_enter (void *data, struct river_seat_v1 *seat,
                                         struct river_window_v1 *window) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->hovered = river_window_v1_get_user_data (window);
}

static void seat_listener_pointer_leave (void                 *data,
                                         struct river_seat_v1 *seat) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->hovered                    = NULL;
}

static void seat_listener_window_interaction (void                   *data,
                                              struct river_seat_v1   *seat,
                                              struct river_window_v1 *window) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->interacted = river_window_v1_get_user_data (window);
}

static void seat_listener_shell_surface_interaction (
  void *data, struct river_seat_v1 *seat,
  struct river_shell_surface_v1 *shell_surface) {}

static void seat_listener_op_delta (void *data, struct river_seat_v1 *seat,
                                    int32_t dx, int32_t dy) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->op_dx                      = dx;
  wayland_seat->op_dy                      = dy;
}

static void seat_listener_op_release (void *data, struct river_seat_v1 *seat) {
  struct canyon_wayland_seat *wayland_seat = data;
  wayland_seat->op_release                 = true;
}

static void seat_listener_pointer_position (void                 *data,
                                            struct river_seat_v1 *seat,
                                            int32_t x, int32_t y) {}

const struct river_seat_v1_listener seat_listener = {
  .removed                   = seat_listener_removed,
  .wl_seat                   = seat_listener_wl_seat,
  .pointer_enter             = seat_listener_pointer_enter,
  .pointer_leave             = seat_listener_pointer_leave,
  .window_interaction        = seat_listener_window_interaction,
  .shell_surface_interaction = seat_listener_shell_surface_interaction,
  .op_delta                  = seat_listener_op_delta,
  .op_release                = seat_listener_op_release,
  .pointer_position          = seat_listener_pointer_position,
};

static void canyon_seat_focus (struct canyon_wayland        *wayland,
                               struct canyon_wayland_seat   *seat,
                               struct canyon_wayland_window *window) {
  if (window != NULL && !wl_list_empty (&wayland->windows))
    window = wl_container_of (wayland->windows.prev, window, link);

  if (seat->focused == window) return;

  if (window != NULL) {
    river_seat_v1_focus_window (seat->seat, window->window);
    river_node_v1_place_top (window->node);
    wl_list_remove (&window->link);
    wl_list_insert (wayland->windows.prev, &window->link);
  } else river_seat_v1_clear_focus (seat->seat);

  seat->focused = window;
}

static void canyon_seat_pointer_move (struct canyon_wayland        *wayland,
                                      struct canyon_wayland_seat   *seat,
                                      struct canyon_wayland_window *window) {
  canyon_seat_focus (wayland, seat, window);
  river_seat_v1_op_start_pointer (seat->seat);

  seat->op         = SEAT_OP_MOVE;
  seat->op_window  = window;
  seat->op_start_x = window->x;
  seat->op_start_y = window->y;
  seat->op_dx      = 0;
  seat->op_dy      = 0;
}

static void canyon_seat_pointer_resize (struct canyon_wayland        *wayland,
                                        struct canyon_wayland_seat   *seat,
                                        struct canyon_wayland_window *window,
                                        uint32_t                      edges) {
  canyon_seat_focus (wayland, seat, window);
  river_window_v1_inform_resize_start (window->window);
  river_seat_v1_op_start_pointer (seat->seat);

  seat->op              = SEAT_OP_RESIZE;
  seat->op_window       = window;
  seat->op_edges        = edges;
  seat->op_start_x      = window->x;
  seat->op_start_y      = window->y;
  seat->op_start_width  = window->width;
  seat->op_start_height = window->height;
  seat->op_dx           = 0;
  seat->op_dy           = 0;
}

static void canyon_seat_action (struct canyon_wayland          *wayland,
                                struct canyon_wayland_seat     *seat,
                                enum canyon_wayland_seat_action action) {
  switch (action) {
  case ACTION_CLOSE:
    if (seat->focused != NULL) river_window_v1_close (seat->focused->window);
    break;
  case ACTION_EXIT:
    river_window_manager_v1_exit_session (window_manager);
    break;
  case ACTION_FOCUS_NEXT:
    if (!wl_list_empty (&wayland->windows)) {
      struct canyon_wayland_window *window =
        wl_container_of (wayland->windows.next, window, link);
      canyon_seat_focus (wayland, seat, window);
    }
    break;
  case ACTION_MOVE:
    if (seat->op == SEAT_OP_NONE && seat->hovered != NULL)
      canyon_seat_pointer_move (wayland, seat, seat->hovered);
    break;
  case ACTION_NONE:
    break;
  case ACTION_RESIZE:
    if (seat->op == SEAT_OP_NONE && seat->hovered != NULL)
      canyon_seat_pointer_resize (wayland, seat, seat->hovered,
                                  RIVER_WINDOW_V1_EDGES_BOTTOM |
                                    RIVER_WINDOW_V1_EDGES_RIGHT);
    break;
  case ACTION_SPAWN_FOOT:
    if (!fork ()) execlp ("foot", "foot", NULL);
    printf("SPAWN_FOOT action recieved\n");
    break;
  }
}

static void canyon_seat_manage (struct canyon_wayland      *wayland,
                                struct canyon_wayland_seat *seat) {
  if (seat->new) {
    seat->new = false;

    canyon_xkb_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                               XKB_KEY_q, ACTION_CLOSE);
    canyon_xkb_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                               XKB_KEY_e, ACTION_EXIT);
    canyon_xkb_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                               XKB_KEY_n, ACTION_FOCUS_NEXT);
    canyon_xkb_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                               XKB_KEY_space, ACTION_SPAWN_FOOT);

    canyon_pointer_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                                   BTN_LEFT, ACTION_MOVE);
    canyon_pointer_binding_create (wayland, seat, RIVER_SEAT_V1_MODIFIERS_MOD4,
                                   BTN_RIGHT, ACTION_RESIZE);
  }

  canyon_seat_focus (wayland, seat, seat->interacted);
  seat->interacted = NULL;

  canyon_seat_action (wayland, seat, seat->pending_action);
  seat->pending_action = ACTION_NONE;

  switch (seat->op) {
  case SEAT_OP_MOVE:
    if (seat->op_release) {
      river_seat_v1_op_end (seat->seat);
      seat->op        = SEAT_OP_NONE;
      seat->op_window = NULL;
    }
    break;
  case SEAT_OP_NONE:
    break;
  case SEAT_OP_RESIZE:
    if (seat->op_release) {
      river_window_v1_inform_resize_end (seat->op_window->window);
      river_seat_v1_op_end (seat->seat);
      seat->op        = SEAT_OP_NONE;
      seat->op_window = NULL;
      break;
    }

    int32_t width  = seat->op_start_width;
    int32_t height = seat->op_start_height;

    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT)) width -= seat->op_dx;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_RIGHT)) width += seat->op_dx;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP)) height -= seat->op_dy;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_BOTTOM)) height += seat->op_dy;

    river_window_v1_propose_dimensions (
      seat->op_window->window, width > 1 ? width : 1, height > 1 ? height : 1);
    break;
  }

  seat->op_release = false;
}

static void canyon_seat_render (struct canyon_wayland      *wayland,
                                struct canyon_wayland_seat *seat) {
  switch (seat->op) {
  case SEAT_OP_MOVE:
    river_node_v1_set_position (seat->op_window->node,
                                seat->op_start_x + seat->op_dx,
                                seat->op_start_y + seat->op_dy);
    seat->op_window->x = seat->op_start_x + seat->op_dx;
    seat->op_window->y = seat->op_start_y + seat->op_dy;
    break;
  case SEAT_OP_NONE:
    break;
  case SEAT_OP_RESIZE:
    int32_t x = seat->op_start_x;
    int32_t y = seat->op_start_y;

    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT))
      x += seat->op_start_width - seat->op_window->width;
    if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP))
      y += seat->op_start_height - seat->op_window->height;

    river_node_v1_set_position (seat->op_window->node, x, y);
    seat->op_window->x = x;
    seat->op_window->y = y;
    break;
  }
}

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

  struct canyon_wayland_output *output, *output_tmp;
  wl_list_for_each_safe (output, output_tmp, &wayland->outputs,
                         link) if (output->removed) {
    river_output_v1_destroy (output->output);
    wl_list_remove (&output->link);
    free (output);
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
    canyon_window_manage (wayland, window);

  wl_list_for_each (seat, &wayland->seats, link)
    canyon_seat_manage (wayland, seat);

  river_window_manager_v1_manage_finish (window_manager);
}

static void window_manager_listener_render_start (
  void *data, struct river_window_manager_v1 *window_manager) {
  struct canyon_wayland *wayland = data;

  struct canyon_wayland_seat *seat;
  wl_list_for_each (seat, &wayland->seats, link)
    canyon_seat_render (wayland, seat);

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
  wayland_seat->seat = seat;
  wayland_seat->new  = true;
  wl_list_init (&wayland_seat->xkb_bindings);
  wl_list_init (&wayland_seat->pointer_bindings);

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
  .seat             = window_manager_listener_seat,
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

  wl_list_init (&wayland.windows);
  wl_list_init (&wayland.outputs);
  wl_list_init (&wayland.seats);

  struct wl_display  *display  = wl_display_connect (NULL);
  struct wl_registry *registry = wl_display_get_registry (display);

  unsetenv ("WAYLAND_DEBUG");
  signal (SIGCHLD, SIG_IGN);

  wl_registry_add_listener (registry, &registry_listener, &wayland);
  wl_display_roundtrip (display);

  if (window_manager != NULL && xkb_bindings != NULL)
    river_window_manager_v1_add_listener (window_manager,
                                          &window_manager_listener, &wayland);

  while (wl_display_dispatch (display) != -1 && !wayland.exit)
    ;

  wl_registry_destroy (registry);
  wl_display_disconnect (display);
}
