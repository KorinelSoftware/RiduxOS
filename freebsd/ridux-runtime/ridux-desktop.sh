#!/bin/sh
set -eu

TERM="${TERM:-xterm}"
export TERM

ROWS=25
COLS=80
theme="dark"
focus="system"
start_open=0
quick_open=0
message="Ridux Desktop listo."
old_stty=""

open_system=1
open_files=1
open_terminal=0
open_settings=1
open_about=0

esc() {
    printf '\033[%s' "$1"
}

save_tty() {
    if [ -t 0 ]; then
        old_stty="$(stty -g 2>/dev/null || true)"
    fi
}

set_desktop_tty() {
    if [ -t 0 ]; then
        stty -echo -icanon min 0 time 10 2>/dev/null || true
    fi
}

restore_tty() {
    if [ -n "$old_stty" ]; then
        stty "$old_stty" 2>/dev/null || true
    fi
}

cleanup() {
    restore_tty
    color_reset
    esc "?25h"
}

move_to() {
    printf '\033[%s;%sH' "$2" "$1"
}

color_reset() {
    esc "0m"
}

screen_size() {
    set -- $(stty size 2>/dev/null || echo "25 80")
    ROWS="${1:-25}"
    COLS="${2:-80}"
    [ "$ROWS" -lt 20 ] && ROWS=20
    [ "$COLS" -lt 70 ] && COLS=70
    return 0
}

repeat_char() {
    ch="$1"
    count="$2"
    i=0
    while [ "$i" -lt "$count" ]; do
        printf "%s" "$ch"
        i=$((i + 1))
    done
}

print_fit() {
    width="$1"
    shift
    text="$*"
    printf "%-${width}.${width}s" "$text"
}

draw_text() {
    x="$1"
    y="$2"
    shift 2
    move_to "$x" "$y"
    printf "%s" "$*"
}

theme_colors() {
    if [ "$theme" = "light" ]; then
        bg="47;30"
        panel="107;30"
        panel2="47;30"
        border="94"
        active="44;97"
        text="30"
        muted="90"
        accent="46;30"
        taskbar="107;30"
    else
        bg="40;37"
        panel="48;5;236;97"
        panel2="48;5;238;97"
        border="96"
        active="48;5;33;97"
        text="97"
        muted="90"
        accent="46;30"
        taskbar="48;5;235;97"
    fi
}

clear_desktop() {
    theme_colors
    esc "2J"
    esc "H"
    esc "${bg}m"
    y=1
    while [ "$y" -le "$ROWS" ]; do
        move_to 1 "$y"
        repeat_char " " "$COLS"
        y=$((y + 1))
    done
    color_reset
}

draw_wallpaper() {
    theme_colors
    esc "${bg}m"
    y=2
    while [ "$y" -lt $((ROWS - 3)) ]; do
        if [ $((y % 4)) -eq 0 ]; then
            move_to 4 "$y"
            esc "${muted}m"
            printf "Ridux"
        fi
        if [ $((y % 5)) -eq 0 ]; then
            x=$((COLS - 18))
            move_to "$x" "$y"
            esc "${muted}m"
            printf "RIDUX"
        fi
        y=$((y + 1))
    done
    color_reset
}

draw_box() {
    x="$1"
    y="$2"
    w="$3"
    h="$4"
    title="$5"
    is_active="$6"
    theme_colors

    [ "$w" -lt 12 ] && return
    [ "$h" -lt 5 ] && return

    if [ "$is_active" = "1" ]; then
        title_color="$active"
        line_color="$border"
    else
        title_color="$panel2"
        line_color="$muted"
    fi

    move_to "$x" "$y"
    esc "${line_color}m"
    printf "+"
    repeat_char "-" $((w - 2))
    printf "+"

    move_to $((x + 2)) "$y"
    esc "${title_color}m"
    print_fit $((w - 4)) " $title "

    row=1
    while [ "$row" -lt $((h - 1)) ]; do
        move_to "$x" $((y + row))
        esc "${line_color}m"
        printf "|"
        esc "${panel}m"
        repeat_char " " $((w - 2))
        esc "${line_color}m"
        printf "|"
        row=$((row + 1))
    done

    move_to "$x" $((y + h - 1))
    esc "${line_color}m"
    printf "+"
    repeat_char "-" $((w - 2))
    printf "+"
    color_reset
}

