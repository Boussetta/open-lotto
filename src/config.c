/**
 * @file config.c
 * @brief Implementation of configuration file support (.lottorc).
 *
 * SPDX-License-Identifier: MIT
 */

#include "config.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* =====================================================================
   Helper Functions
   ===================================================================== */

/**
 * @brief Get home directory path.
 * @return Home directory (from $HOME env var or /root as fallback)
 */
static const char *get_home_dir(void)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
    {
        home = "/root"; /* Fallback for root user */
    }
    return home;
}

/**
 * @brief Trim leading and trailing whitespace from a string.
 * @param str String to trim (modified in-place)
 */
static void trim_whitespace(char *str)
{
    if (!str)
        return;

    /* Trim leading whitespace */
    size_t start = 0;
    while (str[start] && (str[start] == ' ' || str[start] == '\t'))
    {
        start++;
    }

    /* Trim trailing whitespace */
    size_t end = strlen(str);
    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' || str[end - 1] == '\n'))
    {
        end--;
    }

    /* Move trimmed content to front and null-terminate */
    if (start > 0)
    {
        memmove(str, str + start, end - start);
    }
    str[end - start] = '\0';
}

/**
 * @brief Parse a single key=value line from config.
 * @param line Line to parse
 * @param key Output: key name
 * @param value Output: value (must be freed by caller)
 * @return 1 if successfully parsed, 0 if line is comment/empty
 */
static int parse_config_line(const char *line, char *key, char **value)
{
    if (!line || line[0] == '\0' || line[0] == '#' || line[0] == ';')
    {
        return 0; /* Comment or empty line */
    }

    const char *sep = strchr(line, '=');
    if (!sep)
    {
        return 0; /* No '=' found */
    }

    size_t key_len = (size_t)(sep - line);
    if (key_len > 255)
    {
        return 0;
    }

    /* Copy and trim key */
    strncpy(key, line, key_len);
    key[key_len] = '\0';
    trim_whitespace(key);

    /* Copy and trim value */
    *value = strdup(sep + 1);
    if (*value)
    {
        trim_whitespace(*value);
    }

    return (*value != NULL && key[0] != '\0');
}

/* =====================================================================
   Public API
   ===================================================================== */

int config_load_file(LoCalConfig *config, const char *path)
{
    if (!config || !path)
    {
        return 1;
    }

    /* Initialize config to defaults */
    memset(config, 0, sizeof(LoCalConfig));
    config->draws = -1; /* -1 means "not set" */

    FILE *f = fopen(path, "r");
    if (!f)
    {
        /* File doesn't exist; return with defaults */
        log_debug("Config file '%s' not found; using defaults", path);
        return 0;
    }

    char line[512];
    int in_defaults_section = 0;

    while (fgets(line, sizeof(line), f))
    {
        /* Remove trailing newline */
        if (line[strlen(line) - 1] == '\n')
        {
            line[strlen(line) - 1] = '\0';
        }

        /* Check for section header */
        if (line[0] == '[')
        {
            if (strncmp(line, "[defaults]", 10) == 0)
            {
                in_defaults_section = 1;
            }
            else
            {
                in_defaults_section = 0;
            }
            continue;
        }

        if (!in_defaults_section)
        {
            continue; /* Skip lines outside [defaults] section */
        }

        char key[256] = {0};
        char *value = NULL;

        if (parse_config_line(line, key, &value))
        {
            log_debug("Config: %s = %s", key, value);

            if (strcmp(key, "game") == 0)
            {
                config->game = value;
            }
            else if (strcmp(key, "draws") == 0)
            {
                char *end = NULL;
                long draws_val = strtol(value, &end, 10);
                if (end != value && *end == '\0' && draws_val > 0)
                {
                    config->draws = (int)draws_val;
                }
                else
                {
                    log_warn("Config: invalid draws value '%s', ignoring", value);
                }
                free(value);
            }
            else if (strcmp(key, "export") == 0)
            {
                config->export_format = value;
            }
            else if (strcmp(key, "output") == 0)
            {
                config->output_file = value;
            }
            else if (strcmp(key, "verbose") == 0)
            {
                config->verbose_level = value;
            }
            else if (strcmp(key, "gui") == 0)
            {
                config->gui_mode = value;
            }
            else
            {
                log_warn("Unknown config key: %s", key);
                free(value);
            }
        }
        else
        {
            free(value); /* parse may have allocated value even on failure */
        }
    }

    fclose(f);
    log_info("Loaded config from %s", path);
    return 0;
}

int config_load_lottorc(LoCalConfig *config)
{
    if (!config)
    {
        return 1;
    }

    /* Build ~/.lottorc path */
    const char *home = get_home_dir();
    size_t path_len = strlen(home) + strlen("/.lottorc") + 1;
    char path[512];
    if (path_len > sizeof(path))
    {
        log_error("Home directory path too long");
        return 1;
    }

    snprintf(path, sizeof(path), "%s/.lottorc", home);
    return config_load_file(config, path);
}

void config_free(LoCalConfig *config)
{
    if (!config)
    {
        return;
    }

    free(config->game);
    free(config->export_format);
    free(config->output_file);
    free(config->verbose_level);
    free(config->gui_mode);

    memset(config, 0, sizeof(LoCalConfig));
}

void config_print(const LoCalConfig *config)
{
    if (!config)
    {
        return;
    }

    printf("Configuration:\n");
    printf("  game: %s\n", config->game ? config->game : "(not set)");
    printf("  draws: %d\n", config->draws >= 0 ? config->draws : -1);
    printf("  export: %s\n", config->export_format ? config->export_format : "(not set)");
    printf("  output: %s\n", config->output_file ? config->output_file : "(not set)");
    printf("  verbose: %s\n", config->verbose_level ? config->verbose_level : "(not set)");
    printf("  gui: %s\n", config->gui_mode ? config->gui_mode : "(not set)");
}

int config_lottorc_exists(void)
{
    const char *home = get_home_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/.lottorc", home);
    return (access(path, R_OK) == 0) ? 1 : 0;
}
