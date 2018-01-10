/*
 * Copyright (C) 2013 Red Hat, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef COBIWM_WAYLAND_TYPES_H
#define COBIWM_WAYLAND_TYPES_H

typedef struct _CobiwmWaylandCompositor CobiwmWaylandCompositor;

typedef struct _CobiwmWaylandSeat CobiwmWaylandSeat;
typedef struct _CobiwmWaylandPointer CobiwmWaylandPointer;
typedef struct _CobiwmWaylandPointerGrab CobiwmWaylandPointerGrab;
typedef struct _CobiwmWaylandPointerGrabInterface CobiwmWaylandPointerGrabInterface;
typedef struct _CobiwmWaylandPopupGrab CobiwmWaylandPopupGrab;
typedef struct _CobiwmWaylandPopup CobiwmWaylandPopup;
typedef struct _CobiwmWaylandKeyboard CobiwmWaylandKeyboard;
typedef struct _CobiwmWaylandKeyboardGrab CobiwmWaylandKeyboardGrab;
typedef struct _CobiwmWaylandKeyboardGrabInterface CobiwmWaylandKeyboardGrabInterface;
typedef struct _CobiwmWaylandTouch CobiwmWaylandTouch;
typedef struct _CobiwmWaylandDragDestFuncs CobiwmWaylandDragDestFuncs;
typedef struct _CobiwmWaylandDataOffer CobiwmWaylandDataOffer;
typedef struct _CobiwmWaylandDataDevice CobiwmWaylandDataDevice;

typedef struct _CobiwmWaylandBuffer CobiwmWaylandBuffer;
typedef struct _CobiwmWaylandRegion CobiwmWaylandRegion;

typedef struct _CobiwmWaylandSurface CobiwmWaylandSurface;

typedef struct _CobiwmWaylandOutput CobiwmWaylandOutput;

typedef struct _CobiwmWaylandSerial CobiwmWaylandSerial;

typedef struct _CobiwmWaylandPointerClient CobiwmWaylandPointerClient;

#endif
