# Flag build for parallelism; see https://savannah.gnu.org/patch/?6905
.AUTOPARALLEL:

EXTRA_DIST =
NULL =

AM_CPPFLAGS = \
	-DCLUTTER_ENABLE_COMPOSITOR_API					\
	-DCLUTTER_ENABLE_EXPERIMENTAL_API				\
	-DCOGL_ENABLE_EXPERIMENTAL_API					\
	-DCOGL_ENABLE_EXPERIMENTAL_2_0_API                              \
	-DCLUTTER_DISABLE_DEPRECATION_WARNINGS				\
	-DCOGL_DISABLE_DEPRECATION_WARNINGS				\
	$(COBIWM_CFLAGS)						\
	$(COBIWM_NATIVE_BACKEND_CFLAGS)					\
	-I$(builddir)							\
	-I$(srcdir)							\
	-I$(srcdir)/backends						\
	-I$(srcdir)/core						\
	-I$(srcdir)/ui							\
	-I$(srcdir)/compositor						\
	-I$(srcdir)/include						\
	-DCOBIWM_LIBEXECDIR=\"$(libexecdir)\"				\
	-DCOBIWM_LOCALEDIR=\"$(localedir)\"				\
	-DCOBIWM_PKGDATADIR=\"$(pkgdatadir)\"				\
	-DCOBIWM_DATADIR=\"$(datadir)\"					\
	-DG_LOG_DOMAIN=\"cobiwm\"					\
	-DSN_API_NOT_YET_FROZEN=1					\
	-DCOBIWM_PKGLIBDIR=\"$(pkglibdir)\"				\
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\"			\
	-DXWAYLAND_PATH=\"$(XWAYLAND_PATH)\"				\
	$(NULL)

cobiwm_built_sources = \
	$(dbus_idle_built_sources)		\
	$(dbus_display_config_built_sources)	\
	$(dbus_login1_built_sources)		\
	include/cobiwm-enum-types.h			\
	core/cobiwm-enum-types.c			\
	$(NULL)

if HAVE_WAYLAND
cobiwm_built_sources += \
	pointer-gestures-unstable-v1-protocol.c				\
	pointer-gestures-unstable-v1-server-protocol.h			\
	gtk-shell-protocol.c			\
	gtk-shell-server-protocol.h		\
	gtk-primary-selection-protocol.c				\
	gtk-primary-selection-server-protocol.h				\
	xdg-shell-unstable-v5-protocol.c				\
	xdg-shell-unstable-v5-server-protocol.h				\
	relative-pointer-unstable-v1-protocol.c				\
	relative-pointer-unstable-v1-server-protocol.h			\
	pointer-constraints-unstable-v1-protocol.c			\
	pointer-constraints-unstable-v1-server-protocol.h		\
	$(NULL)
endif

wayland_protocols =				\
	wayland/protocol/gtk-shell.xml		\
	wayland/protocol/gtk-primary-selection.xml	\
	$(NULL)

