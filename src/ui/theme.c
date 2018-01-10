/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
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
#include "theme-private.h"
#include "frames.h" /* for COBIWM_TYPE_FRAMES */
#include "util-private.h"
#include <prefs.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#define DEBUG_FILL_STRUCT(s) memset ((s), 0xef, sizeof (*(s)))

static void scale_border (GtkBorder *border, double factor);

static CobiwmFrameLayout *
cobiwm_frame_layout_new  (void)
{
  CobiwmFrameLayout *layout;

  layout = g_new0 (CobiwmFrameLayout, 1);

  /* Spacing as hardcoded in GTK+:
   * https://git.gnome.org/browse/gtk+/tree/gtk/gtkheaderbar.c?h=gtk-3-14#n53
   */
  layout->titlebar_spacing = 6;
  layout->has_title = TRUE;
  layout->title_scale = PANGO_SCALE_MEDIUM;
  layout->icon_size = COBIWM_MINI_ICON_WIDTH;

  return layout;
}

static void
cobiwm_frame_layout_free (CobiwmFrameLayout *layout)
{
  g_return_if_fail (layout != NULL);

  DEBUG_FILL_STRUCT (layout);
  g_free (layout);
}

static void
cobiwm_frame_layout_get_borders (const CobiwmFrameLayout *layout,
                               int                    text_height,
                               CobiwmFrameFlags         flags,
                               CobiwmFrameType          type,
                               CobiwmFrameBorders      *borders)
{
  int buttons_height, content_height, draggable_borders;
  int scale = cobiwm_theme_get_window_scaling_factor ();

  cobiwm_frame_borders_clear (borders);

  /* For a full-screen window, we don't have any borders, visible or not. */
  if (flags & COBIWM_FRAME_FULLSCREEN)
    return;

  g_return_if_fail (layout != NULL);

  if (!layout->has_title)
    text_height = 0;
  else
    text_height = layout->title_margin.top + text_height + layout->title_margin.bottom;

  buttons_height = MAX ((int)layout->icon_size, layout->button_min_size.height) +
    layout->button_margin.top + layout->button_border.top +
    layout->button_margin.bottom + layout->button_border.bottom;
  content_height = MAX (buttons_height, text_height);
  content_height = MAX (content_height, layout->titlebar_min_size.height) +
                   layout->titlebar_border.top + layout->titlebar_border.bottom;

  borders->visible.top    = layout->frame_border.top + content_height;
  borders->visible.left   = layout->frame_border.left;
  borders->visible.right  = layout->frame_border.right;
  borders->visible.bottom = layout->frame_border.bottom;

  borders->invisible = layout->invisible_border;

  draggable_borders = cobiwm_prefs_get_draggable_border_width ();

  if (flags & COBIWM_FRAME_ALLOWS_HORIZONTAL_RESIZE)
    {
      borders->invisible.left   = MAX (borders->invisible.left,
                                       draggable_borders - borders->visible.left);
      borders->invisible.right  = MAX (borders->invisible.right,
                                       draggable_borders - borders->visible.right);
    }

  if (flags & COBIWM_FRAME_ALLOWS_VERTICAL_RESIZE)
    {
      borders->invisible.bottom = MAX (borders->invisible.bottom,
                                       draggable_borders - borders->visible.bottom);

      /* borders.visible.top is the height of the *title bar*. We can't do the same
       * algorithm here, titlebars are expectedly much bigger. Just subtract a couple
       * pixels to get a proper feel. */
      if (type != COBIWM_FRAME_TYPE_ATTACHED)
        borders->invisible.top    = MAX (borders->invisible.top, draggable_borders - 2);
    }

  borders->total.left   = borders->invisible.left   + borders->visible.left;
  borders->total.right  = borders->invisible.right  + borders->visible.right;
  borders->total.bottom = borders->invisible.bottom + borders->visible.bottom;
  borders->total.top    = borders->invisible.top    + borders->visible.top;

  /* Scale geometry for HiDPI, see comment in cobiwm_frame_layout_draw_with_style() */
  scale_border (&borders->visible, scale);
  scale_border (&borders->invisible, scale);
  scale_border (&borders->total, scale);
}

int
cobiwm_theme_get_window_scaling_factor (void)
{
  GdkScreen *screen;
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);

  screen = gdk_screen_get_default ();
  if (gdk_screen_get_setting (screen, "gdk-window-scaling-factor", &value))
    return g_value_get_int (&value);
  else
    return 1;
}

void
cobiwm_frame_layout_apply_scale (const CobiwmFrameLayout *layout,
                               PangoFontDescription  *font_desc)
{
  int size = pango_font_description_get_size (font_desc);
  double scale = layout->title_scale / cobiwm_theme_get_window_scaling_factor ();
  pango_font_description_set_size (font_desc, MAX (size * scale, 1));
}

static CobiwmButtonSpace*
rect_for_function (CobiwmFrameGeometry *fgeom,
                   CobiwmFrameFlags     flags,
                   CobiwmButtonFunction function,
                   CobiwmTheme         *theme)
{
  switch (function)
    {
    case COBIWM_BUTTON_FUNCTION_MENU:
      if (flags & COBIWM_FRAME_ALLOWS_MENU)
        return &fgeom->menu_rect;
      else
        return NULL;
    case COBIWM_BUTTON_FUNCTION_APPMENU:
      if (flags & COBIWM_FRAME_ALLOWS_APPMENU)
        return &fgeom->appmenu_rect;
      else
        return NULL;
    case COBIWM_BUTTON_FUNCTION_MINIMIZE:
      if (flags & COBIWM_FRAME_ALLOWS_MINIMIZE)
        return &fgeom->min_rect;
      else
        return NULL;
    case COBIWM_BUTTON_FUNCTION_MAXIMIZE:
      if (flags & COBIWM_FRAME_ALLOWS_MAXIMIZE)
        return &fgeom->max_rect;
      else
        return NULL;
    case COBIWM_BUTTON_FUNCTION_CLOSE:
      if (flags & COBIWM_FRAME_ALLOWS_DELETE)
        return &fgeom->close_rect;
      else
        return NULL;

    case COBIWM_BUTTON_FUNCTION_LAST:
      return NULL;
    }

  return NULL;
}

