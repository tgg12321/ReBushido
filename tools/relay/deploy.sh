#!/bin/bash
# Install, check or remove the ReBushido relay server on an Ubuntu box, as root.
#
#   bash deploy.sh             install, or bring an install up to date (idempotent)
#   bash deploy.sh status      what is installed and whether it listens
#   bash deploy.sh uninstall   undo everything install did
#
# Run it from a copy of tools/relay/ (it installs the rebushido_relay.py and
# rebushido-relay.service beside it). It touches nothing but its own unit,
# its folder /opt/rebushido-relay and, only when ufw is active and it added
# it, one ufw rule for 7845/udp. It never restarts or reconfigures any other
# service.
set -eu

NAME=rebushido-relay
PORT=7845
DIR=/opt/rebushido-relay
UNIT=/etc/systemd/system/$NAME.service
MARK=$DIR/ufw-rule-added
PYTHON=/usr/bin/python3
SRC=$(cd "$(dirname "$0")" && pwd)

CHANGES=()
changed() { CHANGES+=("$1"); echo "  changed: $1"; }
note() { echo "  $1"; }
die() { echo "deploy.sh: $1" >&2; exit "${2:-1}"; }

need_root() {
    [ "$(id -u)" -eq 0 ] || die "run as root (sudo bash $0 ${1:-})"
    command -v systemctl >/dev/null || die "no systemctl: this needs systemd"
}

# The UDP sockets bound to the port, one ss line each (nothing if none).
port_lines() {
    ss -ulpn 2>/dev/null | tail -n +2 | awk -v p=":$PORT" '
        { a = $4; if (length(a) > length(p) && substr(a, length(a) - length(p) + 1) == p) print }'
}

main_pid() {
    systemctl show -p MainPID --value "$NAME.service" 2>/dev/null || echo 0
}

# 0 if every socket on the port belongs to our service's process.
port_is_ours() {
    local pid lines
    lines=$(port_lines)
    [ -n "$lines" ] || return 0
    pid=$(main_pid)
    [ -n "$pid" ] && [ "$pid" != 0 ] || return 1
    ! echo "$lines" | grep -v -q "pid=$pid,"
}

ufw_active() {
    command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q '^Status: active'
}

ufw_allows_port() {
    ufw status 2>/dev/null | grep -Eq "^$PORT/udp( \(v6\))?[[:space:]]+ALLOW"
}

do_install() {
    need_root
    for f in rebushido_relay.py "$NAME.service"; do
        [ -f "$SRC/$f" ] || die "$SRC/$f is missing: run deploy.sh from a copy of tools/relay/"
    done
    [ -x "$PYTHON" ] || die "no $PYTHON (apt install python3-minimal)"
    "$PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 8))' || die "$PYTHON is older than 3.8"
    local sd
    sd=$(systemctl --version | awk 'NR == 1 { print $2 }')
    [ "${sd:-0}" -ge 235 ] 2>/dev/null || die "systemd $sd is too old for DynamicUser (235 or newer)"
    command -v ss >/dev/null || die "no ss (iproute2)"

    echo "Installing $NAME (UDP $PORT)"
    if ! port_is_ours; then
        echo "UDP port $PORT is taken by something else:" >&2
        port_lines >&2
        die "refusing to install; nothing was changed" 2
    fi

    local restart=0 reload=0
    if [ ! -d "$DIR" ]; then
        install -d -m 0755 -o root -g root "$DIR"
        changed "created $DIR"
    fi
    if ! cmp -s "$SRC/rebushido_relay.py" "$DIR/rebushido_relay.py"; then
        install -m 0644 -o root -g root "$SRC/rebushido_relay.py" "$DIR/rebushido_relay.py"
        changed "installed $DIR/rebushido_relay.py"
        restart=1
    fi
    if ! cmp -s "$SRC/$NAME.service" "$UNIT"; then
        install -m 0644 -o root -g root "$SRC/$NAME.service" "$UNIT"
        changed "installed $UNIT"
        reload=1
        restart=1
    fi
    if [ "$reload" = 1 ]; then
        systemctl daemon-reload
    fi
    if ! systemctl is-enabled --quiet "$NAME.service" 2>/dev/null; then
        systemctl enable --quiet "$NAME.service"
        changed "enabled $NAME.service (starts at boot)"
    fi
    if systemctl is-active --quiet "$NAME.service"; then
        if [ "$restart" = 1 ]; then
            systemctl restart "$NAME.service"
            changed "restarted $NAME.service"
        fi
    else
        systemctl start "$NAME.service"
        changed "started $NAME.service"
    fi

    if ufw_active; then
        if [ -f "$MARK" ]; then
            note "ufw: the rule for $PORT/udp this script added is in place"
        elif ufw_allows_port; then
            note "ufw: $PORT/udp was already allowed (not by this script; left alone)"
        else
            ufw allow "$PORT/udp" comment "$NAME" >/dev/null
            : >"$MARK"
            changed "ufw: allowed $PORT/udp in"
        fi
    else
        note "ufw is not active: no host firewall rule added"
    fi

    # It must be up and listening, on the port, as itself.

    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if systemctl is-active --quiet "$NAME.service" && [ -n "$(port_lines)" ] && port_is_ours; then
            break
        fi
        sleep 0.5
    done
    if ! systemctl is-active --quiet "$NAME.service" || [ -z "$(port_lines)" ]; then
        journalctl -u "$NAME.service" -n 20 --no-pager >&2 || true
        die "$NAME.service is not listening on UDP $PORT"
    fi
    echo "Listening:"
    port_lines | awk '{ print "  " $4 }'
    if [ "${#CHANGES[@]}" -eq 0 ]; then
        echo "Nothing changed: already installed and up to date."
    else
        echo "Changed ${#CHANGES[@]} thing(s)."
    fi
    echo "A cloud firewall in front of the box (Hetzner's, for one) must let UDP $PORT in as well."
}

