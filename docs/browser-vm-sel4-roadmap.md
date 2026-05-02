# Ridux Browser VM sobre seL4

Objetivo: mantener una ruta de fallback/oraculo para Firefox/Chromium reales
sin convertir Ridux en una distro Linux normal. El camino principal vuelve a
ser el ABI Linux nativo en `src/compat/*.c`; esta VM sirve para comparar,
debuggear y rescatar casos que todavia no funcionen nativamente.

## Arquitectura

```text
Hardware
  -> seL4
     -> Ridux UI / shell / window policy
     -> Linux guest aislado
        -> Firefox / Chromium reales
```

Ridux sigue siendo el sistema visible: ventanas, foco, permisos, input,
launcher y experiencia de usuario. Si el ABI nativo falla, Linux puede quedar
encapsulado como motor de compatibilidad temporal para apps enormes.

## ISO portable

La meta no es meter Firefox, Linux y drivers dentro de `kernel.c`. La meta es
que el ISO sea autocontenido:

- kernel/base seL4 o Ridux;
- servicios Ridux y shell visual;
- imagen Linux guest minima;
- rootfs con Firefox/Chromium;
- bridge de framebuffer/input/red;
- configuracion de arranque para que `browser-vm chromium` funcione como
  fallback despues de bootear en otra PC.

Asi Ridux no se vuelve una distro Linux normal: Linux viaja como componente de
compatibilidad dentro del ISO.

## Fase actual

1. `firefox`, `chrome` y `chromium` en Ridux apuntan al comando
   `browser-real`, que usa el ABI Linux nativo.
2. `browser-vm <firefox|chromium>` abre la ventana Ridux y registra el motor
   pedido para el fallback/oraculo seL4.
3. `browser realrun <path>` queda disponible para laboratorio directo de ELF
   Linux cuando queremos aislar un binario exacto.
4. `make sel4-browser-vm-deps` prepara herramientas locales sin sudo cuando
   se puede.
5. `make sel4-browser-vm-bootstrap` trae el VMM oficial de seL4/CAmkES.
6. `make sel4-browser-vm-build` compila el Linux guest minimo.
7. `make sel4-browser-vm-stage` copia las imagenes seL4 a `build/browser-vm`.
8. `make sel4-browser-vm-iso` genera una ISO Ridux con entrada GRUB opcional
   para el prototipo seL4 Browser VM.

En WSL, el source tree de seL4 se descarga por defecto en
`/var/tmp/ridux-sel4` para evitar problemas de permisos/renames sobre
`/mnt/c`. Tambien se crean herramientas locales (`bin/repo`, `bin/ninja`,
`bin/xmllint`, `bin/stack`) y dependencias Python locales (`venv/` si existe
`python3-venv`, o `pyuser/` con `get-pip.py` si no), sin sudo. Los artefactos
finales se copian despues al `build/` del repo para entrar al ISO.

## Siguiente etapa tecnica

- Arrancar el Linux guest minimo sobre seL4 en x86_64.
- Definir un canal Ridux <-> guest:
  - framebuffer compartido para pixels del navegador;
  - cola de input para mouse/teclado;
  - mensajes de control para abrir URL, resize, clipboard y cierre.
- Reemplazar el placeholder visual de `app_draw_browser()` por pixels reales
  del guest.
- Agregar rootfs Linux con Firefox ESR/Chromium y sesion fullscreen.
- Integrar ese rootfs en las imagenes seL4 staged y luego en el ISO portable.

## Donde entran NetBSD Rump, HelenOS y Phoenix-RTOS

- NetBSD Rump: fase posterior para filesystem/red/servicios aislados, no para
  desbloquear el primer browser.
- HelenOS: referencia de arquitectura por componentes.
- Phoenix-RTOS: referencia para ports POSIX livianos.

La regla es no fusionar kernels: cada pieza entra como componente con frontera
clara.

## Fuentes primarias

- seL4 CAmkES VMM: https://docs.sel4.systems/projects/camkes-vm/
- seL4 licensing: https://sel4.org/Legal/license.html
- NetBSD rump kernels: https://www.netbsd.org/docs/rump/sysproxy.html
