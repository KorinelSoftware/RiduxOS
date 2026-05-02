#!/bin/sh
set -eu

RIDUX_PROMPT="RiduxShell"
TERM="${TERM:-xterm}"
export TERM

if [ "${RIDUX_DESKTOP_DISABLE:-0}" != "1" ] && [ -x /usr/local/bin/ridux-desktop ]; then
    exec /usr/local/bin/ridux-desktop
fi

print_header() {
    clear 2>/dev/null || true
    echo "==============================="
    echo "      RiduxShell (Live)"
    echo "==============================="
    echo "Comandos: help, desktop, status, start, logs, browser <app>, sh, reboot, poweroff, exit"
    echo ""
}

print_status() {
    echo "[status] $(date)"
    if [ -f /var/db/ridux/live-ready ]; then
        echo "live-ready: yes"
    else
        echo "live-ready: no"
    fi

    if pgrep -f "Xorg.*:0" >/dev/null 2>&1; then
        echo "xorg: running (:0)"
    else
        echo "xorg: stopped"
    fi

    if [ -f /var/log/ridux-live-init.log ]; then
        echo ""
        echo "Ultimas lineas ridux-live-init.log:"
        tail -n 10 /var/log/ridux-live-init.log
    fi
}

start_session() {
    if [ -x /usr/local/bin/ridux-live-session ]; then
        env HOME=/tmp/ridux-root USER=root SHELL=/bin/sh \
            /usr/local/bin/startx /usr/local/bin/ridux-live-session -- :0 vt9 -nolisten tcp \
            >/var/log/ridux-startx.log 2>&1 || true
        return 0
    fi

    if [ -x /usr/local/bin/ridux-browser ]; then
        /usr/local/bin/ridux-browser firefox || true
        return 0
    fi

    echo "No hay interfaz disponible todavia."
    return 1
}

print_header

while :; do
    printf "%s> " "$RIDUX_PROMPT"
    IFS= read -r line || exit 0

    set -- $line
    cmd="${1:-}"

    case "$cmd" in
        "" )
            ;;
        help)
            echo "help                 muestra ayuda"
            echo "desktop              vuelve al escritorio Ridux"
            echo "status               estado de boot/Ridux"
            echo "start                lanza interfaz Ridux"
            echo "logs                 tail del log de inicio"
            echo "browser <app>        ridux-browser firefox|chromium|chrome"
            echo "sh                   shell base /bin/sh"
            echo "reboot               reinicia"
            echo "poweroff             apaga"
            echo "exit                 salir"
            ;;
        desktop)
            if [ -x /usr/local/bin/ridux-desktop ]; then
                exec /usr/local/bin/ridux-desktop
            fi
            echo "ridux-desktop no disponible."
            ;;
        status)
            print_status
            ;;
        start)
            start_session
            ;;
        logs)
            if [ -f /var/log/ridux-live-init.log ]; then
                tail -n 80 /var/log/ridux-live-init.log
            else
                echo "No existe /var/log/ridux-live-init.log todavia."
            fi
            ;;
        browser)
            shift || true
            app="${1:-firefox}"
            if [ -x /usr/local/bin/ridux-browser ]; then
                /usr/local/bin/ridux-browser "$app" || true
            else
                echo "ridux-browser no disponible."
            fi
            ;;
        sh)
            /bin/sh
            ;;
        reboot)
            /sbin/reboot
            ;;
        poweroff)
            /sbin/shutdown -p now
            ;;
        exit)
            exit 0
            ;;
        *)
            echo "Comando desconocido: $cmd"
            ;;
    esac
done
