/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BS_CHANNEL_MULTIATT_ARGS_H
#define BS_CHANNEL_MULTIATT_ARGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  double att;
  double attextra;
  char  *matrix_file_name;
} ch_multiatt_args_t;

void channel_multiatt_argparse(int argc, char *argv[], ch_multiatt_args_t *args);

#ifdef __cplusplus
}
#endif

#endif
