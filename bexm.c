/*
 * bexm - bash extension manager
 *
 * Manages ~/.bashrc.d/available/  (your module files, any name, no forced extension)
 * and    ~/.bashrc.d/enabled.conf (plain text: "<order> <name>", '#' prefix = disabled)
 *
 * The config format is the source of truth and is meant to be human-readable/editable:
 *
 *   # bexm enabled modules - format: <order> <name>
 *   # lines starting with # are disabled
 *   10 env
 *   20 alias
 *   # 25 dashboard
 *   30 ps1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_LINES 4096
#define MAX_LINE_LEN 512
#define MAX_PATH_LEN 2048

typedef struct {
    int order;
    int disabled;
    char name[MAX_LINE_LEN];
} Entry;

static char home[MAX_PATH_LEN];
static char bashrc_dir[MAX_PATH_LEN];
static char avail_dir[MAX_PATH_LEN];
static char conf_path[MAX_PATH_LEN];

static void init_paths(void) {
    const char *h = getenv("HOME");
    if (!h) { fprintf(stderr, "bexm: HOME is not set\n"); exit(1); }
    snprintf(home, sizeof(home), "%s", h);
    snprintf(bashrc_dir, sizeof(bashrc_dir), "%s/.bashrc.d", home);
    snprintf(avail_dir, sizeof(avail_dir), "%s/available", bashrc_dir);
    snprintf(conf_path, sizeof(conf_path), "%s/enabled.conf", bashrc_dir);
}

static void ensure_dirs(void) {
    mkdir(bashrc_dir, 0755);
    mkdir(avail_dir, 0755);
    FILE *f = fopen(conf_path, "a");
    if (f) fclose(f);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) { *end = 0; end--; }
    return s;
}

/* Parse one config line into an Entry. Returns 1 on success, 0 if blank/malformed (skip). */
static int parse_line(const char *raw, Entry *e) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", raw);
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;

    char *t = trim(buf);
    if (*t == 0) return 0;

    int disabled = 0;
    if (t[0] == '#') {
        disabled = 1;
        t++;
        t = trim(t);
    }
    if (*t == 0) return 0;

    char *endptr;
    long num = strtol(t, &endptr, 10);
    if (endptr == t) return 0; /* no leading number -> not a module line (e.g. header comment) */

    char *name = trim(endptr);
    if (*name == 0) return 0;

    e->order = (int)num;
    e->disabled = disabled;
    snprintf(e->name, sizeof(e->name), "%s", name);
    return 1;
}

static int read_entries(Entry *entries) {
    FILE *f = fopen(conf_path, "r");
    if (!f) return 0;
    char line[MAX_LINE_LEN];
    int count = 0;
    while (count < MAX_LINES && fgets(line, sizeof(line), f)) {
        Entry e;
        if (parse_line(line, &e)) entries[count++] = e;
    }
    fclose(f);
    return count;
}

static int cmp_entry(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    if (ea->order != eb->order) return ea->order - eb->order;
    return strcmp(ea->name, eb->name);
}

static void write_entries(Entry *entries, int count) {
    qsort(entries, count, sizeof(Entry), cmp_entry);
    FILE *f = fopen(conf_path, "w");
    if (!f) { fprintf(stderr, "bexm: cannot write %s\n", conf_path); exit(1); }
    fprintf(f, "# bexm enabled modules - format: <order> <name>\n");
    fprintf(f, "# lines starting with # are disabled\n");
    for (int i = 0; i < count; i++) {
        if (entries[i].disabled)
            fprintf(f, "# %d %s\n", entries[i].order, entries[i].name);
        else
            fprintf(f, "%d %s\n", entries[i].order, entries[i].name);
    }
    fclose(f);
}

static int find_entry(Entry *entries, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcmp(entries[i].name, name) == 0) return i;
    return -1;
}

static int module_exists(const char *name) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", avail_dir, name);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int max_order(Entry *entries, int count) {
    int m = 0;
    for (int i = 0; i < count; i++) if (entries[i].order > m) m = entries[i].order;
    return m;
}

