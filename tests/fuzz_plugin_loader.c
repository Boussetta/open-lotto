/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/plugin_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size == 0)
        return 0;

    char path[] = "/tmp/open-lotto-fuzz-plugin-XXXXXX.so";
    int fd = mkstemps(path, 3);
    if (fd < 0)
        return 0;

    ssize_t written = write(fd, data, size);
    close(fd);
    if (written < 0)
    {
        unlink(path);
        return 0;
    }

    LoadedPlugin *plugin = load_plugin(path);
    if (plugin)
        unload_plugin(plugin);

    unlink(path);
    return 0;
}
