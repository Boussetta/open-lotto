# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env sh

set -eu

usage() {
    cat <<'EOF'
Usage:
  scripts/generate_plugin.sh \
    --name plugin_slug \
    --display-name "Game Name" \
    --main-count N --main-min N --main-max N \
    [--extra-count N --extra-min N --extra-max N] \
    [--output-dir DIR]

Generates:
  plugin_slug.c
  test_plugin_slug.c
  Makefile
EOF
}

require_value() {
    option_name="$1"
    option_value="$2"
    if [ -z "$option_value" ]; then
        echo "Missing value for $option_name" >&2
        exit 1
    fi
}

plugin_name=""
display_name=""
main_count=""
main_min=""
main_max=""
extra_count="0"
extra_min="0"
extra_max="0"
output_dir="."

while [ "$#" -gt 0 ]; do
    case "$1" in
        --name)
            shift
            require_value "--name" "${1:-}"
            plugin_name="$1"
            ;;
        --display-name)
            shift
            require_value "--display-name" "${1:-}"
            display_name="$1"
            ;;
        --main-count)
            shift
            require_value "--main-count" "${1:-}"
            main_count="$1"
            ;;
        --main-min)
            shift
            require_value "--main-min" "${1:-}"
            main_min="$1"
            ;;
        --main-max)
            shift
            require_value "--main-max" "${1:-}"
            main_max="$1"
            ;;
        --extra-count)
            shift
            require_value "--extra-count" "${1:-}"
            extra_count="$1"
            ;;
        --extra-min)
            shift
            require_value "--extra-min" "${1:-}"
            extra_min="$1"
            ;;
        --extra-max)
            shift
            require_value "--extra-max" "${1:-}"
            extra_max="$1"
            ;;
        --output-dir)
            shift
            require_value "--output-dir" "${1:-}"
            output_dir="$1"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

require_value "--name" "$plugin_name"
require_value "--display-name" "$display_name"
require_value "--main-count" "$main_count"
require_value "--main-min" "$main_min"
require_value "--main-max" "$main_max"

mkdir -p "$output_dir"

plugin_file="$output_dir/$plugin_name.c"
test_file="$output_dir/test_$plugin_name.c"
makefile_path="$output_dir/Makefile"

for generated_file in "$plugin_file" "$test_file" "$makefile_path"; do
    if [ -e "$generated_file" ]; then
        echo "Refusing to overwrite existing file: $generated_file" >&2
        exit 1
    fi
done

cat > "$plugin_file" <<EOF
#include "combogen.h"
#include "lottery_plugin.h"

static const LotteryInfo INFO = {
    .main_count = $main_count,
    .main_min = $main_min,
    .main_max = $main_max,
    .extra_count = $extra_count,
    .extra_min = $extra_min,
    .extra_max = $extra_max,
};

const LotteryInfo *plugin_get_info(void)
{
    return &INFO;
}

const char *plugin_get_name(void)
{
    return "$display_name";
}

void plugin_draw(LotteryResult *out, draw_event_callback cb)
{
    generate_draw(INFO.main_count, INFO.main_min, INFO.main_max, INFO.extra_count, INFO.extra_min,
                  INFO.extra_max, out, cb);
}
EOF

cat > "$test_file" <<EOF
#include "plugin_loader.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg)                                                                        \
    do                                                                                          \
    {                                                                                           \
        if (!(cond))                                                                            \
        {                                                                                       \
            fprintf(stderr, "FAIL: %s\\n", msg);                                              \
            return 1;                                                                           \
        }                                                                                       \
    } while (0)

static int test_generated_plugin(const char *path)
{
    LoadedPlugin *plugin = load_plugin(path);
    CHECK(plugin != NULL, "generated plugin loads successfully");

    if (plugin)
    {
        CHECK(plugin->info.main_count == $main_count, "main_count matches template");
        CHECK(plugin->info.extra_count == $extra_count, "extra_count matches template");
        unload_plugin(plugin);
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: ./test_$plugin_name ./lib$plugin_name.so\\n");
        return 1;
    }

    if (test_generated_plugin(argv[1]) != 0)
        return 1;

    printf("Generated plugin smoke test passed\\n");
    return 0;
}
EOF

cat > "$makefile_path" <<'EOF'
OPEN_LOTTO_ROOT ?= $(abspath $(CURDIR)/..)
CC ?= cc
PLUGIN_NAME := __PLUGIN_NAME__
PLUGIN_SO := lib$(PLUGIN_NAME).so
CFLAGS ?= -Wall -Wextra -Wpedantic -Werror -fPIC -I$(OPEN_LOTTO_ROOT)/include
LDFLAGS ?= -shared -lm

all: $(PLUGIN_SO)

$(PLUGIN_SO): __PLUGIN_NAME__.c
	$(CC) $(CFLAGS) -o $@ __PLUGIN_NAME__.c $(LDFLAGS)

test: $(PLUGIN_SO)
	$(CC) -Wall -Wextra -Wpedantic -Werror -I$(OPEN_LOTTO_ROOT)/include \
	    -o test___PLUGIN_NAME__ test___PLUGIN_NAME__.c \
	    $(OPEN_LOTTO_ROOT)/src/plugin_loader.c \
	    $(OPEN_LOTTO_ROOT)/src/combogen.c \
	    $(OPEN_LOTTO_ROOT)/src/random_seed.c \
	    $(OPEN_LOTTO_ROOT)/src/log.c -ldl -lm -Wl,-export-dynamic
	./test___PLUGIN_NAME__ ./$(PLUGIN_SO)

validate: $(PLUGIN_SO)
	$(OPEN_LOTTO_ROOT)/build/open-lotto-plugin-validator ./$(PLUGIN_SO)

clean:
	rm -f $(PLUGIN_SO) test___PLUGIN_NAME__
EOF

sed -i "s/__PLUGIN_NAME__/$plugin_name/g" "$makefile_path"

echo "Generated plugin scaffold in $output_dir"