static gboolean
strip_button (CobiwmButtonSpace *func_rects[MAX_BUTTONS_PER_CORNER],
              int             *n_rects,
              CobiwmButtonSpace *to_strip)
{
  int i;

  i = 0;
  while (i < *n_rects)
    {
      if (func_rects[i] == to_strip)
        {
          *n_rects -= 1;

          /* shift the other rects back in the array */
          while (i < *n_rects)
            {
              func_rects[i] = func_rects[i+1];

              ++i;
            }

          func_rects[i] = NULL;

          return TRUE;
        }

      ++i;
    }

  return FALSE; /* did not strip anything */
}

static void
get_padding_and_border (GtkStyleContext *style,
                        GtkBorder       *border)
{
  GtkBorder tmp;
  GtkStateFlags state = gtk_style_context_get_state (style);

  gtk_style_context_get_border (style, state, border);
  gtk_style_context_get_padding (style, state, &tmp);

  border->left += tmp.left;
  border->top += tmp.top;
  border->right += tmp.right;
  border->bottom += tmp.bottom;
}

static void
get_min_size (GtkStyleContext *style,
              GtkRequisition  *requisition)
{
  gtk_style_context_get (style, gtk_style_context_get_state (style),
                         "min-width", &requisition->width,
                         "min-height", &requisition->height,
                         NULL);
}

static void
scale_border (GtkBorder *border,
              double     factor)
{
  border->left *= factor;
  border->right *= factor;
  border->top *= factor;
  border->bottom *= factor;
}

static void
cobiwm_frame_layout_sync_with_style (CobiwmFrameLayout *layout,
                                   CobiwmStyleInfo   *style_info,
                                   CobiwmFrameFlags   flags)
{
  GtkStyleContext *style;
  GtkBorder border;
  GtkRequisition requisition;
  GdkRectangle clip_rect;
  int border_radius, max_radius;

  cobiwm_style_info_set_flags (style_info, flags);

  style = style_info->styles[COBIWM_STYLE_ELEMENT_FRAME];
  get_padding_and_border (style, &layout->frame_border);
  scale_border (&layout->frame_border, layout->title_scale);

  gtk_render_background_get_clip (style, 0, 0, 0, 0, &clip_rect);
  layout->invisible_border.left = -clip_rect.x;
  layout->invisible_border.right = clip_rect.width + clip_rect.x;
  layout->invisible_border.top = -clip_rect.y;
  layout->invisible_border.bottom = clip_rect.height + clip_rect.y;

  if (layout->hide_buttons)
    layout->icon_size = 0;

  if (!layout->has_title && layout->hide_buttons)
    return; /* border-only - be done */

  style = style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR];
  gtk_style_context_get (style, gtk_style_context_get_state (style),
                         "border-radius", &border_radius,
                         NULL);
  /* GTK+ currently does not allow us to look up radii of individual
   * corners; however we don't clip the client area, so with the
   * current trend of using small/no visible frame borders, most
   * themes should work fine with this.
   */
  layout->top_left_corner_rounded_radius = border_radius;
  layout->top_right_corner_rounded_radius = border_radius;
  max_radius = MIN (layout->frame_border.bottom, layout->frame_border.left);
  layout->bottom_left_corner_rounded_radius = MAX (border_radius, max_radius);
  max_radius = MIN (layout->frame_border.bottom, layout->frame_border.right);
  layout->bottom_right_corner_rounded_radius = MAX (border_radius, max_radius);

  get_min_size (style, &layout->titlebar_min_size);
  get_padding_and_border (style, &layout->titlebar_border);
  scale_border (&layout->titlebar_border, layout->title_scale);

  style = style_info->styles[COBIWM_STYLE_ELEMENT_TITLE];
  gtk_style_context_get_margin (style, gtk_style_context_get_state (style),
                                &layout->title_margin);
  scale_border (&layout->title_margin, layout->title_scale);

  style = style_info->styles[COBIWM_STYLE_ELEMENT_BUTTON];
  get_min_size (style, &layout->button_min_size);
  get_padding_and_border (style, &layout->button_border);
  scale_border (&layout->button_border, layout->title_scale);
  gtk_style_context_get_margin (style, gtk_style_context_get_state (style),
                                &layout->button_margin);
  scale_border (&layout->button_margin, layout->title_scale);

  style = style_info->styles[COBIWM_STYLE_ELEMENT_IMAGE];
  get_min_size (style, &requisition);
  get_padding_and_border (style, &border);
  scale_border (&border, layout->title_scale);

  layout->button_border.left += border.left;
  layout->button_border.right += border.right;
  layout->button_border.top += border.top;
  layout->button_border.bottom += border.bottom;

  gtk_style_context_get_margin (style, gtk_style_context_get_state (style),
                                &border);
  layout->button_border.left += border.left;
  layout->button_border.right += border.right;
  layout->button_border.top += border.top;
  layout->button_border.bottom += border.bottom;

  layout->button_min_size.width = MAX(layout->button_min_size.width,
                                      requisition.width);
  layout->button_min_size.height = MAX(layout->button_min_size.height,
                                       requisition.height);
}

