/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * PLEASE KEEP IN SYNC WITH GSETTINGS SCHEMAS!
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COBIWM_COMMON_H
#define COBIWM_COMMON_H

/* Don't include core headers here */
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <clutter/clutter.h>
#include <glib.h>
#include <gtk/gtk.h>

/**
 * SECTION:common
 * @Title: Common
 * @Short_Description: Cobiwm common types
 */

/* This is set in stone and also hard-coded in GDK. */
#define COBIWM_VIRTUAL_CORE_POINTER_ID 2
#define COBIWM_VIRTUAL_CORE_KEYBOARD_ID 3

/**
 * CobiwmFrameFlags:
 * @COBIWM_FRAME_ALLOWS_DELETE: frame allows delete
 * @COBIWM_FRAME_ALLOWS_MENU: frame allows menu
 * @COBIWM_FRAME_ALLOWS_APPMENU: frame allows (fallback) app menu
 * @COBIWM_FRAME_ALLOWS_MINIMIZE: frame allows minimize
 * @COBIWM_FRAME_ALLOWS_MAXIMIZE: frame allows maximize
 * @COBIWM_FRAME_ALLOWS_VERTICAL_RESIZE: frame allows vertical resize
 * @COBIWM_FRAME_ALLOWS_HORIZONTAL_RESIZE: frame allows horizontal resize
 * @COBIWM_FRAME_HAS_FOCUS: frame has focus
 * @COBIWM_FRAME_SHADED: frame is shaded
 * @COBIWM_FRAME_STUCK: frame is stuck
 * @COBIWM_FRAME_MAXIMIZED: frame is maximized
 * @COBIWM_FRAME_ALLOWS_SHADE: frame allows shade
 * @COBIWM_FRAME_ALLOWS_MOVE: frame allows move
 * @COBIWM_FRAME_FULLSCREEN: frame allows fullscreen
 * @COBIWM_FRAME_IS_FLASHING: frame is flashing
 * @COBIWM_FRAME_ABOVE: frame is above
 * @COBIWM_FRAME_TILED_LEFT: frame is tiled to the left
 * @COBIWM_FRAME_TILED_RIGHT: frame is tiled to the right
 */
typedef enum
{
  COBIWM_FRAME_ALLOWS_DELETE            = 1 << 0,
  COBIWM_FRAME_ALLOWS_MENU              = 1 << 1,
  COBIWM_FRAME_ALLOWS_APPMENU           = 1 << 2,
  COBIWM_FRAME_ALLOWS_MINIMIZE          = 1 << 3,
  COBIWM_FRAME_ALLOWS_MAXIMIZE          = 1 << 4,
  COBIWM_FRAME_ALLOWS_VERTICAL_RESIZE   = 1 << 5,
  COBIWM_FRAME_ALLOWS_HORIZONTAL_RESIZE = 1 << 6,
  COBIWM_FRAME_HAS_FOCUS                = 1 << 7,
  COBIWM_FRAME_SHADED                   = 1 << 8,
  COBIWM_FRAME_STUCK                    = 1 << 9,
  COBIWM_FRAME_MAXIMIZED                = 1 << 10,
  COBIWM_FRAME_ALLOWS_SHADE             = 1 << 11,
  COBIWM_FRAME_ALLOWS_MOVE              = 1 << 12,
  COBIWM_FRAME_FULLSCREEN               = 1 << 13,
  COBIWM_FRAME_IS_FLASHING              = 1 << 14,
  COBIWM_FRAME_ABOVE                    = 1 << 15,
  COBIWM_FRAME_TILED_LEFT               = 1 << 16,
  COBIWM_FRAME_TILED_RIGHT              = 1 << 17
} CobiwmFrameFlags;

