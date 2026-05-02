# Requirements Document

## Introduction

Esta feature formaliza la integración de FreeBSD como base de kernel para RiduxOS (Track B del roadmap).
El objetivo es reemplazar el kernel nativo de Ridux como capa de ejecución de aplicaciones reales,
manteniendo el código existente (`src/kernel.c`, `src/compat/*.c`) como Track A (kernel nativo),
mientras Track B usa el kernel de FreeBSD con un bridge de runtime Ridux encima.

La arquitectura resultante tiene tres capas:
1. `ridux_kmod` — módulo de kernel cargable en FreeBSD que expone hooks para el runtime Ridux.
2. `ridux_compat` — capa ABI que reutiliza la lógica de `compat3..compat8` enrutando primitivas a APIs de FreeBSD.
3. `ridux_userland` — shell, diagnósticos y launchers de browsers de Ridux corriendo sobre FreeBSD userland.

## Glossary

- **RiduxOS**: El sistema operativo hobby cuyo kernel nativo reside en `src/kernel.c`.
- **FreeBSD_Kernel**: El kernel de FreeBSD stable/14 usado como base en Track B.
- **ridux_kmod**: Módulo de kernel cargable (`.ko`) que actúa como bridge entre FreeBSD y el runtime Ridux.
- **ridux_compat**: Capa ABI en userspace/kernel que traduce las primitivas de `compat3..compat8` a llamadas FreeBSD nativas.
- **ridux_userland**: Conjunto de herramientas de usuario de Ridux (shell, diagnósticos, `ridux-browser`, `ridux-app`).
- **Compat_Snapshot**: Copia exportada de `src/kernel.c` y `src/compat/*.c` hacia `freebsd/ridux-runtime/compat_snapshot`.
- **Linuxulator**: Capa de compatibilidad Linux integrada en FreeBSD que permite ejecutar binarios ELF Linux.
- **ELF64_Loader**: Componente que carga y ejecuta binarios ELF de 64 bits.
- **ABI_Bridge**: Interfaz que traduce syscalls y primitivas entre el runtime Ridux y las APIs del FreeBSD_Kernel.
- **Browser_ISO**: Imagen ISO de instalación de FreeBSD personalizada con provisioning automático de browsers.
- **Live_ISO**: Imagen ISO de arranque directo sin instalación.
- **Provisioner**: Script de primer arranque que instala y configura el entorno de escritorio y browsers.
- **Track_A**: Rama de desarrollo del kernel nativo Ridux (`src/kernel.c`), mantenida en paralelo.
- **Track_B**: Rama de desarrollo basada en FreeBSD_Kernel + runtime Ridux bridge.

---

## Requirements

### Requirement 1: Módulo de kernel Ridux en FreeBSD (H1)

**User Story:** Como desarrollador de RiduxOS, quiero cargar un módulo Ridux dentro del FreeBSD_Kernel, para que el runtime Ridux tenga un punto de entrada en el espacio del kernel sin reemplazar el kernel de FreeBSD.

#### Acceptance Criteria

1. THE ridux_kmod SHALL compilar como módulo `.ko` compatible con FreeBSD stable/14 usando el build system de FreeBSD (`make -C sys/modules`).
2. WHEN el operador ejecuta `kldload ridux_kmod.ko`, THE FreeBSD_Kernel SHALL cargar el módulo sin errores de enlazado ni de versión de ABI.
3. WHEN ridux_kmod es cargado, THE ridux_kmod SHALL registrar al menos un hook de tracing y un hook de estado de runtime en el FreeBSD_Kernel.
4. WHEN el operador ejecuta `kldunload ridux_kmod`, THE FreeBSD_Kernel SHALL descargar el módulo liberando todos los recursos registrados sin producir kernel panic.
5. IF ridux_kmod falla al cargar por incompatibilidad de versión, THEN THE ridux_kmod SHALL retornar el código de error `ENOEXEC` y registrar el motivo en el log del kernel (`dmesg`).
6. THE ridux_kmod SHALL exponer una interfaz `sysctl` bajo `kern.ridux.*` para consultar el estado del módulo en tiempo de ejecución.

