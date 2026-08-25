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

run_post_install() {
    local PKG="$1"
    if [ -f "/.bpm_postinstall" ]; then
        echo "Running post-install script for $PKG..."
        chmod +x /.bpm_postinstall
        /.bpm_postinstall
        rm -f /.bpm_postinstall
    fi
}

install_package() {
    local PKG="$1"

    # --- DISALLOW RE-INSTALLATION ---
    if [ -f "$INSTALLED_DIR/$PKG.list" ]; then
        echo "Error: Package '$PKG' is already installed."
        echo "You must remove it first using 'bpm remove $PKG' before reinstalling."
        return 1
    fi

    # Lookup package entry in INDEX
    ENTRY=$(grep "^$PKG " "$DB_DIR/INDEX" 2>/dev/null)
    [ -z "$ENTRY" ] && { echo "Error: Package '$PKG' not found in INDEX."; return 1; }

    VERSION=$(echo "$ENTRY" | awk '{print $2}')
    HASH=$(echo "$ENTRY" | awk '{print $3}')
    DEPS=$(echo "$ENTRY" | awk '{print $4}')
    FILE="$PKG-$VERSION.bpm"

    # --- DEPENDENCY RESOLVER ---
    if [ -n "$DEPS" ] && [ "$DEPS" != "-" ]; then
        echo "Resolving dependencies for $PKG: [$DEPS]"
        IFS=',' read -ra DEP_LIST <<< "$DEPS"
        for dep in "${DEP_LIST[@]}"; do
            if [ -f "$INSTALLED_DIR/$dep.list" ]; then
                echo "Dependency '$dep' is already installed."
            else
                echo "Installing dependency '$dep'..."
                install_package "$dep" || return 1
            fi
        done
    fi

    # --- DOWNLOAD & VERIFY ---
    echo "Downloading $FILE..."
    download_file "$REPO_URL/$FILE" "$CACHE_DIR/$FILE" || { echo "Download failed."; return 1; }

    CALC_HASH=$(sha256sum "$CACHE_DIR/$FILE" | awk '{print $1}')
    if [ "$CALC_HASH" != "$HASH" ]; then
        echo "Error: Checksum mismatch for $FILE!"
        rm -f "$CACHE_DIR/$FILE"
        return 1
    fi

    # --- UNPACK & RECORD ---
    echo "Installing $PKG v$VERSION..."
    tar -tzf "$CACHE_DIR/$FILE" > "$INSTALLED_DIR/$PKG.list"
    tar -xzf "$CACHE_DIR/$FILE" -C /
    rm -f "$CACHE_DIR/$FILE"

    # --- POST-INSTALL COMMAND ---
    run_post_install "$PKG"

    update_ldconfig
    echo "$PKG v$VERSION installed successfully."
}

case "$1" in
    add)
        NEW_URL="$2"
        [ -z "$NEW_URL" ] && { echo "Usage: bpm add <repository_url_or_INDEX_url>"; exit 1; }

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
            [ "$PKG" = "$VERSION" ] && VERSION="local"

            # Disallow offline reinstall
            if [ -f "$INSTALLED_DIR/$PKG.list" ]; then
                echo "Error: Package '$PKG' is already installed."
                echo "You must remove it first using 'bpm remove $PKG' before reinstalling."
                exit 1
            fi

            echo "Installing offline package '$PKG v$VERSION' from $TARGET..."
            tar -tzf "$TARGET" > "$INSTALLED_DIR/$PKG.list"
            tar -xzf "$TARGET" -C /

            run_post_install "$PKG"
            update_ldconfig
            echo "$PKG v$VERSION installed successfully (offline)."
            exit 0
        fi

        # --- ONLINE INSTALLATION ---
        install_package "$TARGET"
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
chmod +x /usr/bin/bpm