do_uninstall() {
    need_root
    echo "Uninstalling $NAME"
    local had_mark=0
    [ -f "$MARK" ] && had_mark=1
    if [ -f "$UNIT" ] || systemctl cat "$NAME.service" >/dev/null 2>&1; then
        systemctl disable --now --quiet "$NAME.service" 2>/dev/null || true
        changed "stopped and disabled $NAME.service"
    fi
    if [ -f "$UNIT" ]; then
        rm -f "$UNIT"
        systemctl daemon-reload
        systemctl reset-failed "$NAME.service" 2>/dev/null || true
        changed "removed $UNIT"
    fi
    if [ "$had_mark" = 1 ]; then
        if command -v ufw >/dev/null && ufw_allows_port; then
            ufw delete allow "$PORT/udp" >/dev/null
            changed "ufw: removed the $PORT/udp rule this script added"
        fi
    else
        note "ufw: no rule of this script's to remove"
    fi
    if [ -d "$DIR" ]; then
        rm -f "$DIR/rebushido_relay.py" "$MARK"
        if rmdir "$DIR" 2>/dev/null; then
            changed "removed $DIR"
        else
            echo "  $DIR holds files this script did not put there; left:" >&2
            ls -la "$DIR" >&2
        fi
    fi
    note "no user to remove: the service ran as a DynamicUser"
    if [ "${#CHANGES[@]}" -eq 0 ]; then
        echo "Nothing to do: it was not installed."
    else
        echo "Changed ${#CHANGES[@]} thing(s)."
    fi
}

do_status() {
    echo "$NAME:"
    if [ -f "$UNIT" ]; then
        echo "  unit:     $UNIT ($(systemctl is-enabled "$NAME.service" 2>/dev/null || true)," \
            "$(systemctl is-active "$NAME.service" 2>/dev/null || true))"
    else
        echo "  unit:     not installed"
    fi
    if [ -f "$DIR/rebushido_relay.py" ]; then
        echo "  server:   $DIR/rebushido_relay.py"
    else
        echo "  server:   not installed"
    fi
    local lines
    lines=$(port_lines)
    if [ -z "$lines" ]; then
        echo "  UDP $PORT: free"
    elif port_is_ours; then
        echo "  UDP $PORT: $NAME"
    else
        echo "  UDP $PORT: taken by something else:"
        echo "$lines" | sed 's/^/    /'
    fi
    if ufw_active; then
        if [ -f "$MARK" ]; then
            echo "  ufw:      active, $PORT/udp allowed by this script"
        elif ufw_allows_port; then
            echo "  ufw:      active, $PORT/udp allowed (not by this script)"
        else
            echo "  ufw:      active, $PORT/udp NOT allowed"
        fi
    else
        echo "  ufw:      not active"
    fi
}

case "${1:-install}" in
install) do_install ;;
uninstall) do_uninstall ;;
status) do_status ;;
*) die "usage: bash deploy.sh [install|status|uninstall]" ;;
esac