---

### Requirement 2: Exportación del Compat Snapshot (prerequisito de integración)

**User Story:** Como desarrollador de RiduxOS, quiero exportar el código de `src/kernel.c` y `src/compat/*.c` al árbol de FreeBSD, para que ridux_compat pueda reutilizar la lógica existente sin duplicar manualmente el código.

#### Acceptance Criteria

1. WHEN el operador ejecuta `make freebsd-compat-snapshot`, THE Compat_Snapshot SHALL copiar `src/kernel.c` y todos los archivos `src/compat/*.c` hacia `freebsd/ridux-runtime/compat_snapshot/` preservando los nombres de archivo originales.
2. THE Compat_Snapshot SHALL incluir un archivo de manifiesto `snapshot.manifest` con la fecha de exportación, el hash SHA-256 de cada archivo copiado, y la rama de git de origen.
3. IF un archivo fuente no existe en el momento de la exportación, THEN THE Compat_Snapshot SHALL abortar la operación y reportar el archivo faltante sin modificar el directorio destino.
4. WHEN el Compat_Snapshot es generado, THE Compat_Snapshot SHALL ser idempotente: ejecutar `make freebsd-compat-snapshot` dos veces consecutivas sin cambios en las fuentes SHALL producir el mismo contenido en el directorio destino.

---

### Requirement 3: Capa ABI ridux_compat (H2)

**User Story:** Como desarrollador de RiduxOS, quiero que los comandos de diagnóstico y compat de Ridux corran sobre FreeBSD, para que la funcionalidad existente de `compat3..compat8` esté disponible sin reescritura completa.

#### Acceptance Criteria

1. THE ridux_compat SHALL enrutar las primitivas de gestión de procesos de `compat3..compat8` a las syscalls equivalentes de FreeBSD (`fork`, `execve`, `wait4`, `kill`).
2. THE ridux_compat SHALL enrutar las primitivas de gestión de memoria virtual de `compat3..compat8` a las APIs de VM de FreeBSD (`mmap`, `munmap`, `mprotect`).
3. THE ridux_compat SHALL enrutar las primitivas de descriptores de archivo de `compat3..compat8` a las syscalls de fd de FreeBSD (`open`, `close`, `read`, `write`, `ioctl`).
4. THE ridux_compat SHALL enrutar las primitivas de red de `compat3..compat8` a las syscalls de socket de FreeBSD (`socket`, `bind`, `connect`, `send`, `recv`).
5. WHEN un comando de diagnóstico Ridux (`abi6`, `dynlink`, `mmaps`) es ejecutado sobre FreeBSD, THE ridux_compat SHALL completar la operación retornando el mismo código de salida que retornaría sobre el kernel nativo Ridux para entradas equivalentes.
6. IF una primitiva de `compat3..compat8` no tiene equivalente directo en FreeBSD, THEN THE ABI_Bridge SHALL registrar la primitiva no mapeada en un log de compatibilidad y retornar `ENOSYS`.
7. THE ridux_compat SHALL exponer la función `ridux_compat_version()` que retorna la versión del snapshot de compat integrado, verificable mediante `sysctl kern.ridux.compat_version`.

---

### Requirement 4: ELF64 Loader estable sobre FreeBSD (H3)

**User Story:** Como desarrollador de RiduxOS, quiero que el ELF64_Loader de Ridux opere de forma estable sobre FreeBSD, para que los binarios ELF64 del userland Ridux se carguen y ejecuten correctamente.

#### Acceptance Criteria

