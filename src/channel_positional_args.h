/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BS_CHANNEL_positional_ARGS_H
#define BS_CHANNEL_positional_ARGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  double att;
  double attextra;
  double distance_exp;
  char  *position_stream_path;
} ch_positional_args_t;

void channel_positional_argparse(int argc, char *argv[], ch_positional_args_t *args);

#ifdef __cplusplus
}
#endif

#endif
