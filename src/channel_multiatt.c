/*
 * Copyright 2018 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ctype.h>
#include "bs_types.h"
#include "bs_tracing.h"
#include "bs_oswrap.h"
#include "p2G4_pending_tx_rx_list.h"
#include "channel_multiatt_args.h"
#include "channel_if.h"

static uint n_devices;
static ch_multiatt_args_t args;

#define UNINITIALIZED 0
#define CONSTANT_ATT  1
#define FROM_FILE     2

#define MAXLINESIZE 2048

/* Structure to keep the status of each path file */
typedef struct {
  char *filename;
  FILE *fileptr;
  bs_time_t last_time;
  bs_time_t next_time;
  double last_att;
  double next_att;
} file_status_t;

typedef struct {
  uint ndevices;
  double distance_exp;
  double attenuation;
  double atxtra;
  char *matrix_file_name;
  char *path_mode; //For each path, in which mode is it running
  double *paths_att;
  file_status_t *files_status;
} channel_status_t;
/* All the status of the channel */
static channel_status_t channel_status;

/**
 * Read a line from a file into a buffer (s), while
 * removing duplicate spaces (unless they are quoted), comments (#...), and ":",
 * and empty lines
 * The string will be null terminated
 *
 * Return: The number of characters copied into s
 */
static int att_readline(char *s, int size, FILE *stream) {
  int c = 0, i=0;
  bool was_a_space = true;
  bool in_a_string = false;

  while ((i == 0) && (c != EOF)) {
    while ((i < size - 1) && ((c=getc(stream)) != EOF) && c!='\n') {
      if (isspace(c) && (!in_a_string)) {
        if (was_a_space) {
          continue;
        }
        was_a_space = true;
      } else {
        was_a_space = false;
      }
      if ((c == ':') && (!in_a_string)) {
        continue;
      }
      if (c=='"') {
        in_a_string = !in_a_string;
      }
      if (c == '#') {
        bs_skipline(stream);
        break;
      }
      s[i++] =c;
    }
  }
  s[i] = 0;

  if (i >= size - 1) {
    bs_trace_warning_line("Truncated line while reading from file after %i chars\n",size-1);
  }
  return i;
}

/*
 * Return the length of a string.
 * Where the strign was terminated by "\0" or "\""
 */
static int att_strlen(char *input) {
  uint i=1;
  while (input[i] != 0 && input[i] != '"') {
    i++;
  }
  return i;
}

/**
 * Copy a string ("<string>") into output and null terminate it
 * note that the "" will be removed, and that the input pointer is expected to point at the first "
 */
static int att_copy_string(char *input, char* output, uint *n) {
  uint i=1;
  while (input[i] != 0 && input[i] != '"') {
    output[i-1] = input[i];
    i++;
  }
  *n = i;
  output[i-1] = 0;

  if (i == 1) {
    return -1;
  } else {
    return 0;
  }
}

/**
 * Initialize the reading/interpolation of attenuations
 * from an attenuation file (for one path)
 */
static uint init_distance_file(file_status_t *this_f, uint index) {
  //Try to read first line
  // if succeeded set last_time, last_att to that
  //Try to read next line
  // if succeeded
  //    set next_time and next_att to that
  //    return FROM_FILE;
  // if not succeded (only 1 line in file)
  //    set the paths_att[index] = last_att + atxtra
  //    return CONSTANT_ATT

  int read;
  char line_buf[MAXLINESIZE];
  char *filename = this_f->filename;
  this_f->fileptr = bs_fopen(filename, "r");

  //we try to read the first line:
  read = att_readline(line_buf, MAXLINESIZE, this_f->fileptr);
  if (read == 0) {
    bs_trace_error_line("file %s seems empty\n",filename);
  }
  read = sscanf(line_buf, "%"SCNtime" %le", &this_f->last_time , &this_f->last_att);
  if (read < 2) {
    bs_trace_error_line("file %s seems corrupted\n",filename);
  }

  //we try to read the second line:
  uint failed = 0;
  read = att_readline(line_buf, MAXLINESIZE, this_f->fileptr);
  if (read == 0) {
    failed = 1;
  } else {
    read = sscanf(line_buf, "%"SCNtime" %le", &this_f->next_time , &this_f->next_att);
    if (read == 0) {
      failed = 1;
    }
    if (read == 1) {
      bs_trace_error_line("file %s seems corrupted\n",filename);
    }
  }
  if (failed == 0) {
    return FROM_FILE;
  } else {
    channel_status.paths_att[index] = this_f->last_att + channel_status.atxtra;
    return CONSTANT_ATT;
  }
}

/**
 * Return the attenuation (for a path whose attenuation is being read/interpolated from file data)
 */