/**
 * CobiwmGrabOp:
 * @COBIWM_GRAB_OP_NONE: None
 * @COBIWM_GRAB_OP_MOVING: Moving with pointer
 * @COBIWM_GRAB_OP_RESIZING_SE: Resizing SE with pointer
 * @COBIWM_GRAB_OP_RESIZING_S: Resizing S with pointer
 * @COBIWM_GRAB_OP_RESIZING_SW: Resizing SW with pointer
 * @COBIWM_GRAB_OP_RESIZING_N: Resizing N with pointer
 * @COBIWM_GRAB_OP_RESIZING_NE: Resizing NE with pointer
 * @COBIWM_GRAB_OP_RESIZING_NW: Resizing NW with pointer
 * @COBIWM_GRAB_OP_RESIZING_W: Resizing W with pointer
 * @COBIWM_GRAB_OP_RESIZING_E: Resizing E with pointer
 * @COBIWM_GRAB_OP_KEYBOARD_MOVING: Moving with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN: Resizing with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_S: Resizing S with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_N: Resizing N with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_W: Resizing W with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_E: Resizing E with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_SE: Resizing SE with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_NE: Resizing NE with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_SW: Resizing SW with keyboard
 * @COBIWM_GRAB_OP_KEYBOARD_RESIZING_NW: Resizing NS with keyboard
 * @COBIWM_GRAB_OP_COMPOSITOR: Compositor asked for grab
 */

/* The lower 16 bits of the grab operation is its type.
 *
 * Window grab operations have the following layout:
 *
 * 0000  0000  | 0000 0011
 * NSEW  flags | type
 *
 * Flags contains whether the operation is a keyboard operation,
 * and whether the keyboard operation is "unknown".
 *
 * The rest of the flags tell you which direction the resize is
 * going in.
 *
 * If the directions field is 0000, then the operation is a move,
 * not a resize.
 */
enum
{
  COBIWM_GRAB_OP_WINDOW_FLAG_KEYBOARD = 0x0100,
  COBIWM_GRAB_OP_WINDOW_FLAG_UNKNOWN  = 0x0200,
  COBIWM_GRAB_OP_WINDOW_DIR_WEST      = 0x1000,
  COBIWM_GRAB_OP_WINDOW_DIR_EAST      = 0x2000,
  COBIWM_GRAB_OP_WINDOW_DIR_SOUTH     = 0x4000,
  COBIWM_GRAB_OP_WINDOW_DIR_NORTH     = 0x8000,
  COBIWM_GRAB_OP_WINDOW_DIR_MASK      = 0xF000,

  /* WGO = "window grab op". shorthand for below */
  _WGO_K = COBIWM_GRAB_OP_WINDOW_FLAG_KEYBOARD,
  _WGO_U = COBIWM_GRAB_OP_WINDOW_FLAG_UNKNOWN,
  _WGO_W = COBIWM_GRAB_OP_WINDOW_DIR_WEST,
  _WGO_E = COBIWM_GRAB_OP_WINDOW_DIR_EAST,
  _WGO_S = COBIWM_GRAB_OP_WINDOW_DIR_SOUTH,
  _WGO_N = COBIWM_GRAB_OP_WINDOW_DIR_NORTH,
};

#define GRAB_OP_GET_BASE_TYPE(op) (op & 0x00FF)

typedef enum
{
  COBIWM_GRAB_OP_NONE,

  /* Window grab ops. */
  COBIWM_GRAB_OP_WINDOW_BASE,

  /* Special grab op when the compositor asked for a grab */
  COBIWM_GRAB_OP_COMPOSITOR,

  /* For when a Wayland client takes a popup grab. */
  COBIWM_GRAB_OP_WAYLAND_POPUP,

  /* For when the user clicks on a frame button. */
  COBIWM_GRAB_OP_FRAME_BUTTON,

  COBIWM_GRAB_OP_MOVING                     = COBIWM_GRAB_OP_WINDOW_BASE,
  COBIWM_GRAB_OP_RESIZING_NW                = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_W,
  COBIWM_GRAB_OP_RESIZING_N                 = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N,
  COBIWM_GRAB_OP_RESIZING_NE                = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_E,
  COBIWM_GRAB_OP_RESIZING_E                 = COBIWM_GRAB_OP_WINDOW_BASE |          _WGO_E,
  COBIWM_GRAB_OP_RESIZING_SW                = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_W,
  COBIWM_GRAB_OP_RESIZING_S                 = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S,
  COBIWM_GRAB_OP_RESIZING_SE                = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_E,
  COBIWM_GRAB_OP_RESIZING_W                 = COBIWM_GRAB_OP_WINDOW_BASE |          _WGO_W,
  COBIWM_GRAB_OP_KEYBOARD_MOVING            = COBIWM_GRAB_OP_WINDOW_BASE |                   _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN  = COBIWM_GRAB_OP_WINDOW_BASE |                   _WGO_K | _WGO_U,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_NW       = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_W | _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_N        = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N |          _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_NE       = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_N | _WGO_E | _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_E        = COBIWM_GRAB_OP_WINDOW_BASE |          _WGO_E | _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_SW       = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_W | _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_S        = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S |          _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_SE       = COBIWM_GRAB_OP_WINDOW_BASE | _WGO_S | _WGO_E | _WGO_K,
  COBIWM_GRAB_OP_KEYBOARD_RESIZING_W        = COBIWM_GRAB_OP_WINDOW_BASE |          _WGO_W | _WGO_K,
} CobiwmGrabOp;

