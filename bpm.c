#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

#define DB_DIR "/var/db/bpm"
#define INSTALLED_DIR "/var/db/bpm/installed"
#define CACHE_DIR "/var/db/bpm/cache"
#define REPO_FILE "/var/db/bpm/repo.url"
#define DEFAULT_REPO "https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main"

void ensure_directories(void) {
    system("mkdir -p " INSTALLED_DIR " " CACHE_DIR);
}

void get_repo_url(char *buf, size_t size) {
    FILE *f = fopen(REPO_FILE, "r");
    if (f) {
        if (fgets(buf, size, f)) {
            buf[strcspn(buf, "\r\n")] = 0;
        }
        fclose(f);
    } else {
        strncpy(buf, DEFAULT_REPO, size - 1);
        buf[size - 1] = '\0';
    }
}

int download_file(const char *url, const char *output) {
    char cmd[1024];
    if (access("/usr/bin/wget", X_OK) == 0 || access("/bin/wget", X_OK) == 0) {
        snprintf(cmd, sizeof(cmd), "wget -q --no-check-certificate \"%s\" -O \"%s\"", url, output);
    } else if (access("/usr/bin/curl", X_OK) == 0 || access("/bin/curl", X_OK) == 0) {
        snprintf(cmd, sizeof(cmd), "curl -s -k -L \"%s\" -o \"%s\"", url, output);
    } else {
        fprintf(stderr, "Error: Neither wget nor curl is available!\n");
        return -1;
    }
    return system(cmd);
}

void update_ldconfig(void) {
    if (access("/sbin/ldconfig", X_OK) == 0 || access("/usr/sbin/ldconfig", X_OK) == 0) {
        printf("Updating dynamic linker cache (ldconfig)...\n");
        system("ldconfig 2>/dev/null");
    }
}

void run_post_install(const char *pkg) {
    if (access("/.bpm_postinstall", F_OK) == 0) {
        printf("Running post-install script for %s...\n", pkg);
        system("chmod +x /.bpm_postinstall && /.bpm_postinstall");
        unlink("/.bpm_postinstall");
    }
}

void cmd_add(const char *new_url) {
    char clean_url[512];
    strncpy(clean_url, new_url, sizeof(clean_url) - 1);
    clean_url[sizeof(clean_url) - 1] = '\0';

    size_t len = strlen(clean_url);
    if (len > 6 && strcmp(clean_url + len - 6, "/INDEX") == 0) {
        clean_url[len - 6] = '\0';
    }
    len = strlen(clean_url);
    if (len > 0 && clean_url[len - 1] == '/') {
        clean_url[len - 1] = '\0';
    }

    FILE *f = fopen(REPO_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", clean_url);
        fclose(f);
        printf("Repository source set to: %s\n", clean_url);
    } else {
        perror("Error saving repo URL");
    }
}

void cmd_update(void) {
    char repo_url[512], index_url[1024], index_file[512];
    get_repo_url(repo_url, sizeof(repo_url));

    snprintf(index_url, sizeof(index_url), "%s/INDEX", repo_url);
    snprintf(index_file, sizeof(index_file), "%s/INDEX", DB_DIR);

    printf("Fetching index from %s...\n", repo_url);
    if (download_file(index_url, index_file) == 0) {
        printf("Package index updated successfully.\n");
    } else {
        fprintf(stderr, "Error: Failed to fetch package index from repository.\n");
        exit(1);
    }
}

