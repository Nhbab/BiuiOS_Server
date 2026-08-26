#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <dirent.h>
#include <libgen.h>

#define DB_DIR "/var/db/bpm"
#define INSTALLED_DIR "/var/db/bpm/installed"
#define CACHE_DIR "/var/db/bpm/cache"
#define REPO_FILE "/var/db/bpm/repo.url"
#define DEFAULT_REPO "https://raw.githubusercontent.com/Nhbab/BiuiOS_Server/main"

/* ========================================================================== */
/*                          1. SECURITY UTILITIES                             */
/* ========================================================================== */

int is_valid_pkg_name(const char *name) {
    if (!name || strlen(name) == 0 || strlen(name) > 128) return 0;
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (!isalnum(name[i]) && name[i] != '-' && name[i] != '_' && name[i] != '.') {
            return 0;
        }
    }
    return 1;
}

int is_safe_path(const char *path) {
    if (strstr(path, "..") != NULL) return 0;
    if (path[0] == '/') path++;
    if (strstr(path, ":") != NULL) return 0;
    return 1;
}

void require_root(void) {
    if (getuid() != 0) {
        fprintf(stderr, "Security Error: Root privileges required for this operation.\n");
        exit(EXIT_FAILURE);
    }
}

/* ========================================================================== */
/*                          2. NATIVE SHA-256 ENGINE                          */
/* ========================================================================== */

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    unsigned long long bitlen;
    uint32_t state[8];
} SHA256_CTX;

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[57] = ctx->bitlen >> 56; ctx->data[56] = ctx->bitlen >> 48;
    ctx->data[58] = ctx->bitlen >> 40; ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[60] = ctx->bitlen >> 24; ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[62] = ctx->bitlen >> 8;  ctx->data[63] = ctx->bitlen;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

int calculate_file_sha256(const char *filename, char output_hex[65]) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint8_t buf[4096];
    size_t bytes;

    while ((bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(&ctx, buf, bytes);
    }
    fclose(f);

    uint8_t hash[32];
    sha256_final(&ctx, hash);

    for (int i = 0; i < 32; i++) {
        sprintf(output_hex + (i * 2), "%02x", hash[i]);
    }
    output_hex[64] = '\0';
    return 0;
}

/* ========================================================================== */
/*                 3. DOWNLOADER ENGINE                                       */
/* ========================================================================== */

int download_file(const char *url, const char *output_path) {
    char cmd[1024];

    if (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0) {
        if (access("/usr/bin/wget", X_OK) == 0 || access("/bin/wget", X_OK) == 0) {
            snprintf(cmd, sizeof(cmd), "wget -q --no-check-certificate \"%s\" -O \"%s\"", url, output_path);
            return system(cmd);
        } else if (access("/usr/bin/curl", X_OK) == 0 || access("/bin/curl", X_OK) == 0) {
            snprintf(cmd, sizeof(cmd), "curl -s -k -L \"%s\" -o \"%s\"", url, output_path);
            return system(cmd);
        }
    }

    char host[256] = {0}, path[512] = {0}, port[10] = "80";
    if (sscanf(url, "http://%255[^/]%511s", host, path) < 1) {
        fprintf(stderr, "Error: Unsupported URL format.\n");
        return -1;
    }
    if (path[0] == '\0') strcpy(path, "/");

    char *port_ptr = strchr(host, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
    }

    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        fprintf(stderr, "Error: DNS Resolution failed for %s\n", host);
        return -1;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0 || connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "Error: Unable to connect to host %s\n", host);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: bpm-sec-client\r\nConnection: close\r\n\r\n",
             path, host);
    send(sockfd, request, strlen(request), 0);

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        close(sockfd);
        return -1;
    }
    chmod(output_path, 0600);

    char buffer[4096];
    int header_ended = 0, bytes;
    char *header_end_ptr;

    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (!header_ended) {
            header_end_ptr = strstr(buffer, "\r\n\r\n");
            if (header_end_ptr) {
                header_ended = 1;
                int header_len = (header_end_ptr + 4) - buffer;
                fwrite(buffer + header_len, 1, bytes - header_len, out);
            }
        } else {
            fwrite(buffer, 1, bytes, out);
        }
    }

    fclose(out);
    close(sockfd);
    return 0;
}

