# RiduxOS boot autorun commands.
#
# The primary desktop is Injury compositor over RiduxUI native GPU:
# /usr/bin/injury-compositor
#
# KDE Plasma and Wayfire are not part of the default desktop path.
# Qt/GTK compatibility, Mesa and the Ridux Wayland bridge stay available for apps.
#
# Manual commands inside RiduxOS:
# apps
# wm
# firefox
# chrome
# Vulkan/Venus probe is intentionally manual. Running it on every boot can
# consume the early serial/debug budget and compete with the primary desktop.
# r3 /usr/bin/ridux-vulkan-probe