cobiwm_SOURCES =				\
	backends/cobiwm-backend.c			\
	include/cobiwm-backend.h			\
	backends/cobiwm-backend-private.h		\
	backends/cobiwm-barrier.c			\
	backends/cobiwm-barrier-private.h		\
	backends/cobiwm-cursor.c			\
	backends/cobiwm-cursor.h			\
	backends/cobiwm-cursor-tracker.c		\
	backends/cobiwm-cursor-tracker-private.h	\
	backends/cobiwm-cursor-renderer.c		\
	backends/cobiwm-cursor-renderer.h		\
	backends/cobiwm-display-config-shared.h	\
	backends/cobiwm-idle-monitor.c		\
	backends/cobiwm-idle-monitor-private.h	\
	backends/cobiwm-idle-monitor-dbus.c	\
	backends/cobiwm-idle-monitor-dbus.h	\
	backends/cobiwm-monitor-config.c		\
	backends/cobiwm-monitor-config.h		\
	backends/cobiwm-monitor-manager.c		\
	include/cobiwm-monitor-manager.h		\
	backends/cobiwm-monitor-manager-private.h	\
	backends/cobiwm-monitor-manager-dummy.c	\
	backends/cobiwm-monitor-manager-dummy.h	\
	backends/cobiwm-pointer-constraint.c	\
	backends/cobiwm-pointer-constraint.h	\
	backends/cobiwm-stage.h			\
	backends/cobiwm-stage.c			\
	backends/edid-parse.c			\
	backends/edid.h				\
	backends/x11/cobiwm-backend-x11.c			\
	backends/x11/cobiwm-backend-x11.h			\
	backends/x11/cobiwm-barrier-x11.c			\
	backends/x11/cobiwm-barrier-x11.h			\
	backends/x11/cobiwm-cursor-renderer-x11.c		\
	backends/x11/cobiwm-cursor-renderer-x11.h		\
	backends/x11/nested/cobiwm-cursor-renderer-x11-nested.c		\
	backends/x11/nested/cobiwm-cursor-renderer-x11-nested.h		\
	backends/x11/cobiwm-idle-monitor-xsync.c		\
	backends/x11/cobiwm-idle-monitor-xsync.h		\
	backends/x11/cobiwm-monitor-manager-xrandr.c	\
	backends/x11/cobiwm-monitor-manager-xrandr.h	\
	core/cobiwm-accel-parse.c			\
	core/cobiwm-accel-parse.h			\
	include/barrier.h				\
	core/bell.c				\
	core/bell.h				\
	core/boxes.c				\
	core/boxes-private.h			\
	include/boxes.h				\
	core/cobiwm-border.c			\
	core/cobiwm-border.h			\
	compositor/clutter-utils.c		\
	compositor/clutter-utils.h		\
	compositor/cogl-utils.c			\
	compositor/cogl-utils.h			\
	compositor/compositor.c			\
	compositor/compositor-private.h		\
	compositor/cobiwm-background.c		\
	compositor/cobiwm-background-private.h	\
	compositor/cobiwm-background-actor.c	\
	compositor/cobiwm-background-actor-private.h	\
	compositor/cobiwm-background-image.c	\
	compositor/cobiwm-background-group.c	\
	compositor/cobiwm-cullable.c		\
	compositor/cobiwm-cullable.h		\
	compositor/cobiwm-dnd-actor.c		\
	compositor/cobiwm-dnd-actor-private.h	\
	compositor/cobiwm-feedback-actor.c	\
	compositor/cobiwm-feedback-actor-private.h	\
	compositor/cobiwm-effect-manager.c	\
	compositor/cobiwm-effect-manager.h	\
	compositor/cobiwm-shadow-factory.c	\
	compositor/cobiwm-shaped-texture.c	\
	compositor/cobiwm-shaped-texture-private.h 	\
	compositor/cobiwm-surface-actor.c		\
	compositor/cobiwm-surface-actor.h		\
	compositor/cobiwm-surface-actor-x11.c	\
	compositor/cobiwm-surface-actor-x11.h	\
	compositor/cobiwm-sync-ring.c		\
	compositor/cobiwm-sync-ring.h		\
	compositor/cobiwm-texture-rectangle.c	\
	compositor/cobiwm-texture-rectangle.h	\
	compositor/cobiwm-texture-tower.c		\
	compositor/cobiwm-texture-tower.h		\
	compositor/cobiwm-window-actor.c		\
	compositor/cobiwm-window-actor-private.h	\
	compositor/cobiwm-window-group.c		\
	compositor/cobiwm-window-group.h		\
	compositor/cobiwm-window-shape.c		\
	compositor/region-utils.c		\
	compositor/region-utils.h		\
	include/compositor.h			\
	include/cobiwm-background.h			\
	include/cobiwm-background-actor.h		\
	include/cobiwm-background-image.h		\
	include/cobiwm-background-group.h		\
	include/cobiwm-plugin.h			\
	include/cobiwm-shadow-factory.h		\
	include/cobiwm-window-actor.h		\
	include/cobiwm-window-shape.h		\
	include/compositor-cobiwm.h 		\
	core/constraints.c			\
	core/constraints.h			\
	core/core.c				\
	core/delete.c				\
	core/display.c				\
	core/display-private.h			\
	include/display.h				\
	core/edge-resistance.c			\
	core/edge-resistance.h			\
	core/events.c				\
	core/events.h				\
	core/errors.c				\
	include/errors.h				\
	core/frame.c				\
	core/frame.h				\
	core/cobiwm-gesture-tracker.c		\
	core/cobiwm-gesture-tracker-private.h	\
	core/keybindings.c			\
	core/keybindings-private.h		\
	core/main.c				\
	core/place.c				\
	core/place.h				\
	core/prefs.c				\
	include/prefs.h				\
	core/screen.c				\
	core/screen-private.h			\
	include/screen.h				\
	core/startup-notification.c		\
	core/startup-notification-private.h	\
	include/types.h				\
	core/restart.c				\
	core/stack.c				\
	core/stack.h				\
	core/stack-tracker.c			\
	core/stack-tracker.h			\
	core/util.c				\
	include/util.h				\
	core/util-private.h			\
	core/window.c				\
	core/window-private.h			\
	include/window.h				\
	core/workspace.c			\
	core/workspace-private.h		\
	include/common.h				\
	core/core.h				\
	ui/ui.h					\
	ui/frames.c				\
	ui/frames.h				\
	ui/theme.c				\
	include/theme.h				\
	ui/theme-private.h			\
	ui/ui.c					\
	x11/atomnames.h				\
	x11/events.c				\
	x11/events.h				\
	x11/group-private.h			\
	x11/group-props.c			\
	x11/group-props.h			\
	x11/group.c				\
	include/group.h				\
	x11/iconcache.c				\
	x11/iconcache.h				\
	x11/session.c				\
	x11/session.h				\
	x11/window-props.c			\
	x11/window-props.h			\
	x11/window-x11.c			\
	x11/window-x11.h			\
	x11/window-x11-private.h		\
	x11/xprops.c				\
	x11/xprops.h				\
	x11/cobiwm-Xatomtype.h			\
	$(NULL)