static void
cobiwm_frame_layout_calc_geometry (CobiwmFrameLayout        *layout,
                                 CobiwmStyleInfo          *style_info,
                                 int                     text_height,
                                 CobiwmFrameFlags          flags,
                                 int                     client_width,
                                 int                     client_height,
                                 const CobiwmButtonLayout *button_layout,
                                 CobiwmFrameType           type,
                                 CobiwmFrameGeometry      *fgeom,
                                 CobiwmTheme              *theme)
{
  int i, n_left, n_right, n_left_spacers, n_right_spacers;
  int x;
  int button_y;
  int title_right_edge;
  int width, height;
  int content_width, content_height;
  int button_width, button_height;
  int min_size_for_rounding;
  int scale = cobiwm_theme_get_window_scaling_factor ();

  /* the left/right rects in order; the max # of rects
   * is the number of button functions
   */
  CobiwmButtonSpace *left_func_rects[MAX_BUTTONS_PER_CORNER];
  CobiwmButtonSpace *right_func_rects[MAX_BUTTONS_PER_CORNER];
  gboolean left_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];
  gboolean right_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];

  CobiwmFrameBorders borders;

  cobiwm_frame_layout_sync_with_style (layout, style_info, flags);

  cobiwm_frame_layout_get_borders (layout, text_height,
                                 flags, type,
                                 &borders);

  fgeom->borders = borders;

  /* Scale geometry for HiDPI, see comment in cobiwm_frame_layout_draw_with_style() */
  fgeom->content_border = layout->frame_border;
  fgeom->content_border.left   += layout->titlebar_border.left * scale;
  fgeom->content_border.right  += layout->titlebar_border.right * scale;
  fgeom->content_border.top    += layout->titlebar_border.top * scale;
  fgeom->content_border.bottom += layout->titlebar_border.bottom * scale;

  width = client_width + borders.total.left + borders.total.right;

  height = borders.total.top + borders.total.bottom;
  if (!(flags & COBIWM_FRAME_SHADED))
    height += client_height;

  fgeom->width = width;
  fgeom->height = height;

  content_width = width -
                  (fgeom->content_border.left + borders.invisible.left) -
                  (fgeom->content_border.right + borders.invisible.right);
  content_height = borders.visible.top - fgeom->content_border.top - fgeom->content_border.bottom;

  button_width = MAX ((int)layout->icon_size, layout->button_min_size.width) +
                 layout->button_border.left + layout->button_border.right;
  button_height = MAX ((int)layout->icon_size, layout->button_min_size.height) +
                  layout->button_border.top + layout->button_border.bottom;
  button_width *= scale;
  button_height *= scale;

  /* FIXME all this code sort of pretends that duplicate buttons
   * with the same function are allowed, but that breaks the
   * code in frames.c, so isn't really allowed right now.
   * Would need left_close_rect, right_close_rect, etc.
   */

  /* Init all button rects to 0, lame hack */
  memset (ADDRESS_OF_BUTTON_RECTS (fgeom), '\0',
          LENGTH_OF_BUTTON_RECTS);

  n_left = 0;
  n_right = 0;
  n_left_spacers = 0;
  n_right_spacers = 0;

  if (!layout->hide_buttons)
    {
      /* Try to fill in rects */
      for (i = 0; i < MAX_BUTTONS_PER_CORNER && button_layout->left_buttons[i] != COBIWM_BUTTON_FUNCTION_LAST; i++)
        {
          left_func_rects[n_left] = rect_for_function (fgeom, flags,
                                                       button_layout->left_buttons[i],
                                                       theme);
          if (left_func_rects[n_left] != NULL)
            {
              left_buttons_has_spacer[n_left] = button_layout->left_buttons_has_spacer[i];
              if (button_layout->left_buttons_has_spacer[i])
                ++n_left_spacers;

              ++n_left;
            }
        }

      for (i = 0; i < MAX_BUTTONS_PER_CORNER && button_layout->right_buttons[i] != COBIWM_BUTTON_FUNCTION_LAST; i++)
        {
          right_func_rects[n_right] = rect_for_function (fgeom, flags,
                                                         button_layout->right_buttons[i],
                                                         theme);
          if (right_func_rects[n_right] != NULL)
            {
              right_buttons_has_spacer[n_right] = button_layout->right_buttons_has_spacer[i];
              if (button_layout->right_buttons_has_spacer[i])
                ++n_right_spacers;

              ++n_right;
            }
        }
    }

  /* Be sure buttons fit */
  while (n_left > 0 || n_right > 0)
    {
      int space_used_by_buttons;

      space_used_by_buttons = 0;

      space_used_by_buttons += layout->button_margin.left * scale * n_left;
      space_used_by_buttons += button_width * n_left;
      space_used_by_buttons += layout->button_margin.right * scale * n_left;
      space_used_by_buttons += (button_width * 0.75) * n_left_spacers;
      space_used_by_buttons += layout->titlebar_spacing * scale * MAX (n_left - 1, 0);

      space_used_by_buttons += layout->button_margin.left * scale * n_right;
      space_used_by_buttons += button_width * n_right;
      space_used_by_buttons += layout->button_margin.right * scale * n_right;
      space_used_by_buttons += (button_width * 0.75) * n_right_spacers;
      space_used_by_buttons += layout->titlebar_spacing * scale * MAX (n_right - 1, 0);

      if (space_used_by_buttons <= content_width)
        break; /* Everything fits, bail out */

      /* First try to remove separators */
      if (n_left_spacers > 0)
        {
          left_buttons_has_spacer[--n_left_spacers] = FALSE;
          continue;
        }
      else if (n_right_spacers > 0)
        {
          right_buttons_has_spacer[--n_right_spacers] = FALSE;
          continue;
        }

      /* Otherwise we need to shave out a button. Shave
       * min, max, close, then menu (menu is most useful);
       * prefer the default button locations.
       */
      if (strip_button (left_func_rects, &n_left, &fgeom->min_rect))
        continue;
      else if (strip_button (right_func_rects, &n_right, &fgeom->min_rect))
        continue;
      else if (strip_button (left_func_rects, &n_left, &fgeom->max_rect))
        continue;
      else if (strip_button (right_func_rects, &n_right, &fgeom->max_rect))
        continue;
      else if (strip_button (left_func_rects, &n_left, &fgeom->close_rect))
        continue;
      else if (strip_button (right_func_rects, &n_right, &fgeom->close_rect))
        continue;
      else if (strip_button (right_func_rects, &n_right, &fgeom->menu_rect))
        continue;
      else if (strip_button (left_func_rects, &n_left, &fgeom->menu_rect))
        continue;
      else if (strip_button (right_func_rects, &n_right, &fgeom->appmenu_rect))
        continue;
      else if (strip_button (left_func_rects, &n_left, &fgeom->appmenu_rect))
        continue;
      else
        {
          cobiwm_bug ("Could not find a button to strip. n_left = %d n_right = %d\n",
                    n_left, n_right);
        }
    }

  /* Save the button layout */
  fgeom->button_layout = *button_layout;
  fgeom->n_left_buttons = n_left;
  fgeom->n_right_buttons = n_right;

  /* center buttons vertically */
  button_y = fgeom->content_border.top + borders.invisible.top +
             (content_height - button_height) / 2;

  /* right edge of farthest-right button */
  x = width - fgeom->content_border.right - borders.invisible.right;

  i = n_right - 1;
  while (i >= 0)
    {
      CobiwmButtonSpace *rect;

      if (x < 0) /* if we go negative, leave the buttons we don't get to as 0-width */
        break;

      x -= layout->button_margin.right * scale;

      rect = right_func_rects[i];
      rect->visible.x = x - button_width;
      if (right_buttons_has_spacer[i])
        rect->visible.x -= (button_width * 0.75);

      rect->visible.y = button_y;
      rect->visible.width = button_width;
      rect->visible.height = button_height;

      if (flags & COBIWM_FRAME_MAXIMIZED ||
          flags & COBIWM_FRAME_TILED_LEFT ||
          flags & COBIWM_FRAME_TILED_RIGHT)
        {
          rect->clickable.x = rect->visible.x;
          rect->clickable.y = 0;
          rect->clickable.width = rect->visible.width;
          rect->clickable.height = button_height + button_y;

          if (i == n_right - 1)
            rect->clickable.width += fgeom->content_border.right;

        }
      else
        g_memmove (&(rect->clickable), &(rect->visible), sizeof(rect->clickable));

      x = rect->visible.x - layout->button_margin.left * scale;

      if (i > 0)
        x -= layout->titlebar_spacing;

      --i;
    }

  /* save right edge of titlebar for later use */
  title_right_edge = x;

  /* Now x changes to be position from the left and we go through
   * the left-side buttons
   */
  x = fgeom->content_border.left + borders.invisible.left;
  for (i = 0; i < n_left; i++)
    {
      CobiwmButtonSpace *rect;

      x += layout->button_margin.left * scale;

      rect = left_func_rects[i];

      rect->visible.x = x;
      rect->visible.y = button_y;
      rect->visible.width = button_width;
      rect->visible.height = button_height;

      if (flags & COBIWM_FRAME_MAXIMIZED)
        {
          if (i==0)
            {
              rect->clickable.x = 0;
              rect->clickable.width = button_width + x;
            }
          else
            {
              rect->clickable.x = rect->visible.x;
              rect->clickable.width = button_width;
            }

          rect->clickable.y = 0;
          rect->clickable.height = button_height + button_y;
        }
      else
        g_memmove (&(rect->clickable), &(rect->visible), sizeof(rect->clickable));

      x = rect->visible.x + rect->visible.width + layout->button_margin.right * scale;
      if (i < n_left - 1)
        x += layout->titlebar_spacing * scale;
      if (left_buttons_has_spacer[i])
        x += (button_width * 0.75);
    }

  /* Center vertically in the available content area */
  fgeom->title_rect.x = x;
  fgeom->title_rect.y = fgeom->content_border.top + borders.invisible.top +
                        (content_height - text_height) / 2;
  fgeom->title_rect.width = title_right_edge - fgeom->title_rect.x;
  fgeom->title_rect.height = text_height;

  /* Nuke title if it won't fit */
  if (fgeom->title_rect.width < 0 ||
      fgeom->title_rect.height < 0)
    {
      fgeom->title_rect.width = 0;
      fgeom->title_rect.height = 0;
    }

  if (flags & COBIWM_FRAME_SHADED)
    min_size_for_rounding = 0;
  else
    min_size_for_rounding = 5 * scale;

  fgeom->top_left_corner_rounded_radius = 0;
  fgeom->top_right_corner_rounded_radius = 0;
  fgeom->bottom_left_corner_rounded_radius = 0;
  fgeom->bottom_right_corner_rounded_radius = 0;

  if (borders.visible.top + borders.visible.left >= min_size_for_rounding)
    fgeom->top_left_corner_rounded_radius = layout->top_left_corner_rounded_radius * scale;
  if (borders.visible.top + borders.visible.right >= min_size_for_rounding)
    fgeom->top_right_corner_rounded_radius = layout->top_right_corner_rounded_radius * scale;

  if (borders.visible.bottom + borders.visible.left >= min_size_for_rounding)
    fgeom->bottom_left_corner_rounded_radius = layout->bottom_left_corner_rounded_radius * scale;
  if (borders.visible.bottom + borders.visible.right >= min_size_for_rounding)
    fgeom->bottom_right_corner_rounded_radius = layout->bottom_right_corner_rounded_radius * scale;
}

