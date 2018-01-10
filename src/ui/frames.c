/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwmcity window frame manager widget */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
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

#include <config.h>
#include <math.h>
#include <string.h>
#include <boxes.h>
#include "frames.h"
#include <util.h>
#include "core.h"
#include <theme.h>
#include <prefs.h>
#include "ui.h"

#include "core/window-private.h"
#include "core/frame.h"
#include "x11/window-x11.h"
#include "x11/window-x11-private.h"

#include <cairo-xlib.h>

#define DEFAULT_INNER_BUTTON_BORDER 3

static void cobiwm_frames_destroy       (GtkWidget       *object);
static void cobiwm_frames_finalize      (GObject         *object);
static void cobiwm_frames_style_updated (GtkWidget       *widget);

static gboolean cobiwm_frames_draw                  (GtkWidget           *widget,
                                                   cairo_t             *cr);

static void cobiwm_ui_frame_attach_style (CobiwmUIFrame *frame);

static void cobiwm_ui_frame_paint        (CobiwmUIFrame  *frame,
                                        cairo_t      *cr);

static void cobiwm_ui_frame_calc_geometry (CobiwmUIFrame       *frame,
                                         CobiwmFrameGeometry *fgeom);

static void cobiwm_ui_frame_update_prelit_control (CobiwmUIFrame     *frame,
                                                 CobiwmFrameControl control);

static void cobiwm_frames_font_changed          (CobiwmFrames *frames);
static void cobiwm_frames_button_layout_changed (CobiwmFrames *frames);


static GdkRectangle*    control_rect (CobiwmFrameControl   control,
                                      CobiwmFrameGeometry *fgeom);
static CobiwmFrameControl get_control  (CobiwmUIFrame       *frame,
                                      int                x,
                                      int                y);

G_DEFINE_TYPE (CobiwmFrames, cobiwm_frames, GTK_TYPE_WINDOW);

static GObject *
cobiwm_frames_constructor (GType                  gtype,
                         guint                  n_properties,
                         GObjectConstructParam *properties)
{
  GObject *object;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (cobiwm_frames_parent_class);
  object = gobject_class->constructor (gtype, n_properties, properties);

  g_object_set (object,
                "type", GTK_WINDOW_POPUP,
                NULL);

  return object;
}

static void
cobiwm_frames_class_init (CobiwmFramesClass *class)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  widget_class = (GtkWidgetClass*) class;

  gobject_class->constructor = cobiwm_frames_constructor;
  gobject_class->finalize = cobiwm_frames_finalize;

  widget_class->destroy = cobiwm_frames_destroy;

  widget_class->style_updated = cobiwm_frames_style_updated;

  widget_class->draw = cobiwm_frames_draw;
}

static gint
unsigned_long_equal (gconstpointer v1,
                     gconstpointer v2)
{
  return *((const gulong*) v1) == *((const gulong*) v2);
}

static guint
unsigned_long_hash (gconstpointer v)
{
  gulong val = * (const gulong *) v;

  /* I'm not sure this works so well. */
#if GLIB_SIZEOF_LONG > 4
  return (guint) (val ^ (val >> 32));
#else
  return val;
#endif
}

static void
prefs_changed_callback (CobiwmPreference pref,
                        void          *data)
{
  switch (pref)
    {
    case COBIWM_PREF_TITLEBAR_FONT:
      cobiwm_frames_font_changed (COBIWM_FRAMES (data));
      break;
    case COBIWM_PREF_BUTTON_LAYOUT:
      cobiwm_frames_button_layout_changed (COBIWM_FRAMES (data));
      break;
    default:
      break;
    }
}

static void
invalidate_whole_window (CobiwmUIFrame *frame)
{
  gdk_window_invalidate_rect (frame->window, NULL, FALSE);
}

static CobiwmStyleInfo *
cobiwm_frames_get_theme_variant (CobiwmFrames  *frames,
                               const gchar *variant)
{
  CobiwmStyleInfo *style_info;

  style_info = g_hash_table_lookup (frames->style_variants, variant);
  if (style_info == NULL)
    {
      style_info = cobiwm_theme_create_style_info (gtk_widget_get_screen (GTK_WIDGET (frames)), variant);
      g_hash_table_insert (frames->style_variants, g_strdup (variant), style_info);
    }

  return style_info;
}

static void
update_style_contexts (CobiwmFrames *frames)
{
  CobiwmStyleInfo *style_info;
  GList *variants, *variant;
  GdkScreen *screen;

  screen = gtk_widget_get_screen (GTK_WIDGET (frames));

  if (frames->normal_style)
    cobiwm_style_info_unref (frames->normal_style);
  frames->normal_style = cobiwm_theme_create_style_info (screen, NULL);

  variants = g_hash_table_get_keys (frames->style_variants);
  for (variant = variants; variant; variant = variants->next)
    {
      style_info = cobiwm_theme_create_style_info (screen, (char *)variant->data);
      g_hash_table_insert (frames->style_variants,
                           g_strdup (variant->data), style_info);
    }
  g_list_free (variants);
}

static void
cobiwm_frames_init (CobiwmFrames *frames)
{
  frames->text_heights = g_hash_table_new (NULL, NULL);

  frames->frames = g_hash_table_new (unsigned_long_hash, unsigned_long_equal);

  frames->style_variants = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, (GDestroyNotify)cobiwm_style_info_unref);

  update_style_contexts (frames);

  cobiwm_prefs_add_listener (prefs_changed_callback, frames);
}

static void
listify_func (gpointer key, gpointer value, gpointer data)
{
  GSList **listp;

  listp = data;
  *listp = g_slist_prepend (*listp, value);
}

static void
cobiwm_frames_destroy (GtkWidget *object)
{
  GSList *winlist;
  GSList *tmp;
  CobiwmFrames *frames;

  frames = COBIWM_FRAMES (object);

  winlist = NULL;
  g_hash_table_foreach (frames->frames, listify_func, &winlist);

  /* Unmanage all frames */
  for (tmp = winlist; tmp != NULL; tmp = tmp->next)
    {
      CobiwmUIFrame *frame = tmp->data;
      cobiwm_ui_frame_unmanage (frame);
    }
  g_slist_free (winlist);

  if (frames->normal_style)
    {
      cobiwm_style_info_unref (frames->normal_style);
      frames->normal_style = NULL;
    }

  if (frames->style_variants)
    {
      g_hash_table_destroy (frames->style_variants);
      frames->style_variants = NULL;
    }

  GTK_WIDGET_CLASS (cobiwm_frames_parent_class)->destroy (object);
}