static void cmd_enable(const char *name, const char *order_str) {
    if (!module_exists(name)) {
        fprintf(stderr, "bexm: no such module '%s' in available/\n", name);
        exit(1);
    }
    Entry entries[MAX_LINES];
    int count = read_entries(entries);
    int idx = find_entry(entries, count, name);
    int order = 0;
    if (order_str) order = atoi(order_str);

    if (idx >= 0) {
        if (order_str) entries[idx].order = order;
        if (!entries[idx].disabled && !order_str) {
            printf("bexm: '%s' already enabled (order %d)\n", name, entries[idx].order);
            return;
        }
        entries[idx].disabled = 0;
    } else {
        if (!order_str) order = max_order(entries, count) + 10;
        entries[count].order = order;
        entries[count].disabled = 0;
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", name);
        count++;
    }
    write_entries(entries, count);
    int final_order = order_str ? order : entries[find_entry(entries, count, name)].order;
    printf("enabled: %s (order %d)\n", name, final_order);
}

static void cmd_disable(const char *name) {
    Entry entries[MAX_LINES];
    int count = read_entries(entries);
    int idx = find_entry(entries, count, name);
    if (idx < 0 || entries[idx].disabled) {
        fprintf(stderr, "bexm: '%s' is not currently enabled\n", name);
        exit(1);
    }
    entries[idx].disabled = 1;
    write_entries(entries, count);
    printf("disabled: %s\n", name);
}

static void cmd_move(const char *name, const char *order_str) {
    Entry entries[MAX_LINES];
    int count = read_entries(entries);
    int idx = find_entry(entries, count, name);
    if (idx < 0) {
        fprintf(stderr, "bexm: '%s' is not tracked yet (use 'bexm enable' first)\n", name);
        exit(1);
    }
    int new_order = atoi(order_str);
    entries[idx].order = new_order;
    write_entries(entries, count);
    printf("moved: %s -> order %d\n", name, new_order);
}

static void cmd_list(void) {
    Entry entries[MAX_LINES];
    int count = read_entries(entries);

    DIR *d = opendir(avail_dir);
    if (!d) { fprintf(stderr, "bexm: cannot open %s\n", avail_dir); exit(1); }

    char names[MAX_LINES][MAX_LINE_LEN];
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", avail_dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && n < MAX_LINES) {
            snprintf(names[n], sizeof(names[n]), "%s", ent->d_name);
            n++;
        }
    }
    closedir(d);

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(names[i], names[j]) > 0) {
                char tmp[MAX_LINE_LEN];
                strcpy(tmp, names[i]);
                strcpy(names[i], names[j]);
                strcpy(names[j], tmp);
            }

    printf("-- available (%d) --\n", n);
    for (int i = 0; i < n; i++) {
        int idx = find_entry(entries, count, names[i]);
        if (idx >= 0 && !entries[idx].disabled)
            printf("[x] %-24s order %d\n", names[i], entries[idx].order);
        else if (idx >= 0 && entries[idx].disabled)
            printf("[-] %-24s order %d (disabled)\n", names[i], entries[idx].order);
        else
            printf("[ ] %s\n", names[i]);
    }
}

static void cmd_order(void) {
    Entry entries[MAX_LINES];
    int count = read_entries(entries);
    qsort(entries, count, sizeof(Entry), cmp_entry);
    printf("-- load order --\n");
    for (int i = 0; i < count; i++) {
        if (entries[i].disabled)
            printf("%4d  %-24s (disabled)\n", entries[i].order, entries[i].name);
        else
            printf("%4d  %s\n", entries[i].order, entries[i].name);
    }
}

static void cmd_new(const char *name) {
    if (module_exists(name)) {
        fprintf(stderr, "bexm: '%s' already exists in available/\n", name);
        exit(1);
    }
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", avail_dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "bexm: could not create %s\n", path); exit(1); }
    fclose(f);
    printf("created: available/%s\n", name);
}

static void cmd_edit(void) {
    const char *editor = getenv("EDITOR");
    if (!editor || !*editor) editor = "vi";
    pid_t pid = fork();
    if (pid == 0) {
        execlp(editor, editor, conf_path, (char *)NULL);
        perror("bexm: exec failed");
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("bexm: fork failed");
    }
}

#define BEXM_MARKER_BEGIN "# --- bexm managed block (loader + wrapper) ---"
#define BEXM_MARKER_END   "# --- end bexm managed block ---"

