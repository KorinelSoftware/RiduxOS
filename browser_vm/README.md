# Browser VM payload

Este directorio contiene piezas que van dentro del Linux guest que corre sobre
seL4/CAmkES. La ventana la sigue manejando Ridux; el guest solo provee el motor
real de Firefox/Chromium.

Primer objetivo del payload:

- arrancar una sesion grafica minima;
- lanzar Firefox o Chromium en `about:blank`;
- exponer pixels/input al bridge Ridux en la fase siguiente.

Overlay inicial:

- `linux-guest-overlay/usr/local/bin/ridux-browser-session`

Ese script todavia no esta empacado automaticamente en el rootfs de seL4; se
deja listo para la siguiente etapa, cuando cambiemos el rootfs minimo por uno
con Firefox/Chromium.