static void
get_button_rect (CobiwmButtonType           type,
                 const CobiwmFrameGeometry *fgeom,
                 GdkRectangle            *rect)
{
  switch (type)
    {
    case COBIWM_BUTTON_TYPE_CLOSE:
      *rect = fgeom->close_rect.visible;
      break;

    case COBIWM_BUTTON_TYPE_MAXIMIZE:
      *rect = fgeom->max_rect.visible;
      break;

    case COBIWM_BUTTON_TYPE_MINIMIZE:
      *rect = fgeom->min_rect.visible;
      break;

    case COBIWM_BUTTON_TYPE_MENU:
      *rect = fgeom->menu_rect.visible;
      break;

    case COBIWM_BUTTON_TYPE_APPMENU:
      *rect = fgeom->appmenu_rect.visible;
      break;

    default:
    case COBIWM_BUTTON_TYPE_LAST:
      g_assert_not_reached ();
      break;
    }
}

static const char *
get_class_from_button_type (CobiwmButtonType type)
{
  switch (type)
    {
    case COBIWM_BUTTON_TYPE_CLOSE:
      return "close";
    case COBIWM_BUTTON_TYPE_MAXIMIZE:
      return "maximize";
    case COBIWM_BUTTON_TYPE_MINIMIZE:
      return "minimize";
    default:
      return NULL;
    }
}