if HAVE_WAYLAND
cobiwm_SOURCES +=				\
	compositor/cobiwm-surface-actor-wayland.c	\
	compositor/cobiwm-surface-actor-wayland.h	\
	wayland/cobiwm-wayland.c			\
	wayland/cobiwm-wayland.h			\
	wayland/cobiwm-wayland-private.h		\
	wayland/cobiwm-xwayland.c			\
	wayland/cobiwm-xwayland.h			\
	wayland/cobiwm-xwayland-selection.c	\
	wayland/cobiwm-xwayland-selection-private.h	\
	wayland/cobiwm-xwayland-private.h		\
	wayland/cobiwm-wayland-buffer.c      	\
	wayland/cobiwm-wayland-buffer.h      	\
	wayland/cobiwm-wayland-region.c      	\
	wayland/cobiwm-wayland-region.h      	\
	wayland/cobiwm-wayland-data-device.c      \
	wayland/cobiwm-wayland-data-device.h      \
	wayland/cobiwm-wayland-data-device-private.h	\
	wayland/cobiwm-wayland-pointer-gestures.c	\
	wayland/cobiwm-wayland-pointer-gestures.h	\
	wayland/cobiwm-wayland-pointer-gesture-swipe.c	\
	wayland/cobiwm-wayland-pointer-gesture-swipe.h	\
	wayland/cobiwm-wayland-pointer-gesture-pinch.c	\
	wayland/cobiwm-wayland-pointer-gesture-pinch.h	\
	wayland/cobiwm-wayland-keyboard.c		\
	wayland/cobiwm-wayland-keyboard.h		\
	wayland/cobiwm-wayland-pointer.c		\
	wayland/cobiwm-wayland-pointer.h		\
	wayland/cobiwm-wayland-pointer-constraints.c	\
	wayland/cobiwm-wayland-pointer-constraints.h	\
	wayland/cobiwm-pointer-lock-wayland.c		\
	wayland/cobiwm-pointer-lock-wayland.h		\
	wayland/cobiwm-pointer-confinement-wayland.c	\
	wayland/cobiwm-pointer-confinement-wayland.h	\
	wayland/cobiwm-wayland-popup.c		\
	wayland/cobiwm-wayland-popup.h		\
	wayland/cobiwm-wayland-seat.c		\
	wayland/cobiwm-wayland-seat.h		\
	wayland/cobiwm-wayland-touch.c		\
	wayland/cobiwm-wayland-touch.h		\
	wayland/cobiwm-wayland-surface.c		\
	wayland/cobiwm-wayland-surface.h		\
	wayland/cobiwm-wayland-types.h		\
	wayland/cobiwm-wayland-versions.h		\
	wayland/cobiwm-wayland-outputs.c		\
	wayland/cobiwm-wayland-outputs.h		\
	wayland/cobiwm-window-wayland.c		\
	wayland/cobiwm-window-wayland.h		\
	$(NULL)