static void
cobiwm_frames_finalize (GObject *object)
{
  CobiwmFrames *frames;

  frames = COBIWM_FRAMES (object);

  cobiwm_prefs_remove_listener (prefs_changed_callback, frames);

  g_hash_table_destroy (frames->text_heights);

  g_assert (g_hash_table_size (frames->frames) == 0);
  g_hash_table_destroy (frames->frames);

  G_OBJECT_CLASS (cobiwm_frames_parent_class)->finalize (object);
}

static void
queue_recalc_func (gpointer key, gpointer value, gpointer data)
{
  CobiwmUIFrame *frame = value;

  invalidate_whole_window (frame);
  cobiwm_core_queue_frame_resize (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                frame->xwindow);

  g_clear_object (&frame->text_layout);
}

static void
cobiwm_frames_font_changed (CobiwmFrames *frames)
{
  if (g_hash_table_size (frames->text_heights) > 0)
    {
      g_hash_table_destroy (frames->text_heights);
      frames->text_heights = g_hash_table_new (NULL, NULL);
    }

  /* Queue a draw/resize on all frames */
  g_hash_table_foreach (frames->frames,
                        queue_recalc_func, frames);

}

static void
queue_draw_func (gpointer key, gpointer value, gpointer data)
{
  CobiwmUIFrame *frame = value;
  invalidate_whole_window (frame);
}

static void
cobiwm_frames_button_layout_changed (CobiwmFrames *frames)
{
  g_hash_table_foreach (frames->frames,
                        queue_draw_func, frames);
}

static void
reattach_style_func (gpointer key, gpointer value, gpointer data)
{
  CobiwmUIFrame *frame = value;
  cobiwm_ui_frame_attach_style (frame);
}

static void
cobiwm_frames_style_updated  (GtkWidget *widget)
{
  CobiwmFrames *frames;

  frames = COBIWM_FRAMES (widget);

  cobiwm_frames_font_changed (frames);

  update_style_contexts (frames);

  g_hash_table_foreach (frames->frames, reattach_style_func, NULL);

  cobiwm_retheme_all ();

  GTK_WIDGET_CLASS (cobiwm_frames_parent_class)->style_updated (widget);
}

static void
cobiwm_ui_frame_ensure_layout (CobiwmUIFrame    *frame,
                             CobiwmFrameType   type)
{
  CobiwmFrames *frames = frame->frames;
  GtkWidget *widget;
  CobiwmFrameLayout *layout;

  widget = GTK_WIDGET (frames);

  g_return_if_fail (gtk_widget_get_realized (widget));

  layout = cobiwm_theme_get_frame_layout (cobiwm_theme_get_default (), type);

  if (layout != frame->cache_layout)
    g_clear_object (&frame->text_layout);

  frame->cache_layout = layout;

  if (frame->text_layout == NULL)
    {
      gpointer key, value;
      PangoFontDescription *font_desc;
      int size;

      frame->text_layout = gtk_widget_create_pango_layout (widget, frame->title);

      pango_layout_set_ellipsize (frame->text_layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_auto_dir (frame->text_layout, FALSE);
      pango_layout_set_single_paragraph_mode (frame->text_layout, TRUE);

      font_desc = cobiwm_style_info_create_font_desc (frame->style_info);
      cobiwm_frame_layout_apply_scale (layout, font_desc);

      size = pango_font_description_get_size (font_desc);

      if (g_hash_table_lookup_extended (frames->text_heights,
                                        GINT_TO_POINTER (size),
                                        &key, &value))
        {
          frame->text_height = GPOINTER_TO_INT (value);
        }
      else
        {
          frame->text_height =
            cobiwm_pango_font_desc_get_text_height (font_desc,
                                                  gtk_widget_get_pango_context (widget));

          g_hash_table_replace (frames->text_heights,
                                GINT_TO_POINTER (size),
                                GINT_TO_POINTER (frame->text_height));
        }

      pango_layout_set_font_description (frame->text_layout,
                                         font_desc);

      pango_font_description_free (font_desc);
    }
}

static void
cobiwm_ui_frame_calc_geometry (CobiwmUIFrame       *frame,
                             CobiwmFrameGeometry *fgeom)
{
  CobiwmFrameFlags flags;
  CobiwmFrameType type;
  CobiwmButtonLayout button_layout;
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (frame->cobiwm_window);
  CobiwmWindowX11Private *priv = window_x11->priv;

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);
  type = cobiwm_window_get_frame_type (frame->cobiwm_window);

  cobiwm_ui_frame_ensure_layout (frame, type);

  cobiwm_prefs_get_button_layout (&button_layout);

  cobiwm_theme_calc_geometry (cobiwm_theme_get_default (),
                            frame->style_info,
                            type,
                            frame->text_height,
                            flags,
                            priv->client_rect.width,
                            priv->client_rect.height,
                            &button_layout,
                            fgeom);
}

CobiwmFrames*
cobiwm_frames_new (int screen_number)
{
  GdkScreen *screen;
  CobiwmFrames *frames;

  screen = gdk_display_get_default_screen (gdk_display_get_default ());

  frames = g_object_new (COBIWM_TYPE_FRAMES,
                         "screen", screen,
                         "type", GTK_WINDOW_POPUP,
                         NULL);

  /* Put the window at an arbitrary offscreen location; the one place
   * it can't be is at -100x-100, since the cobiwm_window_new() will
   * mistake it for a window created via cobiwm_create_offscreen_window()
   * and ignore it, and we need this window to get frame-synchronization
   * messages so that GTK+'s style change handling works.
   */
  gtk_window_move (GTK_WINDOW (frames), -200, -200);
  gtk_window_resize (GTK_WINDOW (frames), 1, 1);

  return frames;
}

static const char *
get_global_theme_variant (CobiwmFrames *frames)
{
  GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (frames));
  GtkSettings *settings = gtk_settings_get_for_screen (screen);
  gboolean dark_theme_requested;

  g_object_get (settings,
                "gtk-application-prefer-dark-theme", &dark_theme_requested,
                NULL);

  if (dark_theme_requested)
    return "dark";

  return NULL;
}