static void
cobiwm_frame_layout_draw_with_style (CobiwmFrameLayout         *layout,
                                   CobiwmStyleInfo           *style_info,
                                   cairo_t                 *cr,
                                   const CobiwmFrameGeometry *fgeom,
                                   PangoLayout             *title_layout,
                                   CobiwmFrameFlags           flags,
                                   CobiwmButtonState          button_states[COBIWM_BUTTON_TYPE_LAST],
                                   cairo_surface_t         *mini_icon)
{
  GtkStyleContext *style;
  GtkStateFlags state;
  CobiwmButtonType button_type;
  GdkRectangle visible_rect;
  GdkRectangle titlebar_rect;
  GdkRectangle button_rect;
  const CobiwmFrameBorders *borders;
  int scale = cobiwm_theme_get_window_scaling_factor ();

  /* We opt out of GTK+/Clutter's HiDPI handling, so we have to do the scaling
   * ourselves; the nitty-gritty is a bit confusing, so here is an overview:
   *  - the values in CobiwmFrameLayout are always as they appear in the theme,
   *    i.e. unscaled
   *  - calculated values (borders, CobiwmFrameGeometry) include the scale - as
   *    the geometry is comprised of scaled decorations and the client size
   *    which we must not scale, we don't have another option
   *  - for drawing, we scale the canvas to have GTK+ render elements (borders,
   *    radii, ...) at the correct scale - as a result, we have to "unscale"
   *    the geometry again to not apply the scaling twice
   */
  cairo_scale (cr, scale, scale);

  borders = &fgeom->borders;

  visible_rect.x = borders->invisible.left / scale;
  visible_rect.y = borders->invisible.top / scale;
  visible_rect.width = (fgeom->width - borders->invisible.left - borders->invisible.right) / scale;
  visible_rect.height = (fgeom->height - borders->invisible.top - borders->invisible.bottom) / scale;

  cobiwm_style_info_set_flags (style_info, flags);

  style = style_info->styles[COBIWM_STYLE_ELEMENT_FRAME];
  gtk_render_background (style, cr,
                         visible_rect.x, visible_rect.y,
                         visible_rect.width, visible_rect.height);
  gtk_render_frame (style, cr,
                    visible_rect.x, visible_rect.y,
                    visible_rect.width, visible_rect.height);

  titlebar_rect.x = visible_rect.x;
  titlebar_rect.y = visible_rect.y;
  titlebar_rect.width = visible_rect.width;
  titlebar_rect.height = borders->visible.top / scale;

  style = style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR];
  gtk_render_background (style, cr,
                         titlebar_rect.x, titlebar_rect.y,
                         titlebar_rect.width, titlebar_rect.height);
  gtk_render_frame (style, cr,
                    titlebar_rect.x, titlebar_rect.y,
                    titlebar_rect.width, titlebar_rect.height);

  if (layout->has_title && title_layout)
    {
      PangoRectangle logical;
      int text_width, x, y;

      pango_layout_set_width (title_layout, -1);
      pango_layout_get_pixel_extents (title_layout, NULL, &logical);

      text_width = MIN(fgeom->title_rect.width / scale, logical.width);

      if (text_width < logical.width)
        pango_layout_set_width (title_layout, PANGO_SCALE * text_width);

      /* Center within the frame if possible */
      x = titlebar_rect.x + (titlebar_rect.width - text_width) / 2;
      y = titlebar_rect.y + (titlebar_rect.height - logical.height) / 2;

      if (x < fgeom->title_rect.x / scale)
        x = fgeom->title_rect.x / scale;
      else if (x + text_width > (fgeom->title_rect.x + fgeom->title_rect.width) / scale)
        x = (fgeom->title_rect.x + fgeom->title_rect.width) / scale - text_width;

      style = style_info->styles[COBIWM_STYLE_ELEMENT_TITLE];
      gtk_render_layout (style, cr, x, y, title_layout);
    }

  style = style_info->styles[COBIWM_STYLE_ELEMENT_BUTTON];
  state = gtk_style_context_get_state (style);
  for (button_type = COBIWM_BUTTON_TYPE_CLOSE; button_type < COBIWM_BUTTON_TYPE_LAST; button_type++)
    {
      const char *button_class = get_class_from_button_type (button_type);
      if (button_class)
        gtk_style_context_add_class (style, button_class);

      get_button_rect (button_type, fgeom, &button_rect);

      button_rect.x /= scale;
      button_rect.y /= scale;
      button_rect.width /= scale;
      button_rect.height /= scale;

      if (button_states[button_type] == COBIWM_BUTTON_STATE_PRELIGHT)
        gtk_style_context_set_state (style, state | GTK_STATE_PRELIGHT);
      else if (button_states[button_type] == COBIWM_BUTTON_STATE_PRESSED)
        gtk_style_context_set_state (style, state | GTK_STATE_ACTIVE);
      else
        gtk_style_context_set_state (style, state);

      cairo_save (cr);

      if (button_rect.width > 0 && button_rect.height > 0)
        {
          cairo_surface_t *surface = NULL;
          const char *icon_name = NULL;

          gtk_render_background (style, cr,
                                 button_rect.x, button_rect.y,
                                 button_rect.width, button_rect.height);
          gtk_render_frame (style, cr,
                            button_rect.x, button_rect.y,
                            button_rect.width, button_rect.height);

          switch (button_type)
            {
            case COBIWM_BUTTON_TYPE_CLOSE:
               icon_name = "window-close-symbolic";
               break;
            case COBIWM_BUTTON_TYPE_MAXIMIZE:
               if (flags & COBIWM_FRAME_MAXIMIZED)
                 icon_name = "window-restore-symbolic";
               else
                 icon_name = "window-maximize-symbolic";
               break;
            case COBIWM_BUTTON_TYPE_MINIMIZE:
               icon_name = "window-minimize-symbolic";
               break;
            case COBIWM_BUTTON_TYPE_MENU:
               icon_name = "open-menu-symbolic";
               break;
            case COBIWM_BUTTON_TYPE_APPMENU:
               surface = cairo_surface_reference (mini_icon);
               break;
            default:
               icon_name = NULL;
               break;
            }

          if (icon_name)
            {
              GtkIconTheme *theme = gtk_icon_theme_get_default ();
              GtkIconInfo *info;
              GdkPixbuf *pixbuf;

              info = gtk_icon_theme_lookup_icon_for_scale (theme, icon_name,
                                                           layout->icon_size, scale, 0);
              pixbuf = gtk_icon_info_load_symbolic_for_context (info, style, NULL, NULL);
              surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
            }

          if (surface)
            {
              float width, height;
              int x, y;

              width = cairo_image_surface_get_width (surface) / scale;
              height = cairo_image_surface_get_height (surface) / scale;
              x = button_rect.x + (button_rect.width - width) / 2;
              y = button_rect.y + (button_rect.height - height) / 2;

              cairo_translate (cr, x, y);
              cairo_scale (cr,
                           width / layout->icon_size,
                           height / layout->icon_size);
              cairo_set_source_surface (cr, surface, 0, 0);
              cairo_paint (cr);

              cairo_surface_destroy (surface);
            }
        }
      cairo_restore (cr);
      if (button_class)
        gtk_style_context_remove_class (style, button_class);
      gtk_style_context_set_state (style, state);
    }
}