endif

if HAVE_NATIVE_BACKEND
cobiwm_SOURCES +=					\
	backends/native/cobiwm-backend-native.c		\
	backends/native/cobiwm-backend-native.h		\
	backends/native/cobiwm-backend-native-private.h	\
	backends/native/cobiwm-barrier-native.c		\
	backends/native/cobiwm-barrier-native.h		\
	backends/native/cobiwm-cursor-renderer-native.c	\
	backends/native/cobiwm-cursor-renderer-native.h	\
	backends/native/cobiwm-idle-monitor-native.c	\
	backends/native/cobiwm-idle-monitor-native.h	\
	backends/native/cobiwm-monitor-manager-kms.c	\
	backends/native/cobiwm-monitor-manager-kms.h	\
	backends/native/cobiwm-launcher.c			\
	backends/native/cobiwm-launcher.h			\
	backends/native/dbus-utils.c			\
	backends/native/dbus-utils.h			\
	$(NULL)
endif

nodist_cobiwm_SOURCES = $(cobiwm_built_sources)

# Headers installed for plugins; introspected information will
# be extracted into Cobiwm-<version>.gir
cobiwm_headers =			\
	include/barrier.h				\
	include/boxes.h				\
	include/common.h				\
	include/compositor-cobiwm.h		\
	include/compositor.h			\
	include/display.h				\
	include/errors.h				\
	include/group.h				\
	include/keybindings.h			\
	include/main.h				\
	include/cobiwm-backend.h			\
	include/cobiwm-background.h			\
	include/cobiwm-background-actor.h		\
	include/cobiwm-background-image.h		\
	include/cobiwm-background-group.h		\
	include/cobiwm-cursor-tracker.h		\
	include/cobiwm-idle-monitor.h		\
	include/cobiwm-monitor-manager.h		\
	include/cobiwm-shaped-texture.h		\
	include/cobiwm-shadow-factory.h		\
	include/cobiwm-window-actor.h		\
	include/cobiwm-window-shape.h		\
	include/prefs.h				\
	include/screen.h				\
	include/theme.h				\
	include/types.h				\
	include/util.h				\
	include/window.h				\
	include/workspace.h			\
	$(NULL)

cobiwm_built_headers =		\
	include/cobiwm-version.h			\
	include/cobiwm-enum-types.h			\
	$(NULL)

cobiwm_base_headers =			\
	$(cobiwm_headers)		\
	$(cobiwm_built_headers)

bin_PROGRAMS=cobiwm
noinst_PROGRAMS=