line_in_box() {
    x="$1"
    y="$2"
    w="$3"
    row="$4"
    shift 4
    theme_colors
    move_to $((x + 2)) $((y + row))
    esc "${panel}m"
    print_fit $((w - 4)) "$*"
    color_reset
}

window_active() {
    [ "$focus" = "$1" ] && echo 1 || echo 0
}

draw_system() {
    x=3
    y=3
    w=$((COLS / 2 - 4))
    h=9
    draw_box "$x" "$y" "$w" "$h" "Sistema" "$(window_active system)"
    line_in_box "$x" "$y" "$w" 2 "RiduxOS Live"
    line_in_box "$x" "$y" "$w" 3 "Kernel: FreeBSD + RIDUX"
    line_in_box "$x" "$y" "$w" 4 "Base: FreeBSD runtime"
    if [ -f /var/db/ridux/live-ready ]; then
        line_in_box "$x" "$y" "$w" 5 "Estado: listo"
    else
        line_in_box "$x" "$y" "$w" 5 "Estado: iniciando"
    fi
    line_in_box "$x" "$y" "$w" 7 "$message"
}

draw_files() {
    x=$((COLS / 2 + 1))
    y=3
    w=$((COLS / 2 - 3))
    h=10
    draw_box "$x" "$y" "$w" "$h" "Archivos" "$(window_active files)"
    line_in_box "$x" "$y" "$w" 2 "Este equipo"
    line_in_box "$x" "$y" "$w" 3 "  /boot/kernel/kernel"
    line_in_box "$x" "$y" "$w" 4 "  /usr/local/bin/ridux-desktop"
    line_in_box "$x" "$y" "$w" 5 "  /usr/local/bin/ridux-shell"
    line_in_box "$x" "$y" "$w" 6 "  /var/log/ridux-live-init.log"
    line_in_box "$x" "$y" "$w" 8 "Atajo: t abre consola base"
}

draw_settings() {
    x=6
    y=$((ROWS - 14))
    [ "$y" -lt 13 ] && y=13
    w=$((COLS - 12))
    h=8
    draw_box "$x" "$y" "$w" "$h" "Configuracion" "$(window_active settings)"
    line_in_box "$x" "$y" "$w" 2 "Tema actual: $theme"
    line_in_box "$x" "$y" "$w" 3 "s cambia tema claro/oscuro"
    line_in_box "$x" "$y" "$w" 4 "m abre Inicio, q abre controles rapidos"
    line_in_box "$x" "$y" "$w" 5 "n cambia foco, c cierra ventana"
    line_in_box "$x" "$y" "$w" 6 "r redibuja la interfaz"
}

draw_terminal() {
    x=8
    y=7
    w=$((COLS - 16))
    h=10
    draw_box "$x" "$y" "$w" "$h" "Terminal Ridux" "$(window_active terminal)"
    line_in_box "$x" "$y" "$w" 2 "RiduxShell integrado"
    line_in_box "$x" "$y" "$w" 3 "Comandos utiles:"
    line_in_box "$x" "$y" "$w" 4 "  t      abrir /bin/sh"
    line_in_box "$x" "$y" "$w" 5 "  l      ver log de arranque"
    line_in_box "$x" "$y" "$w" 6 "  x      salir al RiduxShell"
    line_in_box "$x" "$y" "$w" 8 "La interfaz es propia y corre sin instalacion."
}