static double att_from_file(file_status_t *this_status, bs_time_t now, uint index) {
  //while now >= next_time:
  //  move next_time to last_time
  //  move next_att to last_att
  //  try to read next line
  //  if no next line
  //    overwrite the mode to CONSTANT_ATT, set the paths_att[index] = last_att + atxtra
  //    return paths_att[index]
  //  else
  //    set next_time and next_att to the value,
  //
  //if now <= last_time:
  // return last_att + PL_status.atxtra
  //elseif (in the middle)
  // interpolate attenuation,
  // return that attenuation + atxtra

  while (now >= this_status->next_time){
    uint failed = 0;
    int read;
    char line_buf[MAXLINESIZE];

    this_status->last_time = this_status->next_time;
    this_status->last_att = this_status->next_att;

    read = att_readline(line_buf, MAXLINESIZE, this_status->fileptr);
    if (read == 0) {
      failed = 1;
    } else {
      read = sscanf(line_buf, "%"SCNtime" %le", &this_status->next_time , &this_status->next_att);
      if (read == 0) {
        failed = 1;
      }
      if (read == 1) {
        bs_trace_error_line("file %s seems corrupted\n",this_status->filename);
      }
    }
    if ( failed == 1 ) { //no need to call this function again, the value wont change
      channel_status.paths_att[index] = this_status->last_att + channel_status.atxtra;
      channel_status.path_mode[index] = CONSTANT_ATT;
      return channel_status.paths_att[index];
    }
  }

  if (now <= this_status->last_time) {
    return this_status->last_att + channel_status.atxtra;
  } else { //in between last_time and next_time
    double AttInterp = (this_status->next_att - this_status->last_att)
                       * ((double)(now - this_status->last_time)
                       / (double)(this_status->next_time - this_status->last_time))
                       + this_status->last_att;
    return AttInterp + channel_status.atxtra;
  }

  //Unreachable
  return channel_status.atxtra;
}

static void process_matrix_file_line(char *buf, uint *rx, uint *tx, double *att, char **filename) {
  uint read;
  uint off;
  uint ok;
  uint points_to_file = 0;
  char *endp;
  *filename = NULL;
  const char corruptmsg[] = "Corrupted attenuation matrix file (format txnbr rxnbr : {attenuation|\"filename\"}\n";

  *tx = strtoul(buf, &endp, 0);
  if (endp == buf) {
    bs_trace_error_line(corruptmsg);
  }
  buf = endp;
  *rx = strtoul(buf, &endp, 0);
  if (endp == buf) {
    bs_trace_error_line(corruptmsg);
  }
  buf = endp;

  { //check if next meaningfull char is " or a number
    off = 0;
    while (buf[off] != 0) {
      if (buf[off] == '"') {
        points_to_file = 1;
        break;
      }
      if ((buf[off] >= '0') && (buf[off] <= '9')) {
        break;
      }
      off++;
    } //while
  }
  if (points_to_file) {
    uint length = att_strlen(&buf[off]);
    *filename = (char*) bs_calloc(length, sizeof(char));
    ok = att_copy_string(&buf[off], *filename, &read);
    if (ok == -1) {
      bs_trace_error_line(corruptmsg);
    }
  } else {
    read = sscanf(&buf[off], "%le", att);
    if (read < 1) {
      bs_trace_error_line(corruptmsg);
    }
  }
}

/**
 * Return the path loss from <tx> -> <rx> in this instant (<Now>)
 */
static double calculate_att(uint tx, uint rx, bs_time_t Now) {
  uint index = rx * channel_status.ndevices + tx;
  if (channel_status.path_mode[index] == CONSTANT_ATT) {
    return channel_status.paths_att[index];
  } else if (channel_status.path_mode[index] == FROM_FILE) {
    return att_from_file(&channel_status.files_status[index], Now, index);
  } else {
    bs_trace_error_line("bad error: path status not initialized or corrupted\n");
    return 0;
  }
}

/**
 * Initialize the channel
 */