1. WHEN el ELF64_Loader recibe un binario ELF64 válido, THE ELF64_Loader SHALL mapear los segmentos PT_LOAD en las direcciones virtuales especificadas en el encabezado ELF usando `mmap` de FreeBSD.
2. WHEN el ELF64_Loader recibe un binario ELF64 con secciones de reubicación dinámica, THE ELF64_Loader SHALL resolver los símbolos dinámicos contra las bibliotecas compartidas del sistema FreeBSD.
3. IF el ELF64_Loader recibe un archivo que no es un binario ELF64 válido (magic bytes incorrectos o clase ELF incorrecta), THEN THE ELF64_Loader SHALL retornar `ENOEXEC` sin mapear memoria.
4. IF el ELF64_Loader no puede resolver un símbolo dinámico requerido, THEN THE ELF64_Loader SHALL reportar el nombre del símbolo no resuelto y retornar `ENOENT`.
5. FOR ALL binarios ELF64 válidos cargados por el ELF64_Loader, THE ELF64_Loader SHALL liberar todos los mappings de memoria cuando el proceso termina (propiedad de no-leak).
6. THE ELF64_Loader SHALL soportar binarios ELF64 con hasta 64 segmentos PT_LOAD sin degradación de rendimiento observable.

---

### Requirement 5: Compatibilidad con Firefox y Chromium nativos de FreeBSD (H4)

**User Story:** Como usuario de RiduxOS, quiero ejecutar Firefox y Chromium nativos de FreeBSD desde el entorno Ridux, para que pueda usar un navegador real sin emulación adicional.

#### Acceptance Criteria

1. WHEN el usuario ejecuta `ridux-browser firefox`, THE ridux_userland SHALL lanzar el binario `firefox` instalado en el sistema FreeBSD y presentar la ventana en el servidor X activo.
2. WHEN el usuario ejecuta `ridux-browser chromium`, THE ridux_userland SHALL lanzar el binario `chromium` instalado en el sistema FreeBSD y presentar la ventana en el servidor X activo.
3. WHILE Firefox o Chromium están en ejecución, THE ridux_userland SHALL mantener el proceso activo sin interferir con el scheduler de FreeBSD.
4. IF el binario de Firefox o Chromium no está instalado en el sistema, THEN THE ridux_userland SHALL mostrar un mensaje de error indicando el paquete faltante (`pkg install firefox` o `pkg install chromium`) y retornar código de salida 1.
5. IF el servidor X no está disponible cuando se lanza un browser, THEN THE ridux_userland SHALL reportar el error `DISPLAY not set or X server unavailable` y retornar código de salida 1.
6. THE ridux_userland SHALL proveer el comando `ridux-app list` que enumera los browsers y aplicaciones disponibles en el sistema FreeBSD.

---

### Requirement 6: Compatibilidad con Linux Chrome via Linuxulator (H5)

**User Story:** Como usuario de RiduxOS, quiero ejecutar Google Chrome para Linux mediante el Linuxulator de FreeBSD, para que pueda acceder a aplicaciones Linux que no tienen port nativo en FreeBSD.

#### Acceptance Criteria

1. WHEN el operador ejecuta `ridux-app install linux-chrome`, THE ridux_userland SHALL instalar `linux_base-rl9` y `linux-chrome` usando `pkg` de FreeBSD y habilitar el servicio `linux` en `/etc/rc.conf`.
2. WHEN el usuario ejecuta `ridux-browser linux-chrome`, THE ridux_userland SHALL lanzar Google Chrome a través del Linuxulator de FreeBSD con las variables de entorno necesarias para el servidor X.
3. IF el Linuxulator no está habilitado en el sistema, THEN THE ridux_userland SHALL habilitar el módulo `linux64` mediante `kldload linux64` antes de intentar lanzar el binario Linux.
4. IF `linux_base-rl9` no está instalado, THEN THE ridux_userland SHALL abortar el lanzamiento, mostrar instrucciones de instalación, y retornar código de salida 1.
5. WHERE el Linuxulator está habilitado, THE ridux_userland SHALL exponer el alias `browser run` como equivalente a `ridux-browser linux-chrome` para compatibilidad con el workflow existente de `compat*.c`.

---

### Requirement 7: Build y provisioning del Browser ISO

**User Story:** Como desarrollador de RiduxOS, quiero construir una ISO de FreeBSD con provisioning automático de escritorio y browsers, para que el entorno Ridux+FreeBSD sea reproducible sin configuración manual.

