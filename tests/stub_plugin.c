/**
 * @file stub_plugin.c
 * @brief Stub shared library with no lottery plugin symbols.
 *
 * Used by test_plugin_loader to verify that load_plugin() correctly rejects
 * a .so that does not export the required plugin_get_info / plugin_get_name /
 * plugin_draw symbols.
 */

/* Intentionally exports nothing matching the plugin contract. */
void stub_dummy(void) {}