cobiwm_LDFLAGS = -no-undefined -export-symbols-regex "^(cobiwm|ag)_.*"
cobiwm_LDADD  = $(COBIWM_LIBS) $(COBIWM_NATIVE_BACKEND_LIBS)

libexec_PROGRAMS = cobiwm-restart-helper
cobiwm_restart_helper_SOURCES = core/restart-helper.c
cobiwm_restart_helper_LDADD = $(COBIWM_LIBS)

dbus_idle_built_sources = cobiwm-dbus-idle-monitor.c cobiwm-dbus-idle-monitor.h

CLEANFILES =					\
	$(cobiwm_built_sources)			\
	$(typelib_DATA)				\
	$(gir_DATA)

DISTCLEANFILES = 				\
	$(cobiwm_built_headers)

EXTRA_DIST +=					\
	$(wayland_protocols)			\
	cobiwm-enum-types.h.in			\
	cobiwm-enum-types.c.in			\
	org.freedesktop.login1.xml		\
	org.Cobiwm.DisplayConfig.xml	\
	org.Cobiwm.IdleMonitor.xml	\
	$(NULL)

BUILT_SOURCES =					\
	$(cobiwm_built_sources)

include/cobiwm-enum-types.h: $(cobiwm_headers) cobiwm-enum-types.h.in Makefile
	$(AM_V_GEN) ( cd $(srcdir) && \
	  $(GLIB_MKENUMS) \
	    --template cobiwm-enum-types.h.in \
	  $(cobiwm_headers) ) >> xgen-teth && \
	cp xgen-teth include/cobiwm-enum-types.h && \
	rm -f xgen-teth

core/cobiwm-enum-types.c: include/cobiwm-enum-types.h cobiwm-enum-types.c.in
	  $(AM_V_GEN) ( cd $(srcdir) && \
	  $(GLIB_MKENUMS) \
	    --template cobiwm-enum-types.c.in \
	  $(cobiwm_headers) ) >> xgen-tetc && \
	cp xgen-tetc core/cobiwm-enum-types.c && \
	rm -f xgen-tetc

dbus_display_config_built_sources = cobiwm-dbus-display-config.c cobiwm-dbus-display-config.h

$(dbus_display_config_built_sources) : Makefile.am org.Cobiwm.DisplayConfig.xml
	$(AM_V_GEN)gdbus-codegen							\
		--interface-prefix org.Cobiwm					\
		--c-namespace CobiwmDBus							\
		--generate-c-code cobiwm-dbus-display-config				\
		$(srcdir)/org.Cobiwm.DisplayConfig.xml

$(dbus_idle_built_sources) : Makefile.am org.Cobiwm.IdleMonitor.xml
	$(AM_V_GEN)gdbus-codegen							\
		--interface-prefix org.Cobiwm					\
		--c-namespace CobiwmDBus							\
		--generate-c-code cobiwm-dbus-idle-monitor				\
		--c-generate-object-manager						\
		$(srcdir)/org.Cobiwm.IdleMonitor.xml

dbus_login1_built_sources = cobiwm-dbus-login1.c cobiwm-dbus-login1.h

$(dbus_login1_built_sources) : Makefile.am org.freedesktop.login1.xml
	$(AM_V_GEN)gdbus-codegen							\
		--interface-prefix org.freedesktop.login1				\
		--c-namespace Login1							\
		--generate-c-code cobiwm-dbus-login1					\
		$(srcdir)/org.freedesktop.login1.xml

.SECONDEXPANSION:

define protostability
$(shell echo $1 | sed 's/.*\(\<unstable\>\|\<stable\>\).*/\1/')
endef

define protoname
$(shell echo $1 | sed 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
%-server-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) server-header < $< > $@
%-protocol.c : $(srcdir)/wayland/protocol/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
%-server-protocol.h : $(srcdir)/wayland/protocol/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) server-header < $< > $@