/**
 * cobiwm_theme_get_default: (skip)
 *
 */
CobiwmTheme*
cobiwm_theme_get_default (void)
{
  static CobiwmTheme *theme = NULL;
  int frame_type;

  if (theme)
    return theme;

  theme = cobiwm_theme_new ();

  for (frame_type = 0; frame_type < COBIWM_FRAME_TYPE_LAST; frame_type++)
    {
      CobiwmFrameLayout *layout = cobiwm_frame_layout_new ();

      switch (frame_type)
        {
        case COBIWM_FRAME_TYPE_NORMAL:
        case COBIWM_FRAME_TYPE_DIALOG:
        case COBIWM_FRAME_TYPE_MODAL_DIALOG:
        case COBIWM_FRAME_TYPE_ATTACHED:
          break;
        case COBIWM_FRAME_TYPE_MENU:
        case COBIWM_FRAME_TYPE_UTILITY:
          layout->title_scale = PANGO_SCALE_SMALL;
          break;
        case COBIWM_FRAME_TYPE_BORDER:
          layout->has_title = FALSE;
          layout->hide_buttons = TRUE;
          break;
        default:
          g_assert_not_reached ();
        }

      theme->layouts[frame_type] = layout;
    }
  return theme;
}

/**
 * cobiwm_theme_new: (skip)
 *
 */
CobiwmTheme*
cobiwm_theme_new (void)
{
  return g_new0 (CobiwmTheme, 1);
}


void
cobiwm_theme_free (CobiwmTheme *theme)
{
  int i;

  g_return_if_fail (theme != NULL);

  for (i = 0; i < COBIWM_FRAME_TYPE_LAST; i++)
    if (theme->layouts[i])
      cobiwm_frame_layout_free (theme->layouts[i]);

  DEBUG_FILL_STRUCT (theme);
  g_free (theme);
}

CobiwmFrameLayout*
cobiwm_theme_get_frame_layout (CobiwmTheme     *theme,
                             CobiwmFrameType  type)
{
  g_return_val_if_fail (type < COBIWM_FRAME_TYPE_LAST, NULL);

  return theme->layouts[type];
}

static GtkStyleContext *
create_style_context (GType            widget_type,
                      GtkStyleContext *parent_style,
                      GtkCssProvider  *provider,
                      const char      *object_name,
                      const char      *first_class,
                      ...)
{
  GtkStyleContext *style;
  GtkStateFlags state;
  GtkWidgetPath *path;
  const char *name;
  va_list ap;

  style = gtk_style_context_new ();
  gtk_style_context_set_scale (style, cobiwm_theme_get_window_scaling_factor ());
  gtk_style_context_set_parent (style, parent_style);

  if (parent_style)
    path = gtk_widget_path_copy (gtk_style_context_get_path (parent_style));
  else
    path = gtk_widget_path_new ();

  gtk_widget_path_append_type (path, widget_type);

  if (object_name)
    gtk_widget_path_iter_set_object_name (path, -1, object_name);

  state = gtk_style_context_get_state (style);
  if (cobiwm_get_locale_direction() == COBIWM_LOCALE_DIRECTION_RTL)
    {
      state |= GTK_STATE_FLAG_DIR_RTL;
      state &= ~GTK_STATE_FLAG_DIR_LTR;
    }
  else
    {
      state |= GTK_STATE_FLAG_DIR_LTR;
      state &= ~GTK_STATE_FLAG_DIR_RTL;
    }
  gtk_style_context_set_state (style, state);

  va_start (ap, first_class);
  for (name = first_class; name; name = va_arg (ap, const char *))
    gtk_widget_path_iter_add_class (path, -1, name);
  va_end (ap);

  gtk_style_context_set_path (style, path);
  gtk_widget_path_unref (path);

  gtk_style_context_add_provider (style, GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);

  return style;
}

