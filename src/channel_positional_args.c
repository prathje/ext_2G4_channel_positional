/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "channel_positional_args.h"
#include "bs_tracing.h"
#include "bs_oswrap.h"
#include "bs_cmd_line_typical.h"

static char library_name[] = "positional 2G4 channel";

void component_print_post_help() {
  fprintf(stdout,"This is a non realistic channel model.\n"
    "It models NxN independent paths each with a configurable attenuation\n"
    "This attenuation can also be configured to change over time\n\n"
    "Note that parameters are processed from left to right and can be overriden\n\n"
    "For more information on the format of <att_matrix_file> and \n"
    "<att_file> please check: docs/README.txt\n"
  );
}

ch_positional_args_t *args_g;

void cmd_att_found(char * argv, int offset) {
  if ((args_g->att < -100) || (args_g->att > 100)) {
    bs_trace_error("channel: cmdarg: attenuation can only be between -100 and 100dB (%lf)\n", args_g->att);
  }
}
void cmd_attextra_found(char * argv, int offset) {
  if ((args_g->attextra < -100) || (args_g->attextra > 100)) {
    bs_trace_error("channel: cmdarg: extra attenuation can only be between -100 and 100dB (%lf)\n", args_g->attextra);
  }
}
void cmd_exp_found(char * argv, int offset) {
    if ((args_g->distance_exp < 1) || (args_g->distance_exp > 4)) {
        bs_trace_error("channel: cmdarg: distance_exp can only be between 1 and 4 (%lf) '%s'\n",
                       args_g->distance_exp, argv);
    }
}
/**
 * Check the arguments provided in the command line: set args based on it
 * or defaults, and check they are correct
 */
void channel_positional_argparse(int argc, char *argv[], ch_positional_args_t *args) {
  args_g = args;
  bs_args_struct_t args_struct[] = {
    /*manual,mandatory,switch,option,   name ,     type,   destination,               callback,      , description*/
    { false, false, false, "at"    ,"att",      'f', (void*)&args->att,      cmd_att_found,      "default attenuation in dB, used in all NxN paths if <positional_file> is not provided Default: 60dB"},
    { false, false, false, "atextra","atextra", 'f', (void*)&args->attextra, cmd_attextra_found, "Extra attenuation to be added to all paths (even if <positional_file> is provided)"},
    { false, false, false, "stream","position_stream_path", 's',(void*)&args->position_stream_path, NULL,   "Stream or file containing the positional for each device"},
    { false, false, false, "exp", "distance_exp",'f', (void*)&args->distance_exp, cmd_exp_found, "Distance exponent for the path loss (default 2)"},
    ARG_TABLE_ENDMARKER
  };

  args->att = 60;
  args->attextra = 0;
  args->position_stream_path = NULL;
  args->distance_exp = 2.0;

  char trace_prefix[50]; //it will not be used as soon as we get out of this function
  snprintf(trace_prefix,50, "channel: (positional) ");

  bs_args_override_exe_name(library_name);
  bs_args_set_trace_prefix(trace_prefix);
  bs_args_parse_all_cmd_line(argc, argv, args_struct);
}