/**
 * CobiwmCursor:
 * @COBIWM_CURSOR_DEFAULT: Default cursor
 * @COBIWM_CURSOR_NORTH_RESIZE: Resize northern edge cursor
 * @COBIWM_CURSOR_SOUTH_RESIZE: Resize southern edge cursor
 * @COBIWM_CURSOR_WEST_RESIZE: Resize western edge cursor
 * @COBIWM_CURSOR_EAST_RESIZE: Resize eastern edge cursor
 * @COBIWM_CURSOR_SE_RESIZE: Resize south-eastern corner cursor
 * @COBIWM_CURSOR_SW_RESIZE: Resize south-western corner cursor
 * @COBIWM_CURSOR_NE_RESIZE: Resize north-eastern corner cursor
 * @COBIWM_CURSOR_NW_RESIZE: Resize north-western corner cursor
 * @COBIWM_CURSOR_MOVE_OR_RESIZE_WINDOW: Move or resize cursor
 * @COBIWM_CURSOR_BUSY: Busy cursor
 * @COBIWM_CURSOR_DND_IN_DRAG: DND in drag cursor
 * @COBIWM_CURSOR_DND_MOVE: DND move cursor
 * @COBIWM_CURSOR_DND_COPY: DND copy cursor
 * @COBIWM_CURSOR_DND_UNSUPPORTED_TARGET: DND unsupported target
 * @COBIWM_CURSOR_POINTING_HAND: pointing hand
 * @COBIWM_CURSOR_CROSSHAIR: crosshair (action forbidden)
 * @COBIWM_CURSOR_IBEAM: I-beam (text input)
 */
typedef enum
{
  COBIWM_CURSOR_NONE = 0,
  COBIWM_CURSOR_DEFAULT,
  COBIWM_CURSOR_NORTH_RESIZE,
  COBIWM_CURSOR_SOUTH_RESIZE,
  COBIWM_CURSOR_WEST_RESIZE,
  COBIWM_CURSOR_EAST_RESIZE,
  COBIWM_CURSOR_SE_RESIZE,
  COBIWM_CURSOR_SW_RESIZE,
  COBIWM_CURSOR_NE_RESIZE,
  COBIWM_CURSOR_NW_RESIZE,
  COBIWM_CURSOR_MOVE_OR_RESIZE_WINDOW,
  COBIWM_CURSOR_BUSY,
  COBIWM_CURSOR_DND_IN_DRAG,
  COBIWM_CURSOR_DND_MOVE,
  COBIWM_CURSOR_DND_COPY,
  COBIWM_CURSOR_DND_UNSUPPORTED_TARGET,
  COBIWM_CURSOR_POINTING_HAND,
  COBIWM_CURSOR_CROSSHAIR,
  COBIWM_CURSOR_IBEAM,
  COBIWM_CURSOR_LAST
} CobiwmCursor;

/**
 * CobiwmFrameType:
 * @COBIWM_FRAME_TYPE_NORMAL: Normal frame
 * @COBIWM_FRAME_TYPE_DIALOG: Dialog frame
 * @COBIWM_FRAME_TYPE_MODAL_DIALOG: Modal dialog frame
 * @COBIWM_FRAME_TYPE_UTILITY: Utility frame
 * @COBIWM_FRAME_TYPE_MENU: Menu frame
 * @COBIWM_FRAME_TYPE_BORDER: Border frame
 * @COBIWM_FRAME_TYPE_ATTACHED: Attached frame
 * @COBIWM_FRAME_TYPE_LAST: Marks the end of the #CobiwmFrameType enumeration
 */