CobiwmStyleInfo *
cobiwm_theme_create_style_info (GdkScreen   *screen,
                              const gchar *variant)
{
  CobiwmStyleInfo *style_info;
  GtkCssProvider *provider;
  char *theme_name;

  g_object_get (gtk_settings_get_for_screen (screen),
                "gtk-theme-name", &theme_name,
                NULL);

  if (theme_name && *theme_name)
    provider = gtk_css_provider_get_named (theme_name, variant);
  else
    provider = gtk_css_provider_get_default ();
  g_free (theme_name);

  style_info = g_new0 (CobiwmStyleInfo, 1);
  style_info->refcount = 1;

  style_info->styles[COBIWM_STYLE_ELEMENT_WINDOW] =
    create_style_context (COBIWM_TYPE_FRAMES,
                          NULL,
                          provider,
                          "window",
                          GTK_STYLE_CLASS_BACKGROUND,
                          "ssd",
                          NULL);
  style_info->styles[COBIWM_STYLE_ELEMENT_FRAME] =
    create_style_context (COBIWM_TYPE_FRAMES,
                          style_info->styles[COBIWM_STYLE_ELEMENT_WINDOW],
                          provider,
                          "decoration",
                          NULL);
  style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR] =
    create_style_context (GTK_TYPE_HEADER_BAR,
                          style_info->styles[COBIWM_STYLE_ELEMENT_FRAME],
                          provider,
                          "headerbar",
                          GTK_STYLE_CLASS_TITLEBAR,
                          GTK_STYLE_CLASS_HORIZONTAL,
                          "default-decoration",
                          NULL);
  style_info->styles[COBIWM_STYLE_ELEMENT_TITLE] =
    create_style_context (GTK_TYPE_LABEL,
                          style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR],
                          provider,
                          "label",
                          GTK_STYLE_CLASS_TITLE,
                          NULL);
  style_info->styles[COBIWM_STYLE_ELEMENT_BUTTON] =
    create_style_context (GTK_TYPE_BUTTON,
                          style_info->styles[COBIWM_STYLE_ELEMENT_TITLEBAR],
                          provider,
                          "button",
                          "titlebutton",
                          NULL);
  style_info->styles[COBIWM_STYLE_ELEMENT_IMAGE] =
    create_style_context (GTK_TYPE_IMAGE,
                          style_info->styles[COBIWM_STYLE_ELEMENT_BUTTON],
                          provider,
                          "image",
                          NULL);
  return style_info;
}

CobiwmStyleInfo *
cobiwm_style_info_ref (CobiwmStyleInfo *style_info)
{
  g_return_val_if_fail (style_info != NULL, NULL);
  g_return_val_if_fail (style_info->refcount > 0, NULL);

  g_atomic_int_inc ((volatile int *)&style_info->refcount);
  return style_info;
}

void
cobiwm_style_info_unref (CobiwmStyleInfo *style_info)
{
  g_return_if_fail (style_info != NULL);
  g_return_if_fail (style_info->refcount > 0);

  if (g_atomic_int_dec_and_test ((volatile int *)&style_info->refcount))
    {
      int i;
      for (i = 0; i < COBIWM_STYLE_ELEMENT_LAST; i++)
        g_object_unref (style_info->styles[i]);
      g_free (style_info);
    }
}

static void
add_toplevel_class (GtkStyleContext *style,
                    const char      *class_name)
{
  if (gtk_style_context_get_parent (style))
    {
      GtkWidgetPath *path;

      path = gtk_widget_path_copy (gtk_style_context_get_path (style));
      gtk_widget_path_iter_add_class (path, 0, class_name);
      gtk_style_context_set_path (style, path);
      gtk_widget_path_unref (path);
    }
  else
    gtk_style_context_add_class (style, class_name);
}

static void
remove_toplevel_class (GtkStyleContext *style,
                       const char      *class_name)
{
  if (gtk_style_context_get_parent (style))
    {
      GtkWidgetPath *path;

      path = gtk_widget_path_copy (gtk_style_context_get_path (style));
      gtk_widget_path_iter_remove_class (path, 0, class_name);
      gtk_style_context_set_path (style, path);
      gtk_widget_path_unref (path);
    }
  else
    gtk_style_context_remove_class (style, class_name);
}

void
cobiwm_style_info_set_flags (CobiwmStyleInfo  *style_info,
                           CobiwmFrameFlags  flags)
{
  GtkStyleContext *style;
  const char *class_name = NULL;
  gboolean backdrop;
  GtkStateFlags state;
  int i;

  backdrop = !(flags & COBIWM_FRAME_HAS_FOCUS);
  if (flags & COBIWM_FRAME_IS_FLASHING)
    backdrop = !backdrop;

  if (flags & COBIWM_FRAME_MAXIMIZED)
    class_name = "maximized";
  else if (flags & COBIWM_FRAME_TILED_LEFT ||
           flags & COBIWM_FRAME_TILED_RIGHT)
    class_name = "tiled";

  for (i = 0; i < COBIWM_STYLE_ELEMENT_LAST; i++)
    {
      style = style_info->styles[i];

      state = gtk_style_context_get_state (style);
      if (backdrop)
        gtk_style_context_set_state (style, state | GTK_STATE_FLAG_BACKDROP);
      else
        gtk_style_context_set_state (style, state & ~GTK_STATE_FLAG_BACKDROP);

      remove_toplevel_class (style, "maximized");
      remove_toplevel_class (style, "tiled");

      if (class_name)
        add_toplevel_class (style, class_name);
    }
}