/* ========================================================================== */
/*                      4. PACKAGE MANAGER CORE ENGINE                        */
/* ========================================================================== */

void mkdir_secure(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

void get_repo_url(char *buf, size_t size) {
    FILE *f = fopen(REPO_FILE, "r");
    if (f) {
        if (fgets(buf, size, f)) buf[strcspn(buf, "\r\n")] = 0;
        fclose(f);
    } else {
        strncpy(buf, DEFAULT_REPO, size - 1);
    }
}

void update_ldconfig(void) {
    if (access("/sbin/ldconfig", X_OK) == 0 || access("/usr/sbin/ldconfig", X_OK) == 0) {
        system("ldconfig 2>/dev/null");
    }
}

void run_post_install(const char *pkg) {
    if (access("/.bpm_postinstall", F_OK) == 0) {
        printf("Running post-install script for %s...\n", pkg);
        chmod("/.bpm_postinstall", 0700);
        system("/.bpm_postinstall");
        unlink("/.bpm_postinstall");
    }
}

int cmd_update(void) {
    char repo_url[512], index_url[1024], index_file[512];

    get_repo_url(repo_url, sizeof(repo_url));
    snprintf(index_url, sizeof(index_url), "%s/INDEX", repo_url);
    snprintf(index_file, sizeof(index_file), "%s/INDEX", DB_DIR);

    printf("Fetching index from %s...\n", repo_url);
    if (download_file(index_url, index_file) == 0) {
        chmod(index_file, 0600);
        printf("Updated index successfully.\n");
        return 0;
    } else {
        fprintf(stderr, "Error: Failed to download repository INDEX.\n");
        return -1;
    }
}

int install_local_package(const char *filepath) {
    char path_copy[512];
    strncpy(path_copy, filepath, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *bname = basename(path_copy);
    char stem[256];
    strncpy(stem, bname, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = '\0';

    char *ext = strstr(stem, ".bpm");
    if (ext) *ext = '\0';

    char pkg_name[128];
    strncpy(pkg_name, stem, sizeof(pkg_name) - 1);
    pkg_name[sizeof(pkg_name) - 1] = '\0';

    for (int i = 0; stem[i] != '\0'; i++) {
        if (stem[i] == '-' && isdigit((unsigned char)stem[i + 1])) {
            pkg_name[i] = '\0';
            break;
        }
    }

    if (!is_valid_pkg_name(pkg_name)) {
        fprintf(stderr, "Security Error: Invalid package name derived from '%s'.\n", filepath);
        return -1;
    }

    char list_path[512];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg_name);

    if (access(list_path, F_OK) == 0) {
        fprintf(stderr, "Error: Package '%s' is already installed.\n", pkg_name);
        fprintf(stderr, "You must remove it first using 'bpm remove %s'\n", pkg_name);
        return -1;
    }

    printf("Installing local package '%s' from %s...\n", pkg_name, filepath);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -tzf \"%s\" > \"%s\"", filepath, list_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Failed to index contents of '%s'.\n", filepath);
        return -1;
    }
    chmod(list_path, 0600);

    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C /", filepath);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Extraction failed for '%s'.\n", filepath);
        unlink(list_path);
        return -1;
    }

    run_post_install(pkg_name);
    update_ldconfig();

    printf("%s installed successfully from local package.\n", pkg_name);
    return 0;
}

