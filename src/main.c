#include "alife/alife.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIFE_VERSION "1.0.0"
#define ERROR_SIZE 512U

typedef struct {
    const char *config_path;
    const char *checkpoint_path;
    bool checkpoint_was_overridden;
} CommandOptions;

static void print_usage(FILE *output) {
    (void)fprintf(output,
        "Usage:\n"
        "  alife run --config FILE [--checkpoint FILE]\n"
        "  alife resume --config FILE --checkpoint FILE\n"
        "  alife inspect --checkpoint FILE\n"
        "  alife --help\n"
        "  alife --version\n\n"
        "Commands:\n"
        "  run      Start a deterministic experiment.\n"
        "  resume   Resume an experiment from a matching checkpoint.\n"
        "  inspect  Print checkpoint metadata as JSON.\n\n"
        "Options:\n"
        "  --config FILE      Read experiment settings from FILE.\n"
        "  --checkpoint FILE  Read or write the versioned checkpoint FILE.\n");
}

static bool parse_options(int argc, char **argv, int first,
                          CommandOptions *options, char *error,
                          size_t error_size) {
    int i;

    memset(options, 0, sizeof(*options));
    for (i = first; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 ||
            strcmp(argv[i], "--checkpoint") == 0) {
            bool config_option = strcmp(argv[i], "--config") == 0;
            if (i + 1 >= argc) {
                (void)snprintf(error, error_size, "%s requires a file path.", argv[i]);
                return false;
            }
            ++i;
            if (config_option) {
                if (options->config_path != NULL) {
                    (void)snprintf(error, error_size, "--config can be used only once.");
                    return false;
                }
                options->config_path = argv[i];
            } else {
                if (options->checkpoint_path != NULL) {
                    (void)snprintf(error, error_size,
                                   "--checkpoint can be used only once.");
                    return false;
                }
                options->checkpoint_path = argv[i];
                options->checkpoint_was_overridden = true;
            }
        } else {
            (void)snprintf(error, error_size, "Unknown option: %s.", argv[i]);
            return false;
        }
    }
    return true;
}

static bool copy_checkpoint_override(AlifeConfig *config, const char *path,
                                     char *error, size_t error_size) {
    int result;

    if (path == NULL) {
        return true;
    }
    result = snprintf(config->checkpoint_path, sizeof(config->checkpoint_path),
                      "%s", path);
    if (result < 0 || (size_t)result >= sizeof(config->checkpoint_path)) {
        (void)snprintf(error, error_size, "The checkpoint path is too long.");
        return false;
    }
    return true;
}

static int run_command(const CommandOptions *options, bool resume) {
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[ERROR_SIZE] = {0};
    uint64_t hash;
    bool okay;

    if (options->config_path == NULL) {
        (void)fprintf(stderr, "error: --config is required.\n");
        return EXIT_FAILURE;
    }
    if (resume && options->checkpoint_path == NULL) {
        (void)fprintf(stderr, "error: resume requires --checkpoint.\n");
        return EXIT_FAILURE;
    }
    if (!alife_config_load(&config, options->config_path, error, sizeof(error))) {
        (void)fprintf(stderr, "error: %s\n", error);
        return EXIT_FAILURE;
    }
    if (!copy_checkpoint_override(&config, options->checkpoint_path,
                                  error, sizeof(error)) ||
        !alife_config_validate(&config, error, sizeof(error))) {
        (void)fprintf(stderr, "error: %s\n", error);
        return EXIT_FAILURE;
    }
    if (resume) {
        okay = alife_world_load(&world, &config, options->checkpoint_path,
                                error, sizeof(error));
    } else {
        okay = alife_world_init(&world, &config, error, sizeof(error));
    }
    if (!okay) {
        (void)fprintf(stderr, "error: %s\n", error);
        return EXIT_FAILURE;
    }
    if (!alife_world_run(&world, error, sizeof(error))) {
        (void)fprintf(stderr, "error: %s\n", error);
        alife_world_destroy(&world);
        return EXIT_FAILURE;
    }
    if (!resume && options->checkpoint_was_overridden &&
        (config.checkpoint_interval == 0U ||
         world.tick % config.checkpoint_interval != 0U) &&
        !alife_world_save(&world, config.checkpoint_path,
                          error, sizeof(error))) {
        (void)fprintf(stderr, "error: %s\n", error);
        alife_world_destroy(&world);
        return EXIT_FAILURE;
    }
    hash = alife_world_hash(&world);
    (void)printf("final_tick=%" PRIu64 " population=%zu state_hash=%016" PRIx64 "\n",
                 world.tick, world.count, hash);
    alife_world_destroy(&world);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    CommandOptions options;
    char error[ERROR_SIZE] = {0};

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        (void)printf("alife %s\n", ALIFE_VERSION);
        return EXIT_SUCCESS;
    }
    if (argc < 2) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }
    if (!parse_options(argc, argv, 2, &options, error, sizeof(error))) {
        (void)fprintf(stderr, "error: %s\n", error);
        print_usage(stderr);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "run") == 0) {
        return run_command(&options, false);
    }
    if (strcmp(argv[1], "resume") == 0) {
        return run_command(&options, true);
    }
    if (strcmp(argv[1], "inspect") == 0) {
        if (options.checkpoint_path == NULL || options.config_path != NULL) {
            (void)fprintf(stderr,
                          "error: inspect requires only --checkpoint FILE.\n");
            return EXIT_FAILURE;
        }
        if (!alife_checkpoint_inspect(options.checkpoint_path, stdout,
                                      error, sizeof(error))) {
            (void)fprintf(stderr, "error: %s\n", error);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    (void)fprintf(stderr, "error: Unknown command: %s.\n", argv[1]);
    print_usage(stderr);
    return EXIT_FAILURE;
}