int channel_init(int argc, char *argv[], uint n_devs) {
  n_devices = n_devs;

  channel_multiatt_argparse(argc, argv, &args);

  channel_status.ndevices = n_devs;
  channel_status.attenuation = args.att;
  channel_status.atxtra = args.attextra;
  channel_status.matrix_file_name = args.matrix_file_name;

  channel_status.path_mode = (char*)bs_calloc(n_devs*n_devs, sizeof(char));
  channel_status.paths_att = (double*)bs_calloc(n_devs*n_devs, sizeof(double));
  channel_status.files_status = (file_status_t *)bs_calloc(n_devs*n_devs, sizeof(file_status_t));

  if (args.matrix_file_name != NULL) {
    //Try to open the file

    FILE *matrix_file;
    char line_buf[MAXLINESIZE];
    int read = 0;
    matrix_file = bs_fopen(args.matrix_file_name, "r");

    while (read != EOF) {
      uint rx, tx;
      double att;
      char *filename;
      uint index;
      read = att_readline(line_buf, MAXLINESIZE, matrix_file);
      if (read == 0) {
        break;
      }
      process_matrix_file_line(line_buf, &rx, &tx, &att, &filename);
      if ((rx >= n_devs) || (tx >= n_devs)) {
        bs_trace_warning_line("The distances matrix file is trying to define the path from %i->%i, but only %i are set in the simulation => will be ignored\n",
                              tx, rx, n_devs);
        free(filename);
        continue;
      }

      index = rx * n_devs + tx;
      channel_status.files_status[index].filename = filename;

      if (channel_status.path_mode[index] != UNINITIALIZED) {
        bs_trace_warning_line("Redefinition of the path (%i->%i) attenuation\n", tx, rx);
      }

      if (filename == NULL) {
        channel_status.paths_att[index] = att + channel_status.atxtra;
        channel_status.path_mode[index] = CONSTANT_ATT;
      } else {
        channel_status.path_mode[index] = init_distance_file(&channel_status.files_status[index], index);
      }

    } //while (EOF matrix file)
    fclose(matrix_file);
  } //if there is a matrix file

  //set the default power level for all remaining paths
  uint tx,rx;
  for (tx = 0 ; tx < n_devs; tx++) {
    for (rx = 0; rx < n_devs; rx++) {
      if (tx == rx) {
        continue;
      }
      uint index = rx * n_devs + tx;
      if (channel_status.path_mode[index] == UNINITIALIZED) {
        if (args.matrix_file_name != NULL) {
          bs_trace_warning_line("The distance matrix file did not set the path %i->%i. It will be set to at + atxtra (%lf+%lf)\n",tx, rx, channel_status.attenuation, channel_status.atxtra);
        }
        channel_status.paths_att[index] = channel_status.attenuation + channel_status.atxtra;
        channel_status.path_mode[index] = CONSTANT_ATT;
      }
    }
  } //for tx
  return 0;
}

/**
 * Recalculate the fading and path loss of the channel in this current moment (<now>)
 * in between the N used paths and the receive path (<rxnbr>)
 *
 * inputs:
 *  tx_used    : array with n_devs elements, 0: that tx is not transmitting,
 *                                           1: that tx is transmitting,
 *               e.g. {0,1,1,0}: devices 1 and 2 are transmitting, device 0 and 3 are not.
 *  tx_list    : array with all transmissions status (the channel can check here the modulation type of the transmitter if necessary)
 *               (ignored in this channel)
 *  txnbr      : desired transmitter number (the channel will calculate the ISI only for the desired transmitter)
 *               (ignored in this channel)
 *  rxnbr      : device number which is receiving
 *               (ignored in this channel)
 *  now        : current time
 *               (ignored in this channel)
 *  att        : array with n_devs elements. The channel will overwrite the element i
 *               with the average attenuation from path i to rxnbr (in dBs)
 *               The caller allocates this array
 *  ISI_SNR    : The channel will return here an estimate of the SNR limit due to multipath
 *               caused ISI for the desired transmitter (in dBs)
 *               (This channel sets this value always to 100.0)
 *
 * Returns < 0 on error.
 * 0 otherwise
 */
int channel_calc(const uint *tx_used, tx_el_t *tx_list, uint txnbr, uint rxnbr, bs_time_t now, double *att, double *ISI_SNR) {
  uint tx_i;
  for (tx_i = 0 ; tx_i < n_devices; tx_i++) {
    if (tx_used[tx_i]) {
      att[tx_i] = calculate_att(tx_i, rxnbr, now);
    }
  }
  *ISI_SNR = 100;

  return 0;
}

/**
 * Clean up: Free the memory the channel may have allocate
 * close any file descriptors etc.
 * (the simulation has ended)
 */
void channel_delete() {
  if (channel_status.path_mode != NULL) {
    free(channel_status.path_mode);
  }
  if (channel_status.paths_att != NULL) {
    free(channel_status.paths_att);
  }
  if (channel_status.files_status != NULL) {
    uint tx;
    uint rx;
    for (tx = 0 ; tx < channel_status.ndevices ; tx ++) {
      for ( rx = 0; rx < channel_status.ndevices; rx++ ){
        uint index = rx * channel_status.ndevices + tx;
        if (channel_status.files_status[index].fileptr != NULL) {
          fclose(channel_status.files_status[index].fileptr);
        }
        if (channel_status.files_status[index].filename != NULL) {
          free(channel_status.files_status[index].filename);
        }
      }
    }
    free(channel_status.files_status);
  }
}
