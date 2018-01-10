/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* CobiwmGroup property handling */

/*
 * Copyright (C) 2002 Red Hat, Inc.
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
#include "group-props.h"
#include "group-private.h"
#include "xprops.h"
#include <X11/Xatom.h>

typedef void (* InitValueFunc)   (CobiwmDisplay   *display,
                                  Atom           property,
                                  CobiwmPropValue *value);
typedef void (* ReloadValueFunc) (CobiwmGroup     *group,
                                  CobiwmPropValue *value);

struct _CobiwmGroupPropHooks
{
  Atom property;
  InitValueFunc   init_func;
  ReloadValueFunc reload_func;
};

static void                init_prop_value   (CobiwmDisplay   *display,
                                              Atom           property,
                                              CobiwmPropValue *value);
static void                reload_prop_value (CobiwmGroup     *group,
                                              CobiwmPropValue *value);
static CobiwmGroupPropHooks* find_hooks        (CobiwmDisplay   *display,
                                              Atom           property);



void
cobiwm_group_reload_property (CobiwmGroup *group,
                            Atom       property)
{
  cobiwm_group_reload_properties (group, &property, 1);
}

void
cobiwm_group_reload_properties (CobiwmGroup  *group,
                              const Atom *properties,
                              int         n_properties)
{
  int i;
  CobiwmPropValue *values;

  g_return_if_fail (properties != NULL);
  g_return_if_fail (n_properties > 0);

  values = g_new0 (CobiwmPropValue, n_properties);

  i = 0;
  while (i < n_properties)
    {
      init_prop_value (group->display, properties[i], &values[i]);
      ++i;
    }

  cobiwm_prop_get_values (group->display, group->group_leader,
                        values, n_properties);

  i = 0;
  while (i < n_properties)
    {
      reload_prop_value (group, &values[i]);

      ++i;
    }

  cobiwm_prop_free_values (values, n_properties);

  g_free (values);
}

/* Fill in the CobiwmPropValue used to get the value of "property" */
static void
init_prop_value (CobiwmDisplay   *display,
                 Atom           property,
                 CobiwmPropValue *value)
{
  CobiwmGroupPropHooks *hooks;

  value->type = COBIWM_PROP_VALUE_INVALID;
  value->atom = None;

  hooks = find_hooks (display, property);
  if (hooks && hooks->init_func != NULL)
    (* hooks->init_func) (display, property, value);
}

static void
reload_prop_value (CobiwmGroup    *group,
                   CobiwmPropValue *value)
{
  CobiwmGroupPropHooks *hooks;

  hooks = find_hooks (group->display, value->atom);
  if (hooks && hooks->reload_func != NULL)
    (* hooks->reload_func) (group, value);
}

static void
init_wm_client_machine (CobiwmDisplay   *display,
                        Atom           property,
                        CobiwmPropValue *value)
{
  value->type = COBIWM_PROP_VALUE_STRING;
  value->atom = display->atom_WM_CLIENT_MACHINE;
}

static void
reload_wm_client_machine (CobiwmGroup     *group,
                          CobiwmPropValue *value)
{
  g_free (group->wm_client_machine);
  group->wm_client_machine = NULL;

  if (value->type != COBIWM_PROP_VALUE_INVALID)
    group->wm_client_machine = g_strdup (value->v.str);

  cobiwm_verbose ("Group has client machine \"%s\"\n",
                group->wm_client_machine ? group->wm_client_machine : "unset");
}

static void
init_net_startup_id (CobiwmDisplay   *display,
                     Atom           property,
                     CobiwmPropValue *value)
{
  value->type = COBIWM_PROP_VALUE_UTF8;
  value->atom = display->atom__NET_STARTUP_ID;
}

static void
reload_net_startup_id (CobiwmGroup     *group,
                       CobiwmPropValue *value)
{
  g_free (group->startup_id);
  group->startup_id = NULL;

  if (value->type != COBIWM_PROP_VALUE_INVALID)
    group->startup_id = g_strdup (value->v.str);

  cobiwm_verbose ("Group has startup id \"%s\"\n",
                group->startup_id ? group->startup_id : "unset");
}

#define N_HOOKS 3

void
cobiwm_display_init_group_prop_hooks (CobiwmDisplay *display)
{
  int i;
  CobiwmGroupPropHooks *hooks;

  g_assert (display->group_prop_hooks == NULL);

  display->group_prop_hooks = g_new0 (CobiwmGroupPropHooks, N_HOOKS);
  hooks = display->group_prop_hooks;

  i = 0;

  hooks[i].property = display->atom_WM_CLIENT_MACHINE;
  hooks[i].init_func = init_wm_client_machine;
  hooks[i].reload_func = reload_wm_client_machine;
  ++i;

  hooks[i].property = display->atom__NET_WM_PID;
  hooks[i].init_func = NULL;
  hooks[i].reload_func = NULL;
  ++i;

  hooks[i].property = display->atom__NET_STARTUP_ID;
  hooks[i].init_func = init_net_startup_id;
  hooks[i].reload_func = reload_net_startup_id;
  ++i;

  if (i != N_HOOKS)
    {
      g_error ("Initialized %d group hooks should have been %d\n", i, N_HOOKS);
    }
}

void
cobiwm_display_free_group_prop_hooks (CobiwmDisplay *display)
{
  g_assert (display->group_prop_hooks != NULL);

  g_free (display->group_prop_hooks);
  display->group_prop_hooks = NULL;
}

static CobiwmGroupPropHooks*
find_hooks (CobiwmDisplay *display,
            Atom         property)
{
  int i;

  /* FIXME we could sort the array and do binary search or
   * something
   */

  i = 0;
  while (i < N_HOOKS)
    {
      if (display->group_prop_hooks[i].property == property)
        return &display->group_prop_hooks[i];

      ++i;
    }

  return NULL;
}