typedef enum
{
  COBIWM_FRAME_TYPE_NORMAL,
  COBIWM_FRAME_TYPE_DIALOG,
  COBIWM_FRAME_TYPE_MODAL_DIALOG,
  COBIWM_FRAME_TYPE_UTILITY,
  COBIWM_FRAME_TYPE_MENU,
  COBIWM_FRAME_TYPE_BORDER,
  COBIWM_FRAME_TYPE_ATTACHED,
  COBIWM_FRAME_TYPE_LAST
} CobiwmFrameType;

/**
 * CobiwmVirtualModifier:
 * @COBIWM_VIRTUAL_SHIFT_MASK: Shift mask
 * @COBIWM_VIRTUAL_CONTROL_MASK: Control mask
 * @COBIWM_VIRTUAL_ALT_MASK: Alt mask
 * @COBIWM_VIRTUAL_COBIWM_MASK: Cobiwm mask
 * @COBIWM_VIRTUAL_SUPER_MASK: Super mask
 * @COBIWM_VIRTUAL_HYPER_MASK: Hyper mask
 * @COBIWM_VIRTUAL_MOD2_MASK: Mod2 mask
 * @COBIWM_VIRTUAL_MOD3_MASK: Mod3 mask
 * @COBIWM_VIRTUAL_MOD4_MASK: Mod4 mask
 * @COBIWM_VIRTUAL_MOD5_MASK: Mod5 mask
 */
typedef enum
{
  /* Create gratuitous divergence from regular
   * X mod bits, to be sure we find bugs
   */
  COBIWM_VIRTUAL_SHIFT_MASK    = 1 << 5,
  COBIWM_VIRTUAL_CONTROL_MASK  = 1 << 6,
  COBIWM_VIRTUAL_ALT_MASK      = 1 << 7,
  COBIWM_VIRTUAL_COBIWM_MASK     = 1 << 8,
  COBIWM_VIRTUAL_SUPER_MASK    = 1 << 9,
  COBIWM_VIRTUAL_HYPER_MASK    = 1 << 10,
  COBIWM_VIRTUAL_MOD2_MASK     = 1 << 11,
  COBIWM_VIRTUAL_MOD3_MASK     = 1 << 12,
  COBIWM_VIRTUAL_MOD4_MASK     = 1 << 13,
  COBIWM_VIRTUAL_MOD5_MASK     = 1 << 14
} CobiwmVirtualModifier;

/**
 * CobiwmDirection:
 * @COBIWM_DIRECTION_LEFT: Left
 * @COBIWM_DIRECTION_RIGHT: Right
 * @COBIWM_DIRECTION_TOP: Top
 * @COBIWM_DIRECTION_BOTTOM: Bottom
 * @COBIWM_DIRECTION_UP: Up
 * @COBIWM_DIRECTION_DOWN: Down
 * @COBIWM_DIRECTION_HORIZONTAL: Horizontal
 * @COBIWM_DIRECTION_VERTICAL: Vertical
 */

/* Relative directions or sides seem to come up all over the place... */
/* FIXME: Replace
 *   screen.[ch]:CobiwmScreenDirection,
 *   workspace.[ch]:CobiwmMotionDirection,
 * with the use of CobiwmDirection.
 */
typedef enum
{
  COBIWM_DIRECTION_LEFT       = 1 << 0,
  COBIWM_DIRECTION_RIGHT      = 1 << 1,
  COBIWM_DIRECTION_TOP        = 1 << 2,
  COBIWM_DIRECTION_BOTTOM     = 1 << 3,

  /* Some aliases for making code more readable for various circumstances. */
  COBIWM_DIRECTION_UP         = COBIWM_DIRECTION_TOP,
  COBIWM_DIRECTION_DOWN       = COBIWM_DIRECTION_BOTTOM,

  /* A few more definitions using aliases */
  COBIWM_DIRECTION_HORIZONTAL = COBIWM_DIRECTION_LEFT | COBIWM_DIRECTION_RIGHT,
  COBIWM_DIRECTION_VERTICAL   = COBIWM_DIRECTION_UP   | COBIWM_DIRECTION_DOWN,
} CobiwmDirection;

/**
 * CobiwmMotionDirection:
 * @COBIWM_MOTION_UP: Upwards motion
 * @COBIWM_MOTION_DOWN: Downwards motion
 * @COBIWM_MOTION_LEFT: Motion to the left
 * @COBIWM_MOTION_RIGHT: Motion to the right
 * @COBIWM_MOTION_UP_LEFT: Motion up and to the left
 * @COBIWM_MOTION_UP_RIGHT: Motion up and to the right
 * @COBIWM_MOTION_DOWN_LEFT: Motion down and to the left
 * @COBIWM_MOTION_DOWN_RIGHT: Motion down and to the right
 */