int install_package(const char *pkg_or_file) {
    if (strstr(pkg_or_file, ".bpm") != NULL || strchr(pkg_or_file, '/') != NULL) {
        if (access(pkg_or_file, R_OK) == 0) {
            return install_local_package(pkg_or_file);
        } else {
            fprintf(stderr, "Error: Local file '%s' not found or unreadable.\n", pkg_or_file);
            return -1;
        }
    }

    const char *pkg = pkg_or_file;
    if (!is_valid_pkg_name(pkg)) {
        fprintf(stderr, "Security Error: Invalid package name '%s'.\n", pkg);
        return -1;
    }

    char list_path[512];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg);

    if (access(list_path, F_OK) == 0) {
        fprintf(stderr, "Error: Package '%s' is already installed.\n", pkg);
        return -1;
    }

    char index_path[512];
    snprintf(index_path, sizeof(index_path), "%s/INDEX", DB_DIR);
    FILE *f = fopen(index_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Missing INDEX. Run 'bpm update' first.\n");
        return -1;
    }

    char line[1024], entry_name[128], version[64], expected_hash[128], deps[256] = {0};
    int found = 0;

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

    if (strlen(deps) > 0 && strcmp(deps, "-") != 0) {
        printf("Resolving dependencies for %s: [%s]\n", pkg, deps);
        char *dep_token = strtok(deps, ",");
        while (dep_token != NULL) {
            if (!is_valid_pkg_name(dep_token)) {
                fprintf(stderr, "Security Error: Malicious dependency name detected: %s\n", dep_token);
                return -1;
            }
            char dep_list[512];
            snprintf(dep_list, sizeof(dep_list), "%s/%s.list", INSTALLED_DIR, dep_token);
            if (access(dep_list, F_OK) != 0) {
                if (install_package(dep_token) != 0) return -1;
            }
            dep_token = strtok(NULL, ",");
        }
    }

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

    char calc_hash[65];
    calculate_file_sha256(cache_path, calc_hash);

    if (strcmp(calc_hash, expected_hash) != 0) {
        fprintf(stderr, "Security Alert: SHA-256 Checksum mismatch for %s!\n", file_name);
        unlink(cache_path);
        return -1;
    }

    printf("Installing %s v%s...\n", pkg, version);
    char cmd[1024];

    snprintf(cmd, sizeof(cmd), "tar -tzf \"%s\" > \"%s\"", cache_path, list_path);
    system(cmd);
    chmod(list_path, 0600);

    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C /", cache_path);
    system(cmd);

    unlink(cache_path);
    run_post_install(pkg);
    update_ldconfig();

    printf("%s v%s installed successfully.\n", pkg, version);
    return 0;
}

void cmd_remove(const char *pkg) {
    if (!is_valid_pkg_name(pkg)) {
        fprintf(stderr, "Security Error: Invalid package name.\n");
        exit(EXIT_FAILURE);
    }

    char list_path[512];
    snprintf(list_path, sizeof(list_path), "%s/%s.list", INSTALLED_DIR, pkg);

    FILE *f = fopen(list_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Package '%s' is not installed.\n", pkg);
        exit(EXIT_FAILURE);
    }

    printf("Removing %s...\n", pkg);
    char line[1024], abs_path[2048];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        if (!is_safe_path(line)) {
            fprintf(stderr, "Security Warning: Skipped unsafe path during removal: %s\n", line);
            continue;
        }

        snprintf(abs_path, sizeof(abs_path), "/%s", line);
        unlink(abs_path);
    }
    fclose(f);

    unlink(list_path);
    update_ldconfig();
    printf("%s removed successfully.\n", pkg);
}

void cmd_list(void) {
    DIR *d = opendir(INSTALLED_DIR);
    if (!d) return;
    struct dirent *dir;
    printf("Installed packages:\n");
    while ((dir = readdir(d)) != NULL) {
        char *ext = strstr(dir->d_name, ".list");
        if (ext) {
            *ext = '\0';
            printf(" - %s\n", dir->d_name);
        }
    }
    closedir(d);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("BiuiOS Package Manager (C Native Port)\n");
        printf("Usage: bpm {add <url>|update|install <pkg|file.bpm>|remove <pkg>|list}\n");
        return 0;
    }

    umask(0077);

    if (strcmp(argv[1], "list") == 0) {
        cmd_list();
        return 0;
    }

    require_root();
    mkdir_secure(INSTALLED_DIR);
    mkdir_secure(CACHE_DIR);

    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        FILE *f = fopen(REPO_FILE, "w");
        if (f) {
            fprintf(f, "%s\n", argv[2]);
            fclose(f);
            chmod(REPO_FILE, 0600);
        }
    } else if (strcmp(argv[1], "update") == 0) {
        cmd_update();
    } else if (strcmp(argv[1], "install") == 0 && argc >= 3) {
        install_package(argv[2]);
    } else if (strcmp(argv[1], "remove") == 0 && argc >= 3) {
        cmd_remove(argv[2]);
    }

    return 0;
}
