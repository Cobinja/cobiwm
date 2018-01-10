/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Cobiwm X property convenience routines */

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

#ifndef COBIWM_XPROPS_H
#define COBIWM_XPROPS_H

#include <config.h>

#include <display.h>
#include <X11/Xutil.h>

#include <X11/extensions/sync.h>

/* Copied from Lesstif by way of GTK. Rudimentary docs can be
 * found in some Motif reference guides online.
 */
typedef struct {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    uint32_t input_mode;
    uint32_t status;
} MotifWmHints, MwmHints;

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_FUNC_ALL            (1L << 0)
#define MWM_FUNC_RESIZE         (1L << 1)
#define MWM_FUNC_MOVE           (1L << 2)
#define MWM_FUNC_MINIMIZE       (1L << 3)
#define MWM_FUNC_MAXIMIZE       (1L << 4)
#define MWM_FUNC_CLOSE          (1L << 5)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define MWM_INPUT_MODELESS 0
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1
#define MWM_INPUT_SYSTEM_MODAL 2
#define MWM_INPUT_FULL_APPLICATION_MODAL 3
#define MWM_INPUT_APPLICATION_MODAL MWM_INPUT_PRIMARY_APPLICATION_MODAL

#define MWM_TEAROFF_WINDOW	(1L<<0)

/* These all return the memory from Xlib, so require an XFree()
 * when they return TRUE. They return TRUE on success.
 */
gboolean cobiwm_prop_get_motif_hints   (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      MotifWmHints **hints_p);
gboolean cobiwm_prop_get_cardinal_list (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      uint32_t     **cardinals_p,
                                      int           *n_cardinals_p);
gboolean cobiwm_prop_get_latin1_string (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      char         **str_p);
gboolean cobiwm_prop_get_utf8_list     (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      char        ***str_p,
                                      int           *n_str_p);
void     cobiwm_prop_set_utf8_string_hint
                                     (CobiwmDisplay *display,
                                      Window xwindow,
                                      Atom atom,
                                      const char *val);
gboolean cobiwm_prop_get_window        (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      Window        *window_p);
gboolean cobiwm_prop_get_cardinal      (CobiwmDisplay   *display,
                                      Window         xwindow,
                                      Atom           xatom,
                                      uint32_t      *cardinal_p);
gboolean cobiwm_prop_get_cardinal_with_atom_type (CobiwmDisplay   *display,
                                                Window         xwindow,
                                                Atom           xatom,
                                                Atom           prop_type,
                                                uint32_t      *cardinal_p);

typedef enum
{
  COBIWM_PROP_VALUE_INVALID,
  COBIWM_PROP_VALUE_UTF8,
  COBIWM_PROP_VALUE_STRING,
  COBIWM_PROP_VALUE_STRING_AS_UTF8,
  COBIWM_PROP_VALUE_MOTIF_HINTS,
  COBIWM_PROP_VALUE_CARDINAL,
  COBIWM_PROP_VALUE_WINDOW,
  COBIWM_PROP_VALUE_CARDINAL_LIST,
  COBIWM_PROP_VALUE_UTF8_LIST,
  COBIWM_PROP_VALUE_ATOM_LIST,
  COBIWM_PROP_VALUE_TEXT_PROPERTY, /* comes back as UTF-8 string */
  COBIWM_PROP_VALUE_WM_HINTS,
  COBIWM_PROP_VALUE_CLASS_HINT,
  COBIWM_PROP_VALUE_SIZE_HINTS,
  COBIWM_PROP_VALUE_SYNC_COUNTER,     /* comes back as CARDINAL */
  COBIWM_PROP_VALUE_SYNC_COUNTER_LIST /* comes back as CARDINAL */
} CobiwmPropValueType;

/* used to request/return/store property values */
typedef struct
{
  CobiwmPropValueType type;
  Atom atom;
  Atom required_type; /* autofilled if None */

  union
  {
    char *str;
    MotifWmHints *motif_hints;
    Window xwindow;
    uint32_t cardinal;
    XWMHints *wm_hints;
    XClassHint class_hint;
    XSyncCounter xcounter;
    struct
    {
      uint32_t *counters;
      int       n_counters;
    } xcounter_list;

    struct
    {
      XSizeHints   *hints;
      unsigned long flags;
    } size_hints;

    struct
    {
      uint32_t *cardinals;
      int       n_cardinals;
    } cardinal_list;

    struct
    {
      char **strings;
      int    n_strings;
    } string_list;

    struct
    {
      uint32_t *atoms;
      int       n_atoms;
    } atom_list;

  } v;

} CobiwmPropValue;

/* Each value has type and atom initialized. If there's an error,
 * or property is unset, type comes back as INVALID;
 * else type comes back as it originated, and the data
 * is filled in.
 */
void cobiwm_prop_get_values (CobiwmDisplay   *display,
                           Window         xwindow,
                           CobiwmPropValue *values,
                           int            n_values);

void cobiwm_prop_free_values (CobiwmPropValue *values,
                            int            n_values);

#endif




