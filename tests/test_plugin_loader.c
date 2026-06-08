/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

/**
 * @file test_plugin_loader.c
 * @brief Integration tests for the plugin loader.
 *
 * Covers:
 *   - NULL and empty path rejection
 *   - Non-existent path rejection
 *   - .so that lacks required symbols rejection
 *   - Valid plugin load and symbol availability
 *   - unload_plugin(NULL) safety
 */

#include "../include/plugin_loader.h"
#include "test.h"
#include <stddef.h>

/* Paths injected by CMake at compile time. */
#ifndef LOTTO_PLUGIN_PATH
#define LOTTO_PLUGIN_PATH "./plugins/liblotto.so"
#endif

#ifndef STUB_PLUGIN_PATH
#define STUB_PLUGIN_PATH "./plugins/libstub_plugin.so"
#endif

/* ------------------------------------------------------------------ */
static void test_error_paths(void)
{
    test_suite("Plugin loader – error paths");

    LoadedPlugin *p;

    p = load_plugin(NULL);
    assert_true(p == NULL, "load_plugin(NULL) returns NULL");

    p = load_plugin("");
    assert_true(p == NULL, "load_plugin(\"\") returns NULL");

    p = load_plugin("/nonexistent/path/plugin.so");
    assert_true(p == NULL, "load_plugin(nonexistent) returns NULL");
}

static void test_missing_symbols(void)
{
    test_suite("Plugin loader – stub with missing symbols");

    LoadedPlugin *p = load_plugin(STUB_PLUGIN_PATH);
    assert_true(p == NULL, "load_plugin(stub without symbols) returns NULL");
}

static void test_valid_plugin(void)
{
    test_suite("Plugin loader – valid plugin");

    LoadedPlugin *p = load_plugin(LOTTO_PLUGIN_PATH);
    assert_true(p != NULL, "load_plugin(liblotto.so) returns non-NULL");

    if (p)
    {
        assert_true(p->name[0] != '\0', "Loaded plugin has non-empty name");
        assert_true(p->draw != NULL, "Loaded plugin has draw function pointer");
        assert_true(p->info.main_count > 0, "Loaded plugin info has main_count > 0");
        assert_true(p->info.main_min > 0, "Loaded plugin info has main_min > 0");
        assert_true(p->info.main_max >= p->info.main_min,
                    "Loaded plugin info has main_max >= main_min");
        unload_plugin(p);
    }
}

static void test_unload_null(void)
{
    test_suite("Plugin loader – unload_plugin(NULL)");

    /* Must not crash. */
    unload_plugin(NULL);
    assert_true(1, "unload_plugin(NULL) does not crash");
}

/* ------------------------------------------------------------------ */
int main(void)
{
    test_error_paths();
    test_missing_symbols();
    test_valid_plugin();
    test_unload_null();

    test_summary();
}
