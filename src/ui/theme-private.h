/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwmcity Theme Rendering */

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

#ifndef COBIWM_THEME_PRIVATE_H
#define COBIWM_THEME_PRIVATE_H

#include <boxes.h>
#include <theme.h>
#include <common.h>
#include <gtk/gtk.h>

/**
 * CobiwmStyleInfo: (skip)
 *
 */
typedef struct _CobiwmStyleInfo CobiwmStyleInfo;
/**
 * CobiwmFrameLayout: (skip)
 *
 */
typedef struct _CobiwmFrameLayout CobiwmFrameLayout;
/**
 * CobiwmButtonSpace: (skip)
 *
 */
typedef struct _CobiwmButtonSpace CobiwmButtonSpace;
/**
 * CobiwmFrameGeometry: (skip)
 *
 */
typedef struct _CobiwmFrameGeometry CobiwmFrameGeometry;

/**
 * Various parameters used to calculate the geometry of a frame.
 **/
struct _CobiwmFrameLayout
{
  /** Invisible border required by the theme */
  GtkBorder invisible_border;
  /** Border/padding of the entire frame */
  GtkBorder frame_border;
  /** Border/padding of the titlebar region */
  GtkBorder titlebar_border;
  /** Border/padding of titlebar buttons */
  GtkBorder button_border;

  /** Margin of title */
  GtkBorder title_margin;
  /** Margin of titlebar buttons */
  GtkBorder button_margin;

  /** Min size of titlebar region */
  GtkRequisition titlebar_min_size;
  /** Min size of titlebar buttons */
  GtkRequisition button_min_size;

  /** Size of images in buttons */
  guint icon_size;

  /** Space between titlebar elements */
  guint titlebar_spacing;

  /** scale factor for title text */
  double title_scale;

  /** Whether title text will be displayed */
  guint has_title : 1;

  /** Whether we should hide the buttons */
  guint hide_buttons : 1;

  /** Radius of the top left-hand corner; 0 if not rounded */
  guint top_left_corner_rounded_radius;
  /** Radius of the top right-hand corner; 0 if not rounded */
  guint top_right_corner_rounded_radius;
  /** Radius of the bottom left-hand corner; 0 if not rounded */
  guint bottom_left_corner_rounded_radius;
  /** Radius of the bottom right-hand corner; 0 if not rounded */
  guint bottom_right_corner_rounded_radius;
};

/**
 * The computed size of a button (really just a way of tying its
 * visible and clickable areas together).
 * The reason for two different rectangles here is Fitts' law & maximized
 * windows; see bug #97703 for more details.
 */
struct _CobiwmButtonSpace
{
  /** The screen area where the button's image is drawn */
  GdkRectangle visible;
  /** The screen area where the button can be activated by clicking */
  GdkRectangle clickable;
};

/**
 * Calculated actual geometry of the frame
 */
struct _CobiwmFrameGeometry
{
  CobiwmFrameBorders borders;

  int width;
  int height;

  GdkRectangle title_rect;

  GtkBorder content_border;

  /* used for a memset hack */
#define ADDRESS_OF_BUTTON_RECTS(fgeom) (((char*)(fgeom)) + G_STRUCT_OFFSET (CobiwmFrameGeometry, close_rect))
#define LENGTH_OF_BUTTON_RECTS (G_STRUCT_OFFSET (CobiwmFrameGeometry, appmenu_rect) + sizeof (CobiwmButtonSpace) - G_STRUCT_OFFSET (CobiwmFrameGeometry, close_rect))

  /* The button rects (if changed adjust memset hack) */
  CobiwmButtonSpace close_rect;
  CobiwmButtonSpace max_rect;
  CobiwmButtonSpace min_rect;
  CobiwmButtonSpace menu_rect;
  CobiwmButtonSpace appmenu_rect;
  /* End of button rects (if changed adjust memset hack) */

  /* Saved button layout */
  CobiwmButtonLayout button_layout;
  int n_left_buttons;
  int n_right_buttons;

  /* Round corners */
  guint top_left_corner_rounded_radius;
  guint top_right_corner_rounded_radius;
  guint bottom_left_corner_rounded_radius;
  guint bottom_right_corner_rounded_radius;
};

typedef enum
{
  COBIWM_BUTTON_STATE_NORMAL,
  COBIWM_BUTTON_STATE_PRESSED,
  COBIWM_BUTTON_STATE_PRELIGHT,
  COBIWM_BUTTON_STATE_LAST
} CobiwmButtonState;