draw_about() {
    x=10
    y=5
    w=$((COLS - 20))
    h=9
    draw_box "$x" "$y" "$w" "$h" "Acerca de Ridux" "$(window_active about)"
    line_in_box "$x" "$y" "$w" 2 "Ridux Desktop Preview"
    line_in_box "$x" "$y" "$w" 3 "Interfaz propia: Inicio, barra y ventanas."
    line_in_box "$x" "$y" "$w" 4 "Objetivo: estilo moderno sin instalador tedioso."
    line_in_box "$x" "$y" "$w" 6 "Base: FreeBSD + kernel RIDUX inyectable."
    line_in_box "$x" "$y" "$w" 7 "Shell: RiduxShell como modo rescate."
}

draw_taskbar() {
    theme_colors
    bar_y=$((ROWS - 2))
    move_to 1 "$bar_y"
    esc "${taskbar}m"
    repeat_char " " "$COLS"
    move_to 2 "$bar_y"
    esc "${accent}m"
    printf " RIDUX "

    dock="[1 Sistema] [2 Archivos] [3 Terminal] [4 Config] [5 Acerca]"
    dock_x=$(((COLS - ${#dock}) / 2))
    [ "$dock_x" -lt 10 ] && dock_x=10
    move_to "$dock_x" "$bar_y"
    esc "${taskbar}m"
    printf "%s" "$dock"

    clock="$(date '+%H:%M' 2>/dev/null || echo '--:--')"
    right="[q] $clock"
    move_to $((COLS - ${#right} - 1)) "$bar_y"
    printf "%s" "$right"

    move_to 1 "$ROWS"
    esc "${bg}m"
    repeat_char " " "$COLS"
    move_to 2 "$ROWS"
    esc "${text}m"
    print_fit $((COLS - 2)) "1-5 apps | m Inicio | q Controles | n Foco | c Cerrar | s Tema | t Shell | x RiduxShell"
    color_reset
}

draw_start_menu() {
    [ "$start_open" = "1" ] || return 0
    w=54
    h=13
    [ "$COLS" -lt 90 ] && w=$((COLS - 10))
    x=$(((COLS - w) / 2))
    y=$((ROWS - h - 4))
    [ "$y" -lt 2 ] && y=2
    draw_box "$x" "$y" "$w" "$h" "Inicio" "1"
    line_in_box "$x" "$y" "$w" 2 "Apps fijadas"
    line_in_box "$x" "$y" "$w" 4 "1 Sistema       2 Archivos"
    line_in_box "$x" "$y" "$w" 5 "3 Terminal      4 Configuracion"
    line_in_box "$x" "$y" "$w" 6 "5 Acerca"
    line_in_box "$x" "$y" "$w" 8 "Sistema"
    line_in_box "$x" "$y" "$w" 9 "l logs    b reiniciar"
    line_in_box "$x" "$y" "$w" 10 "o apagar  x RiduxShell"
}

draw_quick_panel() {
    [ "$quick_open" = "1" ] || return 0
    w=36
    h=10
    x=$((COLS - w - 3))
    y=$((ROWS - h - 4))
    [ "$x" -lt 2 ] && x=2
    [ "$y" -lt 2 ] && y=2
    draw_box "$x" "$y" "$w" "$h" "Controles" "1"
    line_in_box "$x" "$y" "$w" 2 "[on] Red        [on] Audio"
    line_in_box "$x" "$y" "$w" 3 "[on] Video      [--] Bateria"
    line_in_box "$x" "$y" "$w" 5 "Tema: $theme"
    line_in_box "$x" "$y" "$w" 6 "s cambia tema"
    line_in_box "$x" "$y" "$w" 8 "Enter cierra paneles"
}

draw_screen() {
    screen_size
    clear_desktop
    draw_wallpaper
    [ "$open_system" = "1" ] && draw_system
    [ "$open_files" = "1" ] && draw_files
    [ "$open_settings" = "1" ] && draw_settings
    [ "$open_terminal" = "1" ] && draw_terminal
    [ "$open_about" = "1" ] && draw_about
    draw_start_menu
    draw_quick_panel
    draw_taskbar
}

is_open() {
    eval "[ \"\${open_$1}\" = \"1\" ]"
}

open_app() {
    app="$1"
    eval "open_$app=1"
    focus="$app"
    start_open=0
    quick_open=0
    message="Ventana abierta: $app"
}

focus_next() {
    case "$focus" in
        system) order="files terminal settings about system" ;;
        files) order="terminal settings about system files" ;;
        terminal) order="settings about system files terminal" ;;
        settings) order="about system files terminal settings" ;;
        about) order="system files terminal settings about" ;;
        *) order="system files terminal settings about" ;;
    esac
    for app in $order; do
        if is_open "$app"; then
            focus="$app"
            return
        fi
    done
    open_system=1
    focus="system"
}

close_focus() {
    eval "open_$focus=0"
    message="Ventana cerrada: $focus"
    focus_next
}

show_logs() {
    clear_desktop
    move_to 1 1
    color_reset
    echo "Ridux logs"
    echo ""
    if [ -f /var/log/ridux-live-init.log ]; then
        tail -n 22 /var/log/ridux-live-init.log
    else
        echo "No existe /var/log/ridux-live-init.log todavia."
    fi
    echo ""
    printf "Presiona una tecla para volver al escritorio..."
    read_key >/dev/null 2>&1 || true
}

run_shell() {
    clear_desktop
    color_reset
    restore_tty
    echo "Ridux: entrando a /bin/sh. Escribe exit para volver al escritorio."
    /bin/sh || true
    set_desktop_tty
}

exit_to_ridux_shell() {
    clear_desktop
    color_reset
    restore_tty
    if [ -x /usr/local/bin/ridux-shell ]; then
        RIDUX_DESKTOP_DISABLE=1 exec /usr/local/bin/ridux-shell
    fi
    exec /bin/sh
}

read_key() {
    if [ -t 0 ]; then
        key="$(dd bs=1 count=1 2>/dev/null || true)"
        [ -n "$key" ] || return 1
        printf "%s" "$key"
        return 0
    fi

    IFS= read -r line || return 1
    printf "%s" "$line"
}

save_tty
trap cleanup EXIT INT TERM
set_desktop_tty
esc "?25l"

while :; do
    draw_screen
    if cmd="$(read_key)"; then
        :
    else
        [ -t 0 ] && continue
        exit 0
    fi
    case "$cmd" in
        "" )
            start_open=0
            quick_open=0
            ;;
        1|system|sistema)
            open_app system
            ;;
        2|files|archivos)
            open_app files
            ;;
        3|terminal|term)
            open_app terminal
            ;;
        4|settings|config)
            open_app settings
            ;;
        5|about|acerca)
            open_app about
            ;;
        m|menu|inicio)
            if [ "$start_open" = "1" ]; then start_open=0; else start_open=1; fi
            quick_open=0
            ;;
        q|quick|controles)
            if [ "$quick_open" = "1" ]; then quick_open=0; else quick_open=1; fi
            start_open=0
            ;;
        n|next|foco)
            focus_next
            message="Foco: $focus"
            ;;
        c|close|cerrar)
            close_focus
            ;;
        r|redraw|clear)
            message="Pantalla redibujada."
            ;;
        s|theme|tema)
            if [ "$theme" = "dark" ]; then theme="light"; else theme="dark"; fi
            message="Tema cambiado: $theme"
            ;;
        t|sh|shell)
            run_shell
            ;;
        l|logs|log)
            show_logs
            ;;
        b|reboot|reiniciar)
            restore_tty
            /sbin/reboot
            ;;
        o|poweroff|shutdown|apagar)
            restore_tty
            /sbin/shutdown -p now
            ;;
        x|exit|salir)
            exit_to_ridux_shell
            ;;
        help|h|\?)
            start_open=1
            quick_open=0
            message="Usa 1..5 para abrir ventanas, n foco, c cerrar."
            ;;
        *)
            message="Comando no reconocido: $cmd"
            ;;
    esac
done