int install_package(const char *pkg) {
    char list_path[512];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg);

    if (access(list_path, F_OK) == 0) {
        fprintf(stderr, "Error: Package '%s' is already installed.\n", pkg);
        fprintf(stderr, "You must remove it first using 'bpm remove %s' before reinstalling.\n", pkg);
        return -1;
    }

    char index_path[512];
    snprintf(index_path, sizeof(index_path), "%s/INDEX", DB_DIR);
    FILE *f = fopen(index_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Package index file missing. Run 'bpm update' first.\n");
        return -1;
    }

    char line[1024], entry_name[128], version[64], expected_hash[128], deps[256];
    int found = 0;
    deps[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        int count = sscanf(line, "%127s %63s %127s %255s", entry_name, version, expected_hash, deps);
        if (count >= 3 && strcmp(entry_name, pkg) == 0) {
            found = 1;
            if (count < 4) strcpy(deps, "-");
            break;
        }
    }
    fclose(f);

    if (!found) {
        fprintf(stderr, "Error: Package '%s' not found in INDEX.\n", pkg);
        return -1;
    }

    // Dependency Resolution
    if (strlen(deps) > 0 && strcmp(deps, "-") != 0) {
        printf("Resolving dependencies for %s: [%s]\n", pkg, deps);
        char *dep_token = strtok(deps, ",");
        while (dep_token != NULL) {
            char dep_list[512];
            snprintf(dep_list, sizeof(dep_list), "%s/%s.list", INSTALLED_DIR, dep_token);
            if (access(dep_list, F_OK) == 0) {
                printf("Dependency '%s' is already installed.\n", dep_token);
            } else {
                printf("Installing dependency '%s'...\n", dep_token);
                if (install_package(dep_token) != 0) return -1;
            }
            dep_token = strtok(NULL, ",");
        }
    }

    // Download & Verify
    char repo_url[512], file_name[256], file_url[1024], cache_path[512];
    get_repo_url(repo_url, sizeof(repo_url));
    snprintf(file_name, sizeof(file_name), "%s-%s.bpm", pkg, version);
    snprintf(file_url, sizeof(file_url), "%s/%s", repo_url, file_name);
    snprintf(cache_path, sizeof(cache_path), "%s/%s", CACHE_DIR, file_name);

    printf("Downloading %s...\n", file_name);
    if (download_file(file_url, cache_path) != 0) {
        fprintf(stderr, "Download failed.\n");
        return -1;
    }

    char calc_cmd[1024], calc_hash[128] = {0};
    snprintf(calc_cmd, sizeof(calc_cmd), "sha256sum \"%s\" | awk '{print $1}'", cache_path);
    FILE *p = popen(calc_cmd, "r");
    if (p) {
        if (fgets(calc_hash, sizeof(calc_hash), p)) {
            calc_hash[strcspn(calc_hash, "\r\n")] = 0;
        }
        pclose(p);
    }

    if (strcmp(calc_hash, expected_hash) != 0) {
        fprintf(stderr, "Error: Checksum mismatch for %s!\n", file_name);
        unlink(cache_path);
        return -1;
    }

    // Extract & Install
    printf("Installing %s v%s...\n", pkg, version);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -tzf \"%s\" > \"%s\"", cache_path, list_path);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C /", cache_path);
    system(cmd);

    unlink(cache_path);
    run_post_install(pkg);
    update_ldconfig();

    printf("%s v%s installed successfully.\n", pkg, version);
    return 0;
}

void cmd_install(const char *target) {
    if (access(target, F_OK) == 0 && strstr(target, ".bpm") != NULL) {
        char target_copy[512], pkg[128], version[64];
        strncpy(target_copy, target, sizeof(target_copy) - 1);
        char *bname = basename(target_copy);

        char *ext = strrchr(bname, '.');
        if (ext) *ext = '\0';

        char *dash = strrchr(bname, '-');
        if (dash) {
            *dash = '\0';
            strcpy(pkg, bname);
            strcpy(version, dash + 1);
        } else {
            strcpy(pkg, bname);
            strcpy(version, "local");
        }

        char list_path[512];
        snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg);
        if (access(list_path, F_OK) == 0) {
            fprintf(stderr, "Error: Package '%s' is already installed.\n", pkg);
            fprintf(stderr, "You must remove it first using 'bpm remove %s' before reinstalling.\n", pkg);
            exit(1);
        }

        printf("Installing offline package '%s v%s' from %s...\n", pkg, version, target);
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "tar -tzf \"%s\" > \"%s\"", target, list_path);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C /", target);
        system(cmd);

        run_post_install(pkg);
        update_ldconfig();
        printf("%s v%s installed successfully (offline).\n", pkg, version);
    } else {
        install_package(target);
    }
}

void cmd_remove(const char *pkg) {
    char list_path[512];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg);

    if (access(list_path, F_OK) != 0) {
        fprintf(stderr, "Error: Package '%s' is not installed.\n", pkg);
        exit(1);
    }

    printf("Removing %s...\n", pkg);
    FILE *f = fopen(list_path, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (strlen(line) == 0) continue;

            char abs_path[2048];
            snprintf(abs_path, sizeof(abs_path), "/%s", line);
            unlink(abs_path);
        }
        fclose(f);
    }

    unlink(list_path);
    update_ldconfig();
    printf("%s removed.\n", pkg);
}

void cmd_list(void) {
    printf("Installed packages:\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ls \"%s\" 2>/dev/null | sed 's/\\.list$//'", INSTALLED_DIR);
    system(cmd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("BiuiOS Package Manager (.bpm format)\n");
        printf("Usage: bpm {add <url>|update|install <pkg|file.bpm>|remove <pkg>|list}\n");
        return 1;
    }

    ensure_directories();

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: bpm add <repository_url>\n"); return 1; }
        cmd_add(argv[2]);
    } else if (strcmp(argv[1], "update") == 0) {
        cmd_update();
    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: bpm install <package_name | file.bpm>\n"); return 1; }
        cmd_install(argv[2]);
    } else if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: bpm remove <package_name>\n"); return 1; }
        cmd_remove(argv[2]);
    } else if (strcmp(argv[1], "list") == 0) {
        cmd_list();
    } else {
        printf("Unknown action: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
