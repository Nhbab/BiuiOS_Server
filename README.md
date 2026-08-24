How to install BPM :
`cat << 'EOF' > /usr/bin/bpm
#!/bin/bash
DB_DIR="/var/db/bpm"
INSTALLED_DIR="$DB_DIR/installed"
CACHE_DIR="$DB_DIR/cache"

REPO_URL="https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main"
WGET_OPTS="-q --no-check-certificate"

mkdir -p "$INSTALLED_DIR" "$CACHE_DIR"

update_ldconfig() {
    if command -v ldconfig >/dev/null 2>&1; then
        echo "Updating dynamic linker cache (ldconfig)..."
        ldconfig 2>/dev/null
    fi
}

check_missing_so() {
    [ ! -f "$1" ] && return
    if command -v ldd >/dev/null 2>&1; then
        while read -r file; do
            if [ -f "/$file" ] && file "/$file" 2>/dev/null | grep -q "ELF"; then
                MISSING=$(ldd "/$file" 2>/dev/null | grep "not found")
                if [ -n "$MISSING" ]; then
                    echo "  [WARNING] Missing shared libraries for /$file:"
                    echo "$MISSING" | sed 's/^/    /'
                fi
            fi
        done < "$1"
    fi
}

case "$1" in
    update)
        echo "Fetching index from Nhbab/BiuiOS_Server..."
        if wget $WGET_OPTS "$REPO_URL/INDEX" -O "$DB_DIR/INDEX"; then
            echo "Package index updated successfully."
        else
            echo "Error: Failed to fetch package index from GitHub."
            exit 1
        fi
        ;;

    install)
        PKG="$2"
        [ -z "$PKG" ] && { echo "Usage: bpm install <package>"; exit 1; }

        ENTRY=$(grep "^$PKG " "$DB_DIR/INDEX" 2>/dev/null)
        [ -z "$ENTRY" ] && { echo "Error: Package '$PKG' not found in INDEX."; exit 1; }

        VERSION=$(echo "$ENTRY" | awk '{print $2}')
        HASH=$(echo "$ENTRY" | awk '{print $3}')
        FILE="$PKG-$VERSION.bpm"

        echo "Downloading $FILE..."
        wget $WGET_OPTS "$REPO_URL/$FILE" -O "$CACHE_DIR/$FILE" || { echo "Download failed."; exit 1; }

        CALC_HASH=$(sha256sum "$CACHE_DIR/$FILE" | awk '{print $1}')
        if [ "$CALC_HASH" != "$HASH" ]; then
            echo "Error: Checksum mismatch!"
            rm -f "$CACHE_DIR/$FILE"
            exit 1
        fi

        echo "Installing $PKG..."
        tar -tzf "$CACHE_DIR/$FILE" > "$INSTALLED_DIR/$PKG.list"
        tar -xzf "$CACHE_DIR/$FILE" -C /
        rm -f "$CACHE_DIR/$FILE"

        update_ldconfig
        check_missing_so "$INSTALLED_DIR/$PKG.list"

        echo "$PKG v$VERSION installed successfully."
        ;;

    remove)
        PKG="$2"
        LIST="$INSTALLED_DIR/$PKG.list"
        [ ! -f "$LIST" ] && { echo "Error: Package '$PKG' is not installed."; exit 1; }

        echo "Removing $PKG..."
        while read -r file; do
            [ -f "/$file" ] || [ -L "/$file" ] && rm -f "/$file"
        done < "$LIST"
        
        rm -f "$LIST"
        update_ldconfig
        echo "$PKG removed."
        ;;

    list)
        echo "Installed packages:"
        ls "$INSTALLED_DIR" 2>/dev/null | sed 's/\.list$//'
        ;;

    *)
        echo "BiuiOS Package Manager (.bpm format)"
        echo "Usage: bpm {update|install <pkg>|remove <pkg>|list}"
        ;;
esac
EOF

chmod +x /usr/bin/bpm`