/* In order to use a style with a window it has to be attached to that
 * window. Actually, the colormaps just have to match, but since GTK+
 * already takes care of making sure that its cheap to attach a style
 * to multiple windows with the same colormap, we can just go ahead
 * and attach separately for each window.
 */
static void
cobiwm_ui_frame_attach_style (CobiwmUIFrame *frame)
{
  CobiwmFrames *frames = frame->frames;
  const char *variant;

  if (frame->style_info != NULL)
    cobiwm_style_info_unref (frame->style_info);

  variant = frame->cobiwm_window->gtk_theme_variant;
  if (variant == NULL)
    variant = get_global_theme_variant (frame->frames);;

  if (variant == NULL || *variant == '\0')
    frame->style_info = cobiwm_style_info_ref (frames->normal_style);
  else
    frame->style_info = cobiwm_style_info_ref (cobiwm_frames_get_theme_variant (frames,
                                                                            variant));
}

CobiwmUIFrame *
cobiwm_frames_manage_window (CobiwmFrames *frames,
                           CobiwmWindow *cobiwm_window,
                           Window      xwindow,
                           GdkWindow  *window)
{
  CobiwmUIFrame *frame;

  g_assert (window);

  frame = g_new (CobiwmUIFrame, 1);

  frame->frames = frames;
  frame->window = window;

  gdk_window_set_user_data (frame->window, frames);

  frame->style_info = NULL;

  /* Don't set event mask here, it's in frame.c */

  frame->xwindow = xwindow;
  frame->cobiwm_window = cobiwm_window;
  frame->cache_layout = NULL;
  frame->text_layout = NULL;
  frame->text_height = -1;
  frame->title = NULL;
  frame->prelit_control = COBIWM_FRAME_CONTROL_NONE;
  frame->button_state = COBIWM_BUTTON_STATE_NORMAL;

  cobiwm_core_grab_buttons (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), frame->xwindow);

  g_hash_table_replace (frames->frames, &frame->xwindow, frame);

  return frame;
}

void
cobiwm_ui_frame_unmanage (CobiwmUIFrame *frame)
{
  CobiwmFrames *frames = frame->frames;

  /* restore the cursor */
  cobiwm_core_set_screen_cursor (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                               frame->xwindow,
                               COBIWM_CURSOR_DEFAULT);

  gdk_window_set_user_data (frame->window, NULL);

  g_hash_table_remove (frames->frames, &frame->xwindow);

  cobiwm_style_info_unref (frame->style_info);

  gdk_window_destroy (frame->window);

  if (frame->text_layout)
    g_object_unref (G_OBJECT (frame->text_layout));

  g_free (frame->title);

  g_free (frame);
}

void
cobiwm_ui_frame_get_borders (CobiwmUIFrame *frame,
                           CobiwmFrameBorders *borders)
{
  CobiwmFrameFlags flags;
  CobiwmFrameType type;

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);
  type = cobiwm_window_get_frame_type (frame->cobiwm_window);

  g_return_if_fail (type < COBIWM_FRAME_TYPE_LAST);

  cobiwm_ui_frame_ensure_layout (frame, type);

  /* We can't get the full geometry, because that depends on
   * the client window size and probably we're being called
   * by the core move/resize code to decide on the client
   * window size
   */
  cobiwm_theme_get_frame_borders (cobiwm_theme_get_default (),
                                frame->style_info,
                                type,
                                frame->text_height,
                                flags,
                                borders);
}

/* The client rectangle surrounds client window; it subtracts both
 * the visible and invisible borders from the frame window's size.
 */
static void
get_client_rect (CobiwmFrameGeometry     *fgeom,
                 cairo_rectangle_int_t *rect)
{
  rect->x = fgeom->borders.total.left;
  rect->y = fgeom->borders.total.top;
  rect->width = fgeom->width - fgeom->borders.total.right - rect->x;
  rect->height = fgeom->height - fgeom->borders.total.bottom - rect->y;
}

/* The visible frame rectangle surrounds the visible portion of the
 * frame window; it subtracts only the invisible borders from the frame
 * window's size.
 */
static void
get_visible_frame_rect (CobiwmFrameGeometry     *fgeom,
                        cairo_rectangle_int_t *rect)
{
  rect->x = fgeom->borders.invisible.left;
  rect->y = fgeom->borders.invisible.top;
  rect->width = fgeom->width - fgeom->borders.invisible.right - rect->x;
  rect->height = fgeom->height - fgeom->borders.invisible.bottom - rect->y;
}

