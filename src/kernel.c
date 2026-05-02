/*
 * Entrada principal del kernel.
 *
 * Estos archivos siguen entrando en una sola unidad de compilacion porque
 * todavia hay mucho estado compartido, pero ya no estan tirados en un unico
 * kernel.c gigante ni usan archivos de inclusion raros.
 */

#include "kernel/prelude.c"
#include "kernel/core_runtime.c"
#include "kernel/vfs_sched.c"
#include "kernel/wm_apps_ui.c"
#include "kernel/shell_input.c"
#include "kernel/boot_main.c"
