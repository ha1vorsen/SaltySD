#ifndef SALTYSD_PLGLDR_H
#define SALTYSD_PLGLDR_H

#define PLUGIN_PATH_MAX 256
#define PLGLDR_PATH_REJECTED ((int)0xE0000002)

int plgldr_plugin_path(char path[PLUGIN_PATH_MAX]);
int plgldr_plugin_path_valid(const char *path);
int plgldr_plugin_dir(char path[PLUGIN_PATH_MAX]);

#endif
