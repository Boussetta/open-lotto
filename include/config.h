/**
 * @file config.h
 * @brief Configuration file support (.lottorc) for persistent CLI defaults.
 *
 * The config module allows users to set default values for CLI arguments
 * via a ~/.lottorc configuration file, avoiding repetitive typing for
 * frequently-used options.
 *
 * Config file format (INI-style):
 *
 *   [defaults]
 *   game = Lotto 6aus49
 *   draws = 10
 *   export = csv
 *   output = results.csv
 *   verbose = INFO
 *   gui = 2D
 *
 * Command-line arguments always override config file settings.
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief Configuration structure for CLI defaults.
 */
typedef struct
{
    char *game;          /**< Default lottery game name */
    int draws;           /**< Default number of draws (-1 = not set) */
    char *export_format; /**< Default export format ("csv" or "json", NULL = not set) */
    char *output_file;   /**< Default output filename (NULL = not set) */
    char *verbose_level; /**< Default log level (NULL = not set) */
    char *gui_mode;      /**< Default GUI mode ("2D" or "3D", NULL = not set) */
    char *dark_mode;     /**< Dark mode preference ("on", "off", or "auto", NULL = not set) */
} LoCalConfig;

/**
 * @brief Load configuration from ~/.lottorc file.
 *
 * Reads the INI-style config file and populates a LoCalConfig structure.
 * If the file doesn't exist, returns an initialized config with all fields NULL/default.
 *
 * @param config Pointer to config structure to populate
 * @return 0 on success, 1 if file exists but is malformed
 */
int config_load_lottorc(LoCalConfig *config);

/**
 * @brief Load configuration from a custom path (testing/alternative config).
 *
 * @param config Pointer to config structure to populate
 * @param path Custom path to config file
 * @return 0 on success, 1 if file exists but is malformed
 */
int config_load_file(LoCalConfig *config, const char *path);

/**
 * @brief Free allocated memory in a config structure.
 *
 * @param config Pointer to config structure to free
 */
void config_free(LoCalConfig *config);

/**
 * @brief Print config values (for debugging or validation).
 *
 * @param config Pointer to config structure
 */
void config_print(const LoCalConfig *config);

/**
 * @brief Check if a config file exists at the standard location (~/.lottorc).
 *
 * @return 1 if ~/.lottorc exists and is readable, 0 otherwise
 */
int config_lottorc_exists(void);

#endif /* CONFIG_H */
