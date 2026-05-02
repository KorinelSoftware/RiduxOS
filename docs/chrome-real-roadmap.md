# RiduxOS: roadmap para Chrome real (kernel propio)

Objetivo: ejecutar Chromium/Chrome real (no mock UI) en RiduxOS con runtime
suficiente para navegar de verdad.

## Se puede usar codigo open source "copiado"?

Si, y es la forma correcta de acelerar. La regla no es "no copiar", sino:

- copiar desde upstream oficial,
- respetar licencias,
- mantener trazabilidad (`repo/ref/commit`),
- integrar via wrappers Ridux en lugar de mezclar todo en `kernel.c`.

## Gap real actual (resumen)

- ABI Linux: avanzado pero aun parcial para cargas Chromium modernas.
- Red/TLS: faltan piezas end-to-end robustas para web real.
- Sandbox/zygote/process model: incompleto para el path "Chrome completo".
- Display stack (X11/Wayland/DBus real): hoy mayormente virtual/stub.

## Etapas sugeridas (orden recomendado)

1. Etapa A: Chromium real en modo headless controlado.
2. Etapa B: red real + TLS real + resolucion robusta.
3. Etapa C: sandbox Linux (userns/seccomp) con pruebas por proceso.
4. Etapa D: display stack real (Wayland o X11 minimal viable).
5. Etapa E: GPU process y estabilidad de navegacion diaria.

## Upstream sugerido para acelerar

Compatibilidad Linux:
- FreeBSD Linuxulator para semantica kernel-level.
- Fuchsia Starnix y gVisor como oraculos de syscalls y pruebas.
- Ver `docs/linux-compat-upstreams.md`.

Perfil network:
- lwIP (BSD-3-Clause) para TCP/IP portable.
- mbedTLS (Apache-2.0) para TLS.

Perfil display:
- wayland (MIT) protocolo/compositor base.
- libxkbcommon (MIT) teclado/layout.
- pixman (MIT) compositing software.

## Criterio de "done" por etapa

- A: `browser realrun /opt/chromium/chrome` levanta proceso real y no cae en
  loader/syscall early-fail.
- B: request HTTPS real funciona con certificados validos.
- C: renderer/utility process corren con sandbox habilitado.
- D: ventana de Chromium renderiza y acepta input real.
- E: navegacion estable en 10+ sitios comunes sin crash inmediato.

## Comandos agregados al repo para arrancar

- `make vendor-upstream` -> trae upstream pinneado.
- `make vendor-check` -> control de licencias rapido.

## Fuentes primarias usadas

- Chromium Linux sandboxing:
  https://chromium.googlesource.com/chromium/src/+/127.0.6533.88/docs/linux/sandboxing.md
- Chromium Linux build instructions:
  https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/linux/build_instructions.md
- Linux sandbox README (Chromium):
  https://chromium.googlesource.com/chromium/src/+/refs/tags/138.0.7166.1/sandbox/linux/README.md
- musl overview/license context:
  https://musl.libc.org/
- lwIP overview/license:
  https://www.nongnu.org/lwip/2_0_x/index.html