PangoFontDescription*
cobiwm_style_info_create_font_desc (CobiwmStyleInfo *style_info)
{
  PangoFontDescription *font_desc;
  const PangoFontDescription *override = cobiwm_prefs_get_titlebar_font ();
  GtkStyleContext *context = style_info->styles[COBIWM_STYLE_ELEMENT_TITLE];

  gtk_style_context_get (context,
                         gtk_style_context_get_state (context),
                         "font", &font_desc, NULL);

  if (override)
    pango_font_description_merge (font_desc, override, TRUE);

  return font_desc;
}

void
cobiwm_theme_draw_frame (CobiwmTheme              *theme,
                       CobiwmStyleInfo          *style_info,
                       cairo_t                *cr,
                       CobiwmFrameType           type,
                       CobiwmFrameFlags          flags,
                       int                     client_width,
                       int                     client_height,
                       PangoLayout            *title_layout,
                       int                     text_height,
                       const CobiwmButtonLayout *button_layout,
                       CobiwmButtonState         button_states[COBIWM_BUTTON_TYPE_LAST],
                       cairo_surface_t        *mini_icon)
{
  CobiwmFrameGeometry fgeom;
  CobiwmFrameLayout *layout;

  g_return_if_fail (type < COBIWM_FRAME_TYPE_LAST);

  layout = theme->layouts[type];

  /* Parser is not supposed to allow this currently */
  if (layout == NULL)
    return;

  cobiwm_frame_layout_calc_geometry (layout,
                                   style_info,
                                   text_height,
                                   flags,
                                   client_width, client_height,
                                   button_layout,
                                   type,
                                   &fgeom,
                                   theme);

  cobiwm_frame_layout_draw_with_style (layout,
                                     style_info,
                                     cr,
                                     &fgeom,
                                     title_layout,
                                     flags,
                                     button_states,
                                     mini_icon);
}

void
cobiwm_theme_get_frame_borders (CobiwmTheme        *theme,
                              CobiwmStyleInfo    *style_info,
                              CobiwmFrameType     type,
                              int               text_height,
                              CobiwmFrameFlags    flags,
                              CobiwmFrameBorders *borders)
{
  CobiwmFrameLayout *layout;

  g_return_if_fail (type < COBIWM_FRAME_TYPE_LAST);

  layout = theme->layouts[type];

  cobiwm_frame_borders_clear (borders);

  /* Parser is not supposed to allow this currently */
  if (layout == NULL)
    return;

  cobiwm_frame_layout_sync_with_style (layout, style_info, flags);

  cobiwm_frame_layout_get_borders (layout,
                                 text_height,
                                 flags, type,
                                 borders);
}

void
cobiwm_theme_calc_geometry (CobiwmTheme              *theme,
                          CobiwmStyleInfo          *style_info,
                          CobiwmFrameType           type,
                          int                     text_height,
                          CobiwmFrameFlags          flags,
                          int                     client_width,
                          int                     client_height,
                          const CobiwmButtonLayout *button_layout,
                          CobiwmFrameGeometry      *fgeom)
{
  CobiwmFrameLayout *layout;

  g_return_if_fail (type < COBIWM_FRAME_TYPE_LAST);

  layout = theme->layouts[type];

  /* Parser is not supposed to allow this currently */
  if (layout == NULL)
    return;

  cobiwm_frame_layout_calc_geometry (layout,
                                   style_info,
                                   text_height,
                                   flags,
                                   client_width, client_height,
                                   button_layout,
                                   type,
                                   fgeom,
                                   theme);
}

/**
 * cobiwm_pango_font_desc_get_text_height:
 * @font_desc: the font
 * @context: the context of the font
 *
 * Returns the height of the letters in a particular font.
 *
 * Returns: the height of the letters
 */
int
cobiwm_pango_font_desc_get_text_height (const PangoFontDescription *font_desc,
                                      PangoContext         *context)
{
  PangoFontMetrics *metrics;
  PangoLanguage *lang;
  int retval;

  lang = pango_context_get_language (context);
  metrics = pango_context_get_metrics (context, font_desc, lang);

  retval = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) +
                         pango_font_metrics_get_descent (metrics));

  pango_font_metrics_unref (metrics);

  return retval;
}

/**
 * cobiwm_frame_type_to_string:
 * @type: a #CobiwmFrameType
 *
 * Converts a frame type enum value to the name string that would
 * appear in the theme definition file.
 *
 * Return value: the string value
 */
const char*
cobiwm_frame_type_to_string (CobiwmFrameType type)
{
  switch (type)
    {
    case COBIWM_FRAME_TYPE_NORMAL:
      return "normal";
    case COBIWM_FRAME_TYPE_DIALOG:
      return "dialog";
    case COBIWM_FRAME_TYPE_MODAL_DIALOG:
      return "modal_dialog";
    case COBIWM_FRAME_TYPE_UTILITY:
      return "utility";
    case COBIWM_FRAME_TYPE_MENU:
      return "menu";
    case COBIWM_FRAME_TYPE_BORDER:
      return "border";
    case COBIWM_FRAME_TYPE_ATTACHED:
      return "attached";
#if 0
    case COBIWM_FRAME_TYPE_TOOLBAR:
      return "toolbar";
#endif
    case  COBIWM_FRAME_TYPE_LAST:
      break;
    }

  return "<unknown>";
}
