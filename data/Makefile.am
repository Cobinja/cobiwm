desktopfiles_in_files = \
	cobiwm.desktop.in
desktopfilesdir = $(datadir)/applications
desktopfiles_DATA = $(desktopfiles_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

@INTLTOOL_XML_NOMERGE_RULE@

xml_in_files = \
        50-cobiwm-navigation.xml.in \
        50-cobiwm-system.xml.in \
        50-cobiwm-windows.xml.in
xmldir = $(GNOME_KEYBINDINGS_KEYSDIR)
xml_DATA = $(xml_in_files:.xml.in=.xml)

gschema_in_files = \
	org.cobiwm.gschema.xml.in	\
	org.cobiwm.wayland.gschema.xml.in \
	org.cobiwm.keybindings.gschema.xml.in
gsettings_SCHEMAS = $(gschema_in_files:.xml.in=.xml)

%.gschema.xml.in: %.gschema.xml.in.in Makefile
	$(AM_V_GEN) $(SED) -e 's^\@datadir\@^$(datadir)^g' -e 's^\@GETTEXT_PACKAGE\@^$(GETTEXT_PACKAGE)^g' < $< > $@

@GSETTINGS_RULES@

CLEANFILES = \
	$(desktopfiles_DATA) \
	$(gsettings_SCHEMAS) \
	$(xml_DATA)

EXTRA_DIST = \
	$(desktopfiles_in_files) \
	$(gschema_in_files) \
	$(xml_in_files)