static cairo_region_t *
get_visible_region (CobiwmUIFrame       *frame,
                    CobiwmFrameGeometry *fgeom)
{
  cairo_region_t *corners_region;
  cairo_region_t *visible_region;
  cairo_rectangle_int_t rect;
  cairo_rectangle_int_t frame_rect;

  corners_region = cairo_region_create ();
  get_visible_frame_rect (fgeom, &frame_rect);

  if (fgeom->top_left_corner_rounded_radius != 0)
    {
      const int corner = fgeom->top_left_corner_rounded_radius;
      const float radius = corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x;
          rect.y = frame_rect.y + i;
          rect.width = width;
          rect.height = 1;

          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->top_right_corner_rounded_radius != 0)
    {
      const int corner = fgeom->top_right_corner_rounded_radius;
      const float radius = corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x + frame_rect.width - width;
          rect.y = frame_rect.y + i;
          rect.width = width;
          rect.height = 1;

          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->bottom_left_corner_rounded_radius != 0)
    {
      const int corner = fgeom->bottom_left_corner_rounded_radius;
      const float radius = corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x;
          rect.y = frame_rect.y + frame_rect.height - i - 1;
          rect.width = width;
          rect.height = 1;

          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->bottom_right_corner_rounded_radius != 0)
    {
      const int corner = fgeom->bottom_right_corner_rounded_radius;
      const float radius = corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x + frame_rect.width - width;
          rect.y = frame_rect.y + frame_rect.height - i - 1;
          rect.width = width;
          rect.height = 1;

          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  visible_region = cairo_region_create_rectangle (&frame_rect);
  cairo_region_subtract (visible_region, corners_region);
  cairo_region_destroy (corners_region);

  return visible_region;
}

cairo_region_t *
cobiwm_ui_frame_get_bounds (CobiwmUIFrame *frame)
{
  CobiwmFrameGeometry fgeom;
  cobiwm_ui_frame_calc_geometry (frame, &fgeom);
  return get_visible_region (frame, &fgeom);
}

void
cobiwm_ui_frame_move_resize (CobiwmUIFrame *frame,
                           int x, int y, int width, int height)
{
  int old_width, old_height;

  old_width = gdk_window_get_width (frame->window);
  old_height = gdk_window_get_height (frame->window);

  gdk_window_move_resize (frame->window, x, y, width, height);

  if (old_width != width || old_height != height)
    invalidate_whole_window (frame);
}

void
cobiwm_ui_frame_queue_draw (CobiwmUIFrame *frame)
{
  invalidate_whole_window (frame);
}

void
cobiwm_ui_frame_set_title (CobiwmUIFrame *frame,
                         const char *title)
{
  g_free (frame->title);
  frame->title = g_strdup (title);

  g_clear_object (&frame->text_layout);

  invalidate_whole_window (frame);
}

void
cobiwm_ui_frame_update_style (CobiwmUIFrame *frame)
{
  cobiwm_ui_frame_attach_style (frame);
  invalidate_whole_window (frame);
}

static void
redraw_control (CobiwmUIFrame *frame,
                CobiwmFrameControl control)
{
  CobiwmFrameGeometry fgeom;
  GdkRectangle *rect;

  cobiwm_ui_frame_calc_geometry (frame, &fgeom);

  rect = control_rect (control, &fgeom);

  gdk_window_invalidate_rect (frame->window, rect, FALSE);
}

static gboolean
cobiwm_frame_titlebar_event (CobiwmUIFrame *frame,
                           ClutterButtonEvent *event,
                           int action)
{
  CobiwmFrameFlags flags;
  Display *display;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);

  switch (action)
    {
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_SHADE:
      {
        if (flags & COBIWM_FRAME_ALLOWS_SHADE)
          {
            if (flags & COBIWM_FRAME_SHADED)
              cobiwm_window_unshade (frame->cobiwm_window, event->time);
            else
              cobiwm_window_shade (frame->cobiwm_window, event->time);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE:
      {
        if (flags & COBIWM_FRAME_ALLOWS_MAXIMIZE)
          {
            cobiwm_core_toggle_maximize (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY:
      {
        if (flags & COBIWM_FRAME_ALLOWS_MAXIMIZE)
          {
            cobiwm_core_toggle_maximize_horizontally (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY:
      {
        if (flags & COBIWM_FRAME_ALLOWS_MAXIMIZE)
          {
            cobiwm_core_toggle_maximize_vertically (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MINIMIZE:
      {
        if (flags & COBIWM_FRAME_ALLOWS_MINIMIZE)
          cobiwm_window_minimize (frame->cobiwm_window);
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_NONE:
      /* Yaay, a sane user that doesn't use that other weird crap! */
      break;

    case G_DESKTOP_TITLEBAR_ACTION_LOWER:
      cobiwm_core_user_lower_and_unfocus (display,
                                        frame->xwindow,
                                        event->time);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MENU:
      cobiwm_core_show_window_menu (display,
                                  frame->xwindow,
                                  COBIWM_WINDOW_MENU_WM,
                                  event->x,
                                  event->y,
                                  event->time);
      break;
    }

  return TRUE;
}

static gboolean
cobiwm_frame_double_click_event (CobiwmUIFrame  *frame,
                               ClutterButtonEvent *event)
{
  int action = cobiwm_prefs_get_action_double_click_titlebar ();

  return cobiwm_frame_titlebar_event (frame, event, action);
}

static gboolean
cobiwm_frame_middle_click_event (CobiwmUIFrame *frame,
                               ClutterButtonEvent *event)
{
  int action = cobiwm_prefs_get_action_middle_click_titlebar();

  return cobiwm_frame_titlebar_event (frame, event, action);
}

static gboolean
cobiwm_frame_right_click_event (CobiwmUIFrame *frame,
                              ClutterButtonEvent *event)
{
  int action = cobiwm_prefs_get_action_right_click_titlebar();

  return cobiwm_frame_titlebar_event (frame, event, action);
}

static gboolean
cobiwm_frames_try_grab_op (CobiwmUIFrame *frame,
                         CobiwmGrabOp   op,
                         gdouble      grab_x,
                         gdouble      grab_y,
                         guint32      time)
{
  CobiwmFrames *frames = frame->frames;
  Display *display;
  gboolean ret;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  ret = cobiwm_core_begin_grab_op (display,
                                 frame->xwindow,
                                 op,
                                 FALSE,
                                 TRUE,
                                 frame->grab_button,
                                 0,
                                 time,
                                 grab_x, grab_y);
  if (!ret)
    {
      frames->current_grab_op = op;
      frames->grab_frame = frame;
      frames->grab_x = grab_x;
      frames->grab_y = grab_y;
    }

  return ret;
}

static gboolean
cobiwm_frames_retry_grab_op (CobiwmFrames *frames,
                           guint       time)
{
  Display *display;
  CobiwmGrabOp op;

  if (frames->current_grab_op == COBIWM_GRAB_OP_NONE)
    return TRUE;

  op = frames->current_grab_op;
  frames->current_grab_op = COBIWM_GRAB_OP_NONE;
  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  return cobiwm_core_begin_grab_op (display,
                                  frames->grab_frame->xwindow,
                                  op,
                                  FALSE,
                                  TRUE,
                                  frames->grab_frame->grab_button,
                                  0,
                                  time,
                                  frames->grab_x,
                                  frames->grab_y);
}

static CobiwmGrabOp
grab_op_from_resize_control (CobiwmFrameControl control)
{
  switch (control)
    {
    case COBIWM_FRAME_CONTROL_RESIZE_SE:
      return COBIWM_GRAB_OP_RESIZING_SE;
    case COBIWM_FRAME_CONTROL_RESIZE_S:
      return COBIWM_GRAB_OP_RESIZING_S;
    case COBIWM_FRAME_CONTROL_RESIZE_SW:
      return COBIWM_GRAB_OP_RESIZING_SW;
    case COBIWM_FRAME_CONTROL_RESIZE_NE:
      return COBIWM_GRAB_OP_RESIZING_NE;
    case COBIWM_FRAME_CONTROL_RESIZE_N:
      return COBIWM_GRAB_OP_RESIZING_N;
    case COBIWM_FRAME_CONTROL_RESIZE_NW:
      return COBIWM_GRAB_OP_RESIZING_NW;
    case COBIWM_FRAME_CONTROL_RESIZE_E:
      return COBIWM_GRAB_OP_RESIZING_E;
    case COBIWM_FRAME_CONTROL_RESIZE_W:
      return COBIWM_GRAB_OP_RESIZING_W;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
cobiwm_frame_left_click_event (CobiwmUIFrame *frame,
                             ClutterButtonEvent *event)
{
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  CobiwmFrameControl control = get_control (frame, event->x, event->y);

  switch (control)
    {
    case COBIWM_FRAME_CONTROL_MAXIMIZE:
    case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
    case COBIWM_FRAME_CONTROL_MINIMIZE:
    case COBIWM_FRAME_CONTROL_DELETE:
    case COBIWM_FRAME_CONTROL_MENU:
    case COBIWM_FRAME_CONTROL_APPMENU:
      frame->grab_button = event->button;
      frame->button_state = COBIWM_BUTTON_STATE_PRESSED;
      frame->prelit_control = control;
      redraw_control (frame, control);

      if (control == COBIWM_FRAME_CONTROL_MENU ||
          control == COBIWM_FRAME_CONTROL_APPMENU)
        {
          CobiwmFrameGeometry fgeom;
          GdkRectangle *rect;
          CobiwmRectangle root_rect;
          CobiwmWindowMenuType menu;
          int win_x, win_y;

          cobiwm_ui_frame_calc_geometry (frame, &fgeom);

          rect = control_rect (control, &fgeom);

          gdk_window_get_position (frame->window, &win_x, &win_y);

          root_rect.x = win_x + rect->x;
          root_rect.y = win_y + rect->y;
          root_rect.width = rect->width;
          root_rect.height = rect->height;

          menu = control == COBIWM_FRAME_CONTROL_MENU ? COBIWM_WINDOW_MENU_WM
            : COBIWM_WINDOW_MENU_APP;

          /* if the compositor takes a grab for showing the menu, we will
           * get a LeaveNotify event we want to ignore, to keep the pressed
           * button state while the menu is open
           */
          frame->maybe_ignore_leave_notify = TRUE;
          cobiwm_core_show_window_menu_for_rect (display,
                                               frame->xwindow,
                                               menu,
                                               &root_rect,
                                               event->time);
        }
      else
        {
          cobiwm_frames_try_grab_op (frame, COBIWM_GRAB_OP_FRAME_BUTTON,
                                   event->x, event->y,
                                   event->time);
        }

      return TRUE;
    case COBIWM_FRAME_CONTROL_RESIZE_SE:
    case COBIWM_FRAME_CONTROL_RESIZE_S:
    case COBIWM_FRAME_CONTROL_RESIZE_SW:
    case COBIWM_FRAME_CONTROL_RESIZE_NE:
    case COBIWM_FRAME_CONTROL_RESIZE_N:
    case COBIWM_FRAME_CONTROL_RESIZE_NW:
    case COBIWM_FRAME_CONTROL_RESIZE_E:
    case COBIWM_FRAME_CONTROL_RESIZE_W:
      cobiwm_frames_try_grab_op (frame,
                               grab_op_from_resize_control (control),
                               event->x, event->y,
                               event->time);

      return TRUE;
    case COBIWM_FRAME_CONTROL_TITLE:
      {
        CobiwmFrameFlags flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);

        if (flags & COBIWM_FRAME_ALLOWS_MOVE)
          {
            cobiwm_frames_try_grab_op (frame,
                                     COBIWM_GRAB_OP_MOVING,
                                     event->x, event->y,
                                     event->time);
          }
      }

      return TRUE;
    case COBIWM_FRAME_CONTROL_NONE:
      /* We can get this for example when trying to resize window
       * that cannot be resized (e. g. it is maximized and the theme
       * currently used has borders for maximized windows), see #751884 */
      return FALSE;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
handle_button_press_event (CobiwmUIFrame *frame,
                           ClutterButtonEvent *event)
{
  CobiwmFrameControl control;
  Display *display;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  control = get_control (frame, event->x, event->y);

  /* don't do the rest of this if on client area */
  if (control == COBIWM_FRAME_CONTROL_CLIENT_AREA)
    return FALSE; /* not on the frame, just passed through from client */

  if (event->button == 1 &&
      !(control == COBIWM_FRAME_CONTROL_MINIMIZE ||
        control == COBIWM_FRAME_CONTROL_DELETE ||
        control == COBIWM_FRAME_CONTROL_MAXIMIZE))
    {
      cobiwm_topic (COBIWM_DEBUG_FOCUS,
                  "Focusing window with frame 0x%lx due to button 1 press\n",
                  frame->xwindow);
      cobiwm_window_focus (frame->cobiwm_window, event->time);
    }

  /* We want to shade even if we have a GrabOp, since we'll have a move grab
   * if we double click the titlebar.
   */
  if (control == COBIWM_FRAME_CONTROL_TITLE &&
      event->button == 1 &&
      event->click_count == 2)
    {
      cobiwm_core_end_grab_op (display, event->time);
      return cobiwm_frame_double_click_event (frame, event);
    }

  if (cobiwm_core_get_grab_op (display) != COBIWM_GRAB_OP_NONE)
    return FALSE; /* already up to something */

  frame->grab_button = event->button;

  switch (event->button)
    {
    case 1:
      return cobiwm_frame_left_click_event (frame, event);
    case 2:
      return cobiwm_frame_middle_click_event (frame, event);
    case 3:
      return cobiwm_frame_right_click_event (frame, event);
    default:
      return FALSE;
    }
}

static gboolean
handle_button_release_event (CobiwmUIFrame *frame,
                             ClutterButtonEvent *event)
{
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  frame->frames->current_grab_op = COBIWM_GRAB_OP_NONE;
  cobiwm_core_end_grab_op (display, event->time);

  /* We only handle the releases we handled the presses for (things
   * involving frame controls). Window ops that don't require a
   * frame are handled in the Xlib part of the code, display.c/window.c
   */
  if (((int) event->button) == frame->grab_button &&
      frame->button_state == COBIWM_BUTTON_STATE_PRESSED)
    {
      switch (frame->prelit_control)
        {
        case COBIWM_FRAME_CONTROL_MINIMIZE:
          cobiwm_window_minimize (frame->cobiwm_window);
          break;
        case COBIWM_FRAME_CONTROL_MAXIMIZE:
          /* Focus the window on the maximize */
          cobiwm_window_focus (frame->cobiwm_window, event->time);
          if (cobiwm_prefs_get_raise_on_click ())
            cobiwm_window_raise (frame->cobiwm_window);
          cobiwm_window_maximize (frame->cobiwm_window, COBIWM_MAXIMIZE_BOTH);
          break;
        case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
          if (cobiwm_prefs_get_raise_on_click ())
            cobiwm_window_raise (frame->cobiwm_window);
          cobiwm_window_unmaximize (frame->cobiwm_window, COBIWM_MAXIMIZE_BOTH);
          break;
        case COBIWM_FRAME_CONTROL_DELETE:
          cobiwm_window_delete (frame->cobiwm_window, event->time);
          break;
        default:
          break;
        }

      /* Update the prelit control regardless of what button the mouse
       * was released over; needed so that the new button can become
       * prelit so to let the user know that it can now be pressed.
       * :)
       */
      CobiwmFrameControl control = get_control (frame, event->x, event->y);
      cobiwm_ui_frame_update_prelit_control (frame, control);
    }

  return TRUE;
}

static void
cobiwm_ui_frame_update_prelit_control (CobiwmUIFrame     *frame,
                                     CobiwmFrameControl control)
{
  CobiwmFrameControl old_control;
  CobiwmCursor cursor;

  cobiwm_verbose ("Updating prelit control from %u to %u\n",
                frame->prelit_control, control);

  cursor = COBIWM_CURSOR_DEFAULT;

  switch (control)
    {
    case COBIWM_FRAME_CONTROL_CLIENT_AREA:
      break;
    case COBIWM_FRAME_CONTROL_NONE:
      break;
    case COBIWM_FRAME_CONTROL_TITLE:
      break;
    case COBIWM_FRAME_CONTROL_DELETE:
      break;
    case COBIWM_FRAME_CONTROL_MENU:
      break;
    case COBIWM_FRAME_CONTROL_APPMENU:
      break;
    case COBIWM_FRAME_CONTROL_MINIMIZE:
      break;
    case COBIWM_FRAME_CONTROL_MAXIMIZE:
      break;
    case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_SE:
      cursor = COBIWM_CURSOR_SE_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_S:
      cursor = COBIWM_CURSOR_SOUTH_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_SW:
      cursor = COBIWM_CURSOR_SW_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_N:
      cursor = COBIWM_CURSOR_NORTH_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_NE:
      cursor = COBIWM_CURSOR_NE_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_NW:
      cursor = COBIWM_CURSOR_NW_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_W:
      cursor = COBIWM_CURSOR_WEST_RESIZE;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_E:
      cursor = COBIWM_CURSOR_EAST_RESIZE;
      break;
    }

  /* set/unset the prelight cursor */
  cobiwm_core_set_screen_cursor (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                               frame->xwindow,
                               cursor);

  switch (control)
    {
    case COBIWM_FRAME_CONTROL_MENU:
    case COBIWM_FRAME_CONTROL_APPMENU:
    case COBIWM_FRAME_CONTROL_MINIMIZE:
    case COBIWM_FRAME_CONTROL_MAXIMIZE:
    case COBIWM_FRAME_CONTROL_DELETE:
    case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
      /* leave control set */
      break;
    default:
      /* Only prelight buttons */
      control = COBIWM_FRAME_CONTROL_NONE;
      break;
    }

  if (control == frame->prelit_control &&
      frame->button_state == COBIWM_BUTTON_STATE_PRELIGHT)
    return;

  /* Save the old control so we can unprelight it */
  old_control = frame->prelit_control;

  frame->button_state = COBIWM_BUTTON_STATE_PRELIGHT;
  frame->prelit_control = control;

  redraw_control (frame, old_control);
  redraw_control (frame, control);
}

static gboolean
handle_motion_notify_event (CobiwmUIFrame *frame,
                            ClutterMotionEvent *event)
{
  CobiwmFrames *frames = frame->frames;
  CobiwmFrameControl control;

  control = get_control (frame, event->x, event->y);

  if (frame->button_state == COBIWM_BUTTON_STATE_PRESSED)
    {
      /* If the user leaves the frame button, set the state
       * back to normal and redraw. */
      if (frame->prelit_control != control)
        {
          frame->button_state = COBIWM_BUTTON_STATE_NORMAL;
          redraw_control (frame, frame->prelit_control);
        }
    }
  else
    {
      /* Update prelit control and cursor */
      cobiwm_ui_frame_update_prelit_control (frame, control);
    }

  if ((event->modifier_state & CLUTTER_BUTTON1_MASK) &&
      frames->current_grab_op != COBIWM_GRAB_OP_NONE)
    cobiwm_frames_retry_grab_op (frames, event->time);

  return TRUE;
}

static cairo_region_t *
get_visible_frame_border_region (CobiwmUIFrame *frame)
{
  cairo_rectangle_int_t area;
  cairo_region_t *frame_border;
  CobiwmFrameFlags flags;
  CobiwmFrameType type;
  CobiwmFrameBorders borders;
  CobiwmRectangle buffer_rect = frame->cobiwm_window->buffer_rect;

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);
  type = cobiwm_window_get_frame_type (frame->cobiwm_window);

  cobiwm_theme_get_frame_borders (cobiwm_theme_get_default (), frame->style_info,
                                type, frame->text_height, flags,
                                &borders);

  /* Frame rect */
  area.x = 0;
  area.y = 0;
  area.width = buffer_rect.width;
  area.height = buffer_rect.height;

  frame_border = cairo_region_create_rectangle (&area);

  /* Client rect */
  area.x += borders.total.left;
  area.y += borders.total.top;
  area.width -= borders.total.left + borders.total.right;
  area.height -= borders.total.top + borders.total.bottom;

  /* Visible frame border */
  cairo_region_subtract_rectangle (frame_border, &area);
  return frame_border;
}

/*
 * Draw the opaque and semi-opaque pixels of this frame into a mask.
 *
 * (0,0) in Cairo coordinates is assumed to be the top left corner of the
 * invisible border.
 *
 * The parts of @cr's surface in the clip region are assumed to be
 * initialized to fully-transparent, and the clip region is assumed to
 * contain the invisible border and the visible parts of the frame, but
 * not the client area.
 *
 * This function uses @cr to draw pixels of arbitrary color (it will
 * typically be drawing in a %CAIRO_FORMAT_A8 surface, so the color is
 * discarded anyway) with appropriate alpha values to reproduce this
 * frame's alpha channel, as a mask to be applied to an opaque pixmap.
 *
 * @frame: This frame
 * @xwindow: The X window for the frame, which has the client window as a child
 * @cr: Used to draw the resulting mask
 */
void
cobiwm_ui_frame_get_mask (CobiwmUIFrame *frame,
                        cairo_t     *cr)
{
  CobiwmFrameBorders borders;
  CobiwmFrameFlags flags;
  CobiwmRectangle frame_rect;
  int scale = cobiwm_theme_get_window_scaling_factor ();

  cobiwm_window_get_frame_rect (frame->cobiwm_window, &frame_rect);

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);

  cobiwm_style_info_set_flags (frame->style_info, flags);
  cobiwm_ui_frame_get_borders (frame, &borders);

  /* See comment in cobiwm_frame_layout_draw_with_style() for details on HiDPI handling */
  cairo_scale (cr, scale, scale);
  gtk_render_background (frame->style_info->styles[COBIWM_STYLE_ELEMENT_FRAME], cr,
                         borders.invisible.left / scale,
                         borders.invisible.top / scale,
                         frame_rect.width / scale, frame_rect.height / scale);
  gtk_render_background (frame->style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR], cr,
                         borders.invisible.left / scale,
                         borders.invisible.top / scale,
                         frame_rect.width / scale, borders.total.top / scale);
}

/* XXX -- this is disgusting. Find a better approach here.
 * Use multiple widgets? */
static CobiwmUIFrame *
find_frame_to_draw (CobiwmFrames *frames,
                    cairo_t    *cr)
{
  GHashTableIter iter;
  CobiwmUIFrame *frame;

  g_hash_table_iter_init (&iter, frames->frames);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &frame))
    if (gtk_cairo_should_draw_window (cr, frame->window))
      return frame;

  return NULL;
}

static gboolean
cobiwm_frames_draw (GtkWidget *widget,
                  cairo_t   *cr)
{
  CobiwmUIFrame *frame;
  CobiwmFrames *frames;
  cairo_region_t *region;

  frames = COBIWM_FRAMES (widget);

  frame = find_frame_to_draw (frames, cr);
  if (frame == NULL)
    return FALSE;

  region = get_visible_frame_border_region (frame);
  gdk_cairo_region (cr, region);
  cairo_clip (cr);

  /* The target may be cleared to black or transparent, depending
   * on the frame's visual; we don't want decorations to appear
   * differently when the theme's decorations aren't fully opaque,
   * so clear to black first
   */
  cairo_paint (cr);

  cobiwm_ui_frame_paint (frame, cr);
  cairo_region_destroy (region);

  return TRUE;
}

static void
cobiwm_ui_frame_paint (CobiwmUIFrame  *frame,
                     cairo_t      *cr)
{
  CobiwmFrameFlags flags;
  CobiwmFrameType type;
  cairo_surface_t *mini_icon;
  CobiwmButtonState button_states[COBIWM_BUTTON_TYPE_LAST];
  int i;
  int button_type = -1;
  CobiwmButtonLayout button_layout;
  CobiwmWindowX11 *window_x11 = COBIWM_WINDOW_X11 (frame->cobiwm_window);
  CobiwmWindowX11Private *priv = window_x11->priv;

  for (i = 0; i < COBIWM_BUTTON_TYPE_LAST; i++)
    button_states[i] = COBIWM_BUTTON_STATE_NORMAL;

  /* Set prelight state */
  switch (frame->prelit_control)
    {
    case COBIWM_FRAME_CONTROL_MENU:
      button_type = COBIWM_BUTTON_TYPE_MENU;
      break;
    case COBIWM_FRAME_CONTROL_APPMENU:
      button_type = COBIWM_BUTTON_TYPE_APPMENU;
      break;
    case COBIWM_FRAME_CONTROL_MINIMIZE:
      button_type = COBIWM_BUTTON_TYPE_MINIMIZE;
      break;
    case COBIWM_FRAME_CONTROL_MAXIMIZE:
      button_type = COBIWM_BUTTON_TYPE_MAXIMIZE;
      break;
    case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
      button_type = COBIWM_BUTTON_TYPE_MAXIMIZE;
      break;
    case COBIWM_FRAME_CONTROL_DELETE:
      button_type = COBIWM_BUTTON_TYPE_CLOSE;
      break;
    default:
      break;
    }

  if (button_type > -1)
    button_states[button_type] = frame->button_state;

  mini_icon = frame->cobiwm_window->mini_icon;
  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);
  type = cobiwm_window_get_frame_type (frame->cobiwm_window);

  cobiwm_ui_frame_ensure_layout (frame, type);

  cobiwm_prefs_get_button_layout (&button_layout);

  cobiwm_theme_draw_frame (cobiwm_theme_get_default (),
                         frame->style_info,
                         cr,
                         type,
                         flags,
                         priv->client_rect.width,
                         priv->client_rect.height,
                         frame->text_layout,
                         frame->text_height,
                         &button_layout,
                         button_states,
                         mini_icon);
}

static gboolean
handle_enter_notify_event (CobiwmUIFrame *frame,
                           ClutterCrossingEvent *event)
{
  CobiwmFrameControl control;

  frame->maybe_ignore_leave_notify = FALSE;

  control = get_control (frame, event->x, event->y);
  cobiwm_ui_frame_update_prelit_control (frame, control);

  return TRUE;
}

static gboolean
handle_leave_notify_event (CobiwmUIFrame *frame,
                           ClutterCrossingEvent *event)
{
  Display *display;
  CobiwmGrabOp grab_op;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  grab_op = cobiwm_core_get_grab_op (display);

  /* ignore the first LeaveNotify event after opening a window menu
   * if it is the result of a compositor grab
   */
  frame->maybe_ignore_leave_notify = frame->maybe_ignore_leave_notify &&
                                     grab_op == COBIWM_GRAB_OP_COMPOSITOR;

  if (frame->maybe_ignore_leave_notify)
    return FALSE;

  cobiwm_ui_frame_update_prelit_control (frame, COBIWM_FRAME_CONTROL_NONE);

  return TRUE;
}

gboolean
cobiwm_ui_frame_handle_event (CobiwmUIFrame *frame,
                            const ClutterEvent *event)
{
  switch (event->any.type)
    {
    case CLUTTER_BUTTON_PRESS:
      return handle_button_press_event (frame, (ClutterButtonEvent *) event);
    case CLUTTER_BUTTON_RELEASE:
      return handle_button_release_event (frame, (ClutterButtonEvent *) event);
    case CLUTTER_MOTION:
      return handle_motion_notify_event (frame, (ClutterMotionEvent *) event);
    case CLUTTER_ENTER:
      return handle_enter_notify_event (frame, (ClutterCrossingEvent *) event);
    case CLUTTER_LEAVE:
      return handle_leave_notify_event (frame, (ClutterCrossingEvent *) event);
    default:
      return FALSE;
    }
}

static GdkRectangle*
control_rect (CobiwmFrameControl control,
              CobiwmFrameGeometry *fgeom)
{
  GdkRectangle *rect;

  rect = NULL;
  switch (control)
    {
    case COBIWM_FRAME_CONTROL_TITLE:
      rect = &fgeom->title_rect;
      break;
    case COBIWM_FRAME_CONTROL_DELETE:
      rect = &fgeom->close_rect.visible;
      break;
    case COBIWM_FRAME_CONTROL_MENU:
      rect = &fgeom->menu_rect.visible;
      break;
    case COBIWM_FRAME_CONTROL_APPMENU:
      rect = &fgeom->appmenu_rect.visible;
      break;
    case COBIWM_FRAME_CONTROL_MINIMIZE:
      rect = &fgeom->min_rect.visible;
      break;
    case COBIWM_FRAME_CONTROL_MAXIMIZE:
    case COBIWM_FRAME_CONTROL_UNMAXIMIZE:
      rect = &fgeom->max_rect.visible;
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_SE:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_S:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_SW:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_N:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_NE:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_NW:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_W:
      break;
    case COBIWM_FRAME_CONTROL_RESIZE_E:
      break;
    case COBIWM_FRAME_CONTROL_NONE:
      break;
    case COBIWM_FRAME_CONTROL_CLIENT_AREA:
      break;
    }

  return rect;
}

#define TOP_RESIZE_HEIGHT 4
#define CORNER_SIZE_MULT 2
static CobiwmFrameControl
get_control (CobiwmUIFrame *frame, int root_x, int root_y)
{
  CobiwmFrameGeometry fgeom;
  CobiwmFrameFlags flags;
  CobiwmFrameType type;
  gboolean has_vert, has_horiz;
  gboolean has_north_resize;
  cairo_rectangle_int_t client;
  int x, y;
  int win_x, win_y;

  gdk_window_get_position (frame->window, &win_x, &win_y);
  x = root_x - win_x;
  y = root_y - win_y;

  cobiwm_ui_frame_calc_geometry (frame, &fgeom);
  get_client_rect (&fgeom, &client);

  if (POINT_IN_RECT (x, y, client))
    return COBIWM_FRAME_CONTROL_CLIENT_AREA;

  if (POINT_IN_RECT (x, y, fgeom.close_rect.clickable))
    return COBIWM_FRAME_CONTROL_DELETE;

  if (POINT_IN_RECT (x, y, fgeom.min_rect.clickable))
    return COBIWM_FRAME_CONTROL_MINIMIZE;

  if (POINT_IN_RECT (x, y, fgeom.menu_rect.clickable))
    return COBIWM_FRAME_CONTROL_MENU;

  if (POINT_IN_RECT (x, y, fgeom.appmenu_rect.clickable))
    return COBIWM_FRAME_CONTROL_APPMENU;

  flags = cobiwm_frame_get_flags (frame->cobiwm_window->frame);
  type = cobiwm_window_get_frame_type (frame->cobiwm_window);

  has_north_resize = (type != COBIWM_FRAME_TYPE_ATTACHED);
  has_vert = (flags & COBIWM_FRAME_ALLOWS_VERTICAL_RESIZE) != 0;
  has_horiz = (flags & COBIWM_FRAME_ALLOWS_HORIZONTAL_RESIZE) != 0;

  if (POINT_IN_RECT (x, y, fgeom.title_rect))
    {
      if (has_vert && y <= TOP_RESIZE_HEIGHT && has_north_resize)
        return COBIWM_FRAME_CONTROL_RESIZE_N;
      else
        return COBIWM_FRAME_CONTROL_TITLE;
    }

  if (POINT_IN_RECT (x, y, fgeom.max_rect.clickable))
    {
      if (flags & COBIWM_FRAME_MAXIMIZED)
        return COBIWM_FRAME_CONTROL_UNMAXIMIZE;
      else
        return COBIWM_FRAME_CONTROL_MAXIMIZE;
    }

  /* South resize always has priority over north resize,
   * in case of overlap.
   */

  if (y >= (fgeom.height - fgeom.borders.total.bottom * CORNER_SIZE_MULT) &&
      x >= (fgeom.width - fgeom.borders.total.right * CORNER_SIZE_MULT))
    {
      if (has_vert && has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_SE;
      else if (has_vert)
        return COBIWM_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_E;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom * CORNER_SIZE_MULT) &&
           x <= fgeom.borders.total.left * CORNER_SIZE_MULT)
    {
      if (has_vert && has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_SW;
      else if (has_vert)
        return COBIWM_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top * CORNER_SIZE_MULT) &&
           x <= (fgeom.borders.total.left * CORNER_SIZE_MULT) && has_north_resize)
    {
      if (has_vert && has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_NW;
      else if (has_vert)
        return COBIWM_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top * CORNER_SIZE_MULT) &&
           x >= (fgeom.width - fgeom.borders.total.right * CORNER_SIZE_MULT) && has_north_resize)
    {
      if (has_vert && has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_NE;
      else if (has_vert)
        return COBIWM_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_E;
    }
  else if (y < (fgeom.borders.invisible.top + TOP_RESIZE_HEIGHT))
    {
      if (has_vert && has_north_resize)
        return COBIWM_FRAME_CONTROL_RESIZE_N;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom))
    {
      if (has_vert)
        return COBIWM_FRAME_CONTROL_RESIZE_S;
    }
  else if (x <= fgeom.borders.total.left)
    {
      if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_W;
    }
  else if (x >= (fgeom.width - fgeom.borders.total.right))
    {
      if (has_horiz)
        return COBIWM_FRAME_CONTROL_RESIZE_E;
    }

  if (y >= fgeom.borders.total.top)
    return COBIWM_FRAME_CONTROL_NONE;
  else
    return COBIWM_FRAME_CONTROL_TITLE;
}