#### Acceptance Criteria

1. WHEN el operador ejecuta `make freebsd-browser-iso`, THE Provisioner SHALL producir una imagen ISO booteable en `build/RiduxOS-FreeBSD-Browser.iso` que incluye un instalador de FreeBSD con configuración desatendida.
2. WHEN la ISO instalada arranca por primera vez, THE Provisioner SHALL instalar automáticamente `xorg`, `xinit`, `sdl2`, `firefox`, y `chromium` sin intervención del usuario.
3. WHEN la ISO instalada arranca por primera vez, THE Provisioner SHALL configurar autologin en `ttyv0` e iniciar el entorno Ridux UI directamente sin display manager.
4. WHEN el operador ejecuta `make freebsd-browser-iso-fast`, THE Provisioner SHALL producir la misma ISO pero omitiendo la instalación de `linux_base-rl9` y `linux-chrome` en el primer arranque.
5. IF la descarga de un paquete falla durante el provisioning del primer arranque, THEN THE Provisioner SHALL registrar el error en `/var/log/ridux-provision.log` y continuar con los paquetes restantes.
6. THE Provisioner SHALL instalar `ridux_kmod`, `ridux_compat`, y `ridux_userland` como parte del provisioning del primer arranque, verificando que `kldload ridux_kmod.ko` retorna 0.
7. WHEN el operador ejecuta `make freebsd-live-iso`, THE Provisioner SHALL producir una imagen ISO de arranque directo en `build/RiduxOS-FreeBSD-Live.iso` que no requiere instalación en disco.

---

### Requirement 8: Coexistencia de Track A y Track B

**User Story:** Como desarrollador de RiduxOS, quiero que el desarrollo de Track B no rompa ni descarte Track A, para que el kernel nativo Ridux siga siendo compilable y ejecutable de forma independiente.

#### Acceptance Criteria

1. THE RiduxOS build system SHALL compilar `src/kernel.c` y todos los `src/compat/*.c` en la ISO nativa (`make all`) sin depender de ningún artefacto del árbol de FreeBSD.
2. WHEN el operador ejecuta `make all`, THE RiduxOS build system SHALL producir `build/RiduxOS-Unix.iso` usando exclusivamente el toolchain nativo (`gcc`, `ld`, `grub-mkrescue`) sin requerir el FreeBSD_Kernel.
3. THE RiduxOS build system SHALL mantener los targets `freebsd-*` y los targets nativos (`all`, `clean`, `run`) como grupos de targets independientes sin dependencias cruzadas.
4. IF un cambio en `src/compat/*.c` es introducido, THEN THE Compat_Snapshot SHALL requerir una re-exportación explícita mediante `make freebsd-compat-snapshot` antes de que el cambio sea visible en Track B.

---

### Requirement 9: Parser y serialización del manifiesto de snapshot

**User Story:** Como desarrollador de RiduxOS, quiero que el manifiesto del Compat_Snapshot sea parseable y serializable de forma confiable, para que las herramientas de build puedan verificar la integridad del snapshot.

#### Acceptance Criteria

1. WHEN el Compat_Snapshot genera `snapshot.manifest`, THE Compat_Snapshot SHALL escribir el manifiesto en formato JSON con los campos `date`, `git_branch`, `git_commit`, y un array `files` con `name` y `sha256` por cada archivo.
2. WHEN una herramienta de verificación lee `snapshot.manifest`, THE Compat_Snapshot SHALL parsear el JSON y verificar que el SHA-256 de cada archivo listado coincide con el archivo en disco.
3. IF `snapshot.manifest` contiene JSON malformado, THEN THE Compat_Snapshot SHALL reportar el error de parseo con la línea y columna del problema y retornar código de salida 1.
4. FOR ALL manifiestos válidos generados por el Compat_Snapshot, parsear el JSON y serializarlo de nuevo SHALL producir un documento JSON semánticamente equivalente (propiedad round-trip).