static const char *bexm_block =
"\n" BEXM_MARKER_BEGIN "\n"
"BEXM_DIR=\"$HOME/.bashrc.d\"\n"
"BEXM_CONF=\"$BEXM_DIR/enabled.conf\"\n"
"BEXM_AVAIL=\"$BEXM_DIR/available\"\n"
"\n"
"if [ -f \"$BEXM_CONF\" ]; then\n"
"  while IFS= read -r bexm_line; do\n"
"    bexm_order=\"${bexm_line%% *}\"\n"
"    bexm_name=\"${bexm_line#* }\"\n"
"    bexm_file=\"$BEXM_AVAIL/$bexm_name\"\n"
"    if [ -r \"$bexm_file\" ]; then\n"
"      source \"$bexm_file\"\n"
"    else\n"
"      echo \"bashrc: missing module '$bexm_name' (from enabled.conf)\" >&2\n"
"    fi\n"
"  done < <(grep -Ev '^[[:space:]]*(#|$)' \"$BEXM_CONF\" | sort -n -k1,1)\n"
"fi\n"
"unset BEXM_DIR BEXM_CONF BEXM_AVAIL bexm_line bexm_order bexm_name bexm_file\n"
"\n"
"bexm() {\n"
"  command bexm \"$@\"\n"
"  local status=$?\n"
"  case \"$1\" in\n"
"    enable|disable|move)\n"
"      [ $status -eq 0 ] && source \"$HOME/.bashrc\"\n"
"      ;;\n"
"  esac\n"
"  return $status\n"
"}\n"
BEXM_MARKER_END "\n";

/* Does ~/.bashrc already contain our managed block? */
static int bashrc_has_block(const char *bashrc_path) {
    FILE *f = fopen(bashrc_path, "r");
    if (!f) return 0;
    char line[MAX_LINE_LEN];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, BEXM_MARKER_BEGIN)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void cmd_init(void) {
    char bashrc_path[MAX_PATH_LEN];
    snprintf(bashrc_path, sizeof(bashrc_path), "%s/.bashrc", home);

    if (bashrc_has_block(bashrc_path)) {
        printf("bexm: already installed in %s (nothing to do)\n", bashrc_path);
        return;
    }

    FILE *f = fopen(bashrc_path, "a");
    if (!f) {
        fprintf(stderr, "bexm: could not open %s for writing\n", bashrc_path);
        exit(1);
    }
    fputs(bexm_block, f);
    fclose(f);

    printf("installed bexm loader + wrapper into %s\n", bashrc_path);
    printf("run this once now (new shells will pick it up automatically):\n\n");
    printf("  source ~/.bashrc\n\n");
    printf("after that, 'bexm enable/disable/move' will auto-reload for you.\n");
}

static void print_usage(void) {
    printf(
"bexm - bash extension manager\n\n"
"  bexm enable  <name> [order]     enable a module (optionally set/override its order)\n"
"  bexm disable <name>             disable a module (kept in config, just commented out)\n"
"  bexm move    <name> <order>     change a module's load order\n"
"  bexm list                       show available modules and enabled state\n"
"  bexm order                      show current load order\n"
"  bexm new     <name>             create an empty module file in available/\n"
"  bexm edit                       open enabled.conf in $EDITOR\n"
"  bexm init                       install the loader + auto-reload wrapper into ~/.bashrc\n\n"
"Config lives at ~/.bashrc.d/enabled.conf and is plain text -- edit it by hand\n"
"any time, bexm just automates the common edits.\n\n"
"Note: bexm itself is a separate process and can never reload your current\n"
"shell directly (a child process can't touch its parent's environment).\n"
"Run 'bexm init' once to install a shell-function wrapper into ~/.bashrc that\n"
"works around this -- after that, enable/disable/move auto-reload for you.\n"
);
}

int main(int argc, char **argv) {
    init_paths();
    ensure_dirs();

    if (argc < 2) { print_usage(); return 1; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "enable") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: bexm enable <name> [order]\n"); return 1; }
        cmd_enable(argv[2], argc >= 4 ? argv[3] : NULL);
    } else if (strcmp(cmd, "disable") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: bexm disable <name>\n"); return 1; }
        cmd_disable(argv[2]);
    } else if (strcmp(cmd, "move") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: bexm move <name> <order>\n"); return 1; }
        cmd_move(argv[2], argv[3]);
    } else if (strcmp(cmd, "list") == 0) {
        cmd_list();
    } else if (strcmp(cmd, "order") == 0) {
        cmd_order();
    } else if (strcmp(cmd, "new") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: bexm new <name>\n"); return 1; }
        cmd_new(argv[2]);
    } else if (strcmp(cmd, "edit") == 0) {
        cmd_edit();
    } else if (strcmp(cmd, "init") == 0) {
        cmd_init();
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "help") == 0) {
        print_usage();
    } else {
        fprintf(stderr, "bexm: unknown command '%s'\n\n", cmd);
        print_usage();
        return 1;
    }
    return 0;
}
