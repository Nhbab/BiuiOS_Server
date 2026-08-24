cat << 'EOF' > /usr/bin/bpm
#!/bin/bash
DB_DIR="/var/db/bpm"
INSTALLED_DIR="$DB_DIR/installed"
CACHE_DIR="$DB_DIR/cache"
REPO_FILE="$DB_DIR/repo.url"

DEFAULT_REPO="https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main"

# Load repository URL if custom repository file exists
if [ -f "$REPO_FILE" ]; then
    REPO_URL=$(cat "$REPO_FILE")
else
    REPO_URL="$DEFAULT_REPO"
fi

mkdir -p "$INSTALLED_DIR" "$CACHE_DIR"

download_file() {
    local url="$1"
    local output="$2"
    if command -v wget >/dev/null 2>&1; then
        wget -q --no-check-certificate "$url" -O "$output"
    elif command -v curl >/dev/null 2>&1; then
        curl -s -k -L "$url" -o "$output"
    else
        echo "Error: Neither wget nor curl is available!"
        return 1
    fi
}

update_ldconfig() {
    if command -v ldconfig >/dev/null 2>&1; then
        echo "Updating dynamic linker cache (ldconfig)..."
        ldconfig 2>/dev/null
    fi
}

case "$1" in
    add)
        NEW_URL="$2"
        [ -z "$NEW_URL" ] && { echo "Usage: bpm add <repository_url_or_INDEX_url>"; exit 1; }

        # Normalize URL by removing trailing /INDEX or /
        CLEAN_URL=$(echo "$NEW_URL" | sed 's/\/INDEX$//' | sed 's/\/$//')
        echo "$CLEAN_URL" > "$REPO_FILE"
        echo "Repository source set to: $CLEAN_URL"
        ;;

    update)
        echo "Fetching index from $REPO_URL..."
        if download_file "$REPO_URL/INDEX" "$DB_DIR/INDEX"; then
            echo "Package index updated successfully."
        else
            echo "Error: Failed to fetch package index from repository."
            exit 1
        fi
        ;;

    install)
        TARGET="$2"
        [ -z "$TARGET" ] && { echo "Usage: bpm install <package_name | /path/to/file.bpm>"; exit 1; }

        # --- OFFLINE INSTALLATION ---
        if [ -f "$TARGET" ] && [[ "$TARGET" == *.bpm ]]; then
            FILENAME=$(basename "$TARGET")
            FILENAME_NO_EXT="${FILENAME%.bpm}"
            
            PKG="${FILENAME_NO_EXT%-*}"
            VERSION="${FILENAME_NO_EXT##*-}"
            
            # Fallback if package filename has no version hyphen
            [ "$PKG" = "$VERSION" ] && VERSION="local"

            echo "Installing offline package '$PKG v$VERSION' from $TARGET..."
            tar -tzf "$TARGET" > "$INSTALLED_DIR/$PKG.list"
            tar -xzf "$TARGET" -C /

            update_ldconfig
            echo "$PKG v$VERSION installed successfully (offline)."
            exit 0
        fi

        # --- ONLINE INSTALLATION ---
        PKG="$TARGET"
        ENTRY=$(grep "^$PKG " "$DB_DIR/INDEX" 2>/dev/null)
        [ -z "$ENTRY" ] && { echo "Error: Package '$PKG' not found in INDEX and local file does not exist."; exit 1; }

        VERSION=$(echo "$ENTRY" | awk '{print $2}')
        HASH=$(echo "$ENTRY" | awk '{print $3}')
        FILE="$PKG-$VERSION.bpm"

        echo "Downloading $FILE..."
        download_file "$REPO_URL/$FILE" "$CACHE_DIR/$FILE" || { echo "Download failed."; exit 1; }

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
        echo "Usage: bpm {add <url>|update|install <pkg|file.bpm>|remove <pkg>|list}"
        ;;
esac
EOF

chmod +x /usr/bin/bpm