typedef enum
{
  COBIWM_BUTTON_TYPE_CLOSE,
  COBIWM_BUTTON_TYPE_MAXIMIZE,
  COBIWM_BUTTON_TYPE_MINIMIZE,
  COBIWM_BUTTON_TYPE_MENU,
  COBIWM_BUTTON_TYPE_APPMENU,
  COBIWM_BUTTON_TYPE_LAST
} CobiwmButtonType;

typedef enum
{
  COBIWM_STYLE_ELEMENT_WINDOW,
  COBIWM_STYLE_ELEMENT_FRAME,
  COBIWM_STYLE_ELEMENT_TITLEBAR,
  COBIWM_STYLE_ELEMENT_TITLE,
  COBIWM_STYLE_ELEMENT_BUTTON,
  COBIWM_STYLE_ELEMENT_IMAGE,
  COBIWM_STYLE_ELEMENT_LAST
} CobiwmStyleElement;

struct _CobiwmStyleInfo
{
  int refcount;

  GtkStyleContext *styles[COBIWM_STYLE_ELEMENT_LAST];
};

/* Kinds of frame...
 *
 *  normal ->   focused / unfocused
 *  max    ->   focused / unfocused
 *  shaded ->   focused / unfocused
 *  max/shaded -> focused / unfocused
 *
 *  so 4 states with 2 sub-states each, meaning 8 total
 *
 * 8 window states times 7 or 8 window types. Except some
 * window types never get a frame so that narrows it down a bit.
 *
 */
typedef enum
{
  COBIWM_FRAME_STATE_NORMAL,
  COBIWM_FRAME_STATE_MAXIMIZED,
  COBIWM_FRAME_STATE_TILED_LEFT,
  COBIWM_FRAME_STATE_TILED_RIGHT,
  COBIWM_FRAME_STATE_SHADED,
  COBIWM_FRAME_STATE_MAXIMIZED_AND_SHADED,
  COBIWM_FRAME_STATE_TILED_LEFT_AND_SHADED,
  COBIWM_FRAME_STATE_TILED_RIGHT_AND_SHADED,
  COBIWM_FRAME_STATE_LAST
} CobiwmFrameState;

typedef enum
{
  COBIWM_FRAME_FOCUS_NO,
  COBIWM_FRAME_FOCUS_YES,
  COBIWM_FRAME_FOCUS_LAST
} CobiwmFrameFocus;

/**
 * A theme. This is a singleton class which groups all settings from a theme
 * together.
 */
struct _CobiwmTheme
{
  CobiwmFrameLayout *layouts[COBIWM_FRAME_TYPE_LAST];
};

void               cobiwm_frame_layout_apply_scale (const CobiwmFrameLayout *layout,
                                                  PangoFontDescription  *font_desc);

CobiwmFrameLayout* cobiwm_theme_get_frame_layout (CobiwmTheme     *theme,
                                              CobiwmFrameType  type);

CobiwmStyleInfo * cobiwm_theme_create_style_info (GdkScreen   *screen,
                                              const gchar *variant);
CobiwmStyleInfo * cobiwm_style_info_ref          (CobiwmStyleInfo *style);
void            cobiwm_style_info_unref        (CobiwmStyleInfo  *style_info);

void            cobiwm_style_info_set_flags    (CobiwmStyleInfo  *style_info,
                                              CobiwmFrameFlags  flags);

PangoFontDescription * cobiwm_style_info_create_font_desc (CobiwmStyleInfo *style_info);

void cobiwm_theme_draw_frame (CobiwmTheme              *theme,
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
                            cairo_surface_t        *mini_icon);

void cobiwm_theme_get_frame_borders (CobiwmTheme         *theme,
                                   CobiwmStyleInfo     *style_info,
                                   CobiwmFrameType      type,
                                   int                text_height,
                                   CobiwmFrameFlags     flags,
                                   CobiwmFrameBorders  *borders);

void cobiwm_theme_calc_geometry (CobiwmTheme              *theme,
                               CobiwmStyleInfo          *style_info,
                               CobiwmFrameType           type,
                               int                     text_height,
                               CobiwmFrameFlags          flags,
                               int                     client_width,
                               int                     client_height,
                               const CobiwmButtonLayout *button_layout,
                               CobiwmFrameGeometry      *fgeom);

/* random stuff */

int                   cobiwm_pango_font_desc_get_text_height (const PangoFontDescription *font_desc,
                                                            PangoContext         *context);
int                   cobiwm_theme_get_window_scaling_factor (void);

#endif /* COBIWM_THEME_PRIVATE_H */