/* Negative to avoid conflicting with real workspace
 * numbers
 */
typedef enum
{
  COBIWM_MOTION_UP = -1,
  COBIWM_MOTION_DOWN = -2,
  COBIWM_MOTION_LEFT = -3,
  COBIWM_MOTION_RIGHT = -4,
  /* These are only used for effects */
  COBIWM_MOTION_UP_LEFT = -5,
  COBIWM_MOTION_UP_RIGHT = -6,
  COBIWM_MOTION_DOWN_LEFT = -7,
  COBIWM_MOTION_DOWN_RIGHT = -8
} CobiwmMotionDirection;

/**
 * CobiwmSide:
 * @COBIWM_SIDE_LEFT: Left side
 * @COBIWM_SIDE_RIGHT: Right side
 * @COBIWM_SIDE_TOP: Top side
 * @COBIWM_SIDE_BOTTOM: Bottom side
 */

/* Sometimes we want to talk about sides instead of directions; note
 * that the values must be as follows or cobiwm_window_update_struts()
 * won't work. Using these values also is a safety blanket since
 * CobiwmDirection used to be used as a side.
 */
typedef enum
{
  COBIWM_SIDE_LEFT            = COBIWM_DIRECTION_LEFT,
  COBIWM_SIDE_RIGHT           = COBIWM_DIRECTION_RIGHT,
  COBIWM_SIDE_TOP             = COBIWM_DIRECTION_TOP,
  COBIWM_SIDE_BOTTOM          = COBIWM_DIRECTION_BOTTOM
} CobiwmSide;

/**
 * CobiwmButtonFunction:
 * @COBIWM_BUTTON_FUNCTION_MENU: Menu
 * @COBIWM_BUTTON_FUNCTION_MINIMIZE: Minimize
 * @COBIWM_BUTTON_FUNCTION_MAXIMIZE: Maximize
 * @COBIWM_BUTTON_FUNCTION_CLOSE: Close
 * @COBIWM_BUTTON_FUNCTION_LAST: Marks the end of the #CobiwmButtonFunction enumeration
 *
 * Function a window button can have.  Note, you can't add stuff here
 * without extending the theme format to draw a new function and
 * breaking all existing themes.
 */
typedef enum
{
  COBIWM_BUTTON_FUNCTION_MENU,
  COBIWM_BUTTON_FUNCTION_MINIMIZE,
  COBIWM_BUTTON_FUNCTION_MAXIMIZE,
  COBIWM_BUTTON_FUNCTION_CLOSE,
  COBIWM_BUTTON_FUNCTION_APPMENU,
  COBIWM_BUTTON_FUNCTION_LAST
} CobiwmButtonFunction;

#define MAX_BUTTONS_PER_CORNER COBIWM_BUTTON_FUNCTION_LAST

/* Keep array size in sync with MAX_BUTTONS_PER_CORNER */
/**
 * CobiwmButtonLayout:
 * @left_buttons: (array fixed-size=5):
 * @right_buttons: (array fixed-size=5):
 * @left_buttons_has_spacer: (array fixed-size=5):
 * @right_buttons_has_spacer: (array fixed-size=5):
 */
typedef struct _CobiwmButtonLayout CobiwmButtonLayout;
struct _CobiwmButtonLayout
{
  /* buttons in the group on the left side */
  CobiwmButtonFunction left_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean left_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];

  /* buttons in the group on the right side */
  CobiwmButtonFunction right_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean right_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];
};

/**
 * CobiwmWindowMenuType:
 * @COBIWM_WINDOW_MENU_WM: the window manager menu
 * @COBIWM_WINDOW_MENU_APP: the (fallback) app menu
 *
 * Menu the compositor should display for a given window
 */
typedef enum
{
  COBIWM_WINDOW_MENU_WM,
  COBIWM_WINDOW_MENU_APP
} CobiwmWindowMenuType;

/**
 * CobiwmFrameBorders:
 * @visible: inner visible portion of frame border
 * @invisible: outer invisible portion of frame border
 * @total: sum of the two borders above
 */
