#!/system/bin/sh
set -eu

# Cyber Terminal Overlay Runner for Android Root
printf '\033[38;5;120m'
cat << 'EOF'
=========================================================
   ______      __                 ____                     __            
  / ____/_  __/ /_  ___  _____   / __ \_   _____  _____   / /___ ___  __ 
 / /   / / / / __ \/ _ \/ ___/  / / / / | / / _ \/ ___/  / / __ `/ / / / 
/ /___/ /_/ / /_/ /  __/ /     / /_/ /| |/ /  __/ /     / / /_/ / /_/ /  
\____/\__, /_.___/\___/_/      \____/ |___/\___/_/     /_/\__,_/\__, /   
     /____/                                                    /____/    
  [ Terminal & Cheat GUI (Overlay) - Ready for Android Root ]
=========================================================
EOF
printf '\033[0m'

if [ "$(id -u)" -ne 0 ]; then
    printf '\033[38;5;203m[-] Warning: Not running as root (UID: %s)!\033[0m\n' "$(id -u)"
    printf '\033[38;5;220m[!] Please run with root: su -c "%s"\033[0m\n' "$0"
fi

DIR="${TMPDIR:-/data/local/tmp}"
mkdir -p "$DIR"
BIN="$DIR/.cyber_overlay_$$"

cleanup() {
    rm -f "$BIN"
}
trap cleanup EXIT INT TERM

PAYLOAD_LINE=$(awk '/^__PAYLOAD_BELOW__$/ {print NR+1; exit}' "$0")
if [ -z "$PAYLOAD_LINE" ]; then
    printf '\033[38;5;203m[-] Error: Embedded binary payload missing in script.\033[0m\n' >&2
    exit 2
fi

printf '\033[38;5;81m[*] Extracting native overlay executable to %s...\033[0m\n' "$BIN"
tail -n +"$PAYLOAD_LINE" "$0" | gzip -dc > "$BIN"
chmod 700 "$BIN"

printf '\033[38;5;120m[+] Launching overlay...\033[0m\n'
"$BIN" "$@"
EXIT_CODE=$?

exit $EXIT_CODE
__PAYLOAD_BELOW__
