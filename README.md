
# BiuiOS & bpm (BiuiOS Package Manager)

BiuiOS is a lightweight, custom Linux distribution built using Buildroot. It features **`bpm`**, a custom shell-based package manager designed to download, verify, install, and remove pre-compiled software archives (`.bpm`) and their bundled shared libraries.

---

## Key Features

* **Lightweight Package Architecture:** Packages are gzipped tarballs (`.bpm`) structured relative to the root filesystem (`/`).
* **Integrity & Verification:** Every package is validated against a SHA256 checksum defined in the central repository manifest.
* **Library Management:** Automatically updates the dynamic linker cache (`ldconfig`) after installing or removing packages.
* **Resilient Downloading:** Supports download fallbacks using either `wget` or `curl`.
* **Clean Uninstallation:** Tracks installed files in file lists (`/var/db/bpm/installed/<pkg>.list`) to ensure clean removals.

---

## Directory Structure

* **`/usr/bin/bpm`**: Main package manager executable script.
* **`/var/db/bpm/INDEX`**: Local cache of the remote repository index.
* **`/var/db/bpm/installed/`**: Manifest files tracking installed files per package.
* **`/var/db/bpm/cache/`**: Temporary directory for downloaded `.bpm` archives.

---

## Command Usage

```bash
# Update the local package index from the server
bpm update

# Download, verify, and install a package
bpm install <package_name>

# Remove an installed package and its files
bpm remove <package_name>

# List all currently installed packages
bpm list

# Add More Sources
bpm add https://yourwantsource.com/
*Examples
bpm add https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/refs/heads/main

```

---

## Package Repository & Manifest Format

The package manager fetches packages from the official server repository at `[https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main](https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main)`.

The server manifest (`INDEX`) requires exactly three space-separated fields per line: **`NAME VERSION HASH`**.

**Sample `INDEX` File:**

```text
hello 1.0.0 <your Sha 256>
```

---

## Building a `.bpm` Package

1. **Assemble the Filesystem Tree:** Create your directory layout matching the system target structure:
```bash
mkdir -p mypackage_pkg/usr/bin
mkdir -p mypackage_pkg/usr/lib
```


2. **Add Binaries & Shared Libraries:** Place binaries inside `usr/bin` and any required shared objects (`.so`) inside `usr/lib`.
3. **Pack into `.bpm` Archive:**
```bash
cd mypackage_pkg
tar -czf ../mypackage-1.0.0.bpm .
cd ..

```


4. **Generate SHA256 Checksum:**
```bash
sha256sum mypackage-1.0.0.bpm

```


5. **Deploy:** Upload the `.bpm` file to `Nhbab/BiuiOS_Server Or Your Own Host Of BiuiOS_Server` and add `mypackage 1.0.0 <SHA256_HASH>` to the repository `INDEX` file.