typedef struct _CobiwmFrameBorders CobiwmFrameBorders;
struct _CobiwmFrameBorders
{
  /* The frame border is made up of two pieces - an inner visible portion
   * and an outer portion that is invisible but responds to events.
   */
  GtkBorder visible;
  GtkBorder invisible;

  /* For convenience, we have a "total" border which is equal to the sum
   * of the two borders above. */
  GtkBorder total;
};

/* sets all dimensions to zero */
void cobiwm_frame_borders_clear (CobiwmFrameBorders *self);

/* should investigate changing these to whatever most apps use */
#define COBIWM_ICON_WIDTH 96
#define COBIWM_ICON_HEIGHT 96
#define COBIWM_MINI_ICON_WIDTH 16
#define COBIWM_MINI_ICON_HEIGHT 16

#define COBIWM_DEFAULT_ICON_NAME "window"

/* Main loop priorities determine when activity in the GLib
 * will take precendence over the others. Priorities are sometimes
 * used to enforce ordering: give A a higher priority than B if
 * A must occur before B. But that poses a problem since then
 * if A occurs frequently enough, B will never occur.
 *
 * Anything we want to occur more or less immediately should
 * have a priority of G_PRIORITY_DEFAULT. When we want to
 * coelesce multiple things together, the appropriate place to
 * do it is usually COBIWM_PRIORITY_BEFORE_REDRAW.
 *
 * Note that its usually better to use cobiwm_later_add() rather
 * than calling g_idle_add() directly; this will make sure things
 * get run when added from a clutter event handler without
 * waiting for another repaint cycle.
 *
 * If something has a priority lower than the redraw priority
 * (such as a default priority idle), then it may be arbitrarily
 * delayed. This happens if the screen is updating rapidly: we
 * are spending all our time either redrawing or waiting for a
 * vblank-synced buffer swap. (When X is improved to allow
 * clutter to do the buffer-swap asychronously, this will get
 * better.)
 */

/* G_PRIORITY_DEFAULT:
 *  events
 *  many timeouts
 */

/* GTK_PRIORITY_RESIZE:         (G_PRIORITY_HIGH_IDLE + 10) */
#define COBIWM_PRIORITY_RESIZE    (G_PRIORITY_HIGH_IDLE + 15)
/* GTK_PRIORITY_REDRAW:         (G_PRIORITY_HIGH_IDLE + 20) */

#define COBIWM_PRIORITY_BEFORE_REDRAW  (G_PRIORITY_HIGH_IDLE + 40)
/*  calc-showing idle
 *  update-icon idle
 */

/* CLUTTER_PRIORITY_REDRAW:     (G_PRIORITY_HIGH_IDLE + 50) */
#define COBIWM_PRIORITY_REDRAW    (G_PRIORITY_HIGH_IDLE + 50)

/* ==== Anything below here can be starved arbitrarily ==== */

/* G_PRIORITY_DEFAULT_IDLE:
 *  Cobiwm plugin unloading
 */

#define COBIWM_PRIORITY_PREFS_NOTIFY   (G_PRIORITY_DEFAULT_IDLE + 10)

/************************************************************/

#define POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

/**
 * CobiwmStackLayer:
 * @COBIWM_LAYER_DESKTOP: Desktop layer
 * @COBIWM_LAYER_BOTTOM: Bottom layer
 * @COBIWM_LAYER_NORMAL: Normal layer
 * @COBIWM_LAYER_TOP: Top layer
 * @COBIWM_LAYER_DOCK: Dock layer
 * @COBIWM_LAYER_FULLSCREEN: Fullscreen layer
 * @COBIWM_LAYER_OVERRIDE_REDIRECT: Override-redirect layer
 * @COBIWM_LAYER_LAST: Marks the end of the #CobiwmStackLayer enumeration
 *
 * Layers a window can be in.
 * These MUST be in the order of stacking.
 */
typedef enum
{
  COBIWM_LAYER_DESKTOP	       = 0,
  COBIWM_LAYER_BOTTOM	       = 1,
  COBIWM_LAYER_NORMAL	       = 2,
  COBIWM_LAYER_TOP	       = 4, /* Same as DOCK; see EWMH and bug 330717 */
  COBIWM_LAYER_DOCK	       = 4,
  COBIWM_LAYER_FULLSCREEN	       = 5,
  COBIWM_LAYER_OVERRIDE_REDIRECT = 7,
  COBIWM_LAYER_LAST	       = 8
} CobiwmStackLayer;

#endif
