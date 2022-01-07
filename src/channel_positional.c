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
#include "channel_positional_args.h"
#include "channel_if.h"
#include <math.h>
#include <string.h>

static uint n_devices;
static ch_positional_args_t args;

#define MAXLINESIZE 2048

struct pos_t { // TODO: rename...
    double x;
    double y;
    double z;
};

static inline double euclidean_dist(struct pos_t *pos_a, struct pos_t *pos_b) {
    double dx = pos_a->x-pos_b->x;
    double dy = pos_a->y-pos_b->y;
    double dz = pos_a->z-pos_b->z;
    return sqrt(dx*dx+dy*dy+dz*dz);
}

// ts device px py pz duration

/* Structure to track the status of each device file */
struct device_status {
    char communication_enabled;
    char has_position;

    bs_time_t last_time; // the time of the last position
    bs_time_t next_time; // the time of the next position

    struct pos_t last_pos; // the last position
    struct pos_t next_pos; // the next position (which will be reached at next_time)


    char cache_initialized;
    struct pos_t cached_pos; // the cached position (since most of the time, a tx is relevant for more devices
    bs_time_t cached_time; // the time of the cached position
};

typedef struct {
  uint ndevices;
  double distance_exp;
  double attenuation;
  double atxtra;

  char *position_stream_path;
  FILE *position_stream;
  bs_time_t parsed_time; // up to this ts, everything has been parsed
  char parsing_initialized; // up to this ts, everything has been parsed

  char next_line[MAXLINESIZE];
  int next_line_len; // we simply cache this



  struct device_status *devices_status;
} channel_status_t;

/* All the status of the channel */
static channel_status_t channel_status;

static int calc_interpolated_pos(struct pos_t *out_pos, uint device_index, bs_time_t t) {

    struct device_status *ds = &channel_status.devices_status[device_index];

    if (!ds->has_position) {
        return -1;
    }

    // we initialize the position to the last one plus the difference

    double frac = 0.0; // interpolate between last <-> next pos

    // we check the next_time before the last_time, this ensures that a duration of 0 will result in a direct position change at that ts
    if (t >= ds->next_time) {
        frac = 1.0;
    } else if (t <= ds->last_time) {
        frac = 0.0;
    } else  {
        // note that in this case, last_time != next_time
        // we need to interpolate !
        frac = (double) (t - ds->last_time) / (double) (ds->next_time - ds->last_time);
    }

    out_pos->x = ds->last_pos.x + frac * (ds->next_pos.x - ds->last_pos.x);
    out_pos->y = ds->last_pos.y + frac * (ds->next_pos.y - ds->last_pos.y);
    out_pos->z = ds->last_pos.z + frac * (ds->next_pos.z - ds->last_pos.z);

    //bs_trace_warning_line("t: %u, d: %u, X: %f \n", t, device_index, out_pos->x);
    return 0;
}


/**
 * Write the linearly interpolated position of device into out_pos
 * Caution: File needs to have parsed timestamp t entirely!
 */
static struct pos_t *get_pos(uint device_index, bs_time_t t) {

    struct device_status *ds = &channel_status.devices_status[device_index];

    if (!ds->cache_initialized || ds->cached_time != t) { // we need to calculate again...
        int res = calc_interpolated_pos(&ds->cached_pos, device_index, t);

        if (res) {
            bs_trace_error_line("Could not calculate pos for device %u\n", device_index);
        }

        ds->cache_initialized = 1;
        ds->cached_time = t;
    }

    return &ds->cached_pos;
}


/**
 * Read a line from a stream into a buffer (buf), while
 * removing duplicate spaces (unless they are quoted), comments (#...), and ":",
 * and empty lines
 * The string will be null terminated
 *
 * Return: The number of characters copied into s
 */
static int stream_readline(char *buf, int size, FILE *stream) {
    int c = 0, i = 0;
    bool was_a_space = true;
    bool in_a_string = false;

    while ((i == 0) && (c != EOF)) {
        while ((i < size - 1) && ((c = getc(stream)) != EOF) && c != '\n') {
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
            if (c == '"') {
                in_a_string = !in_a_string;
            }
            if (c == '#') {
                bs_skipline(stream);
                break;
            }
            buf[i++] = c;
        }
    }

    buf[i] = 0;

    if (i >= size - 1) {
        bs_trace_warning_line("Truncated line while reading from file after %i chars\n",size-1);
    }

    return i;
}



static void advance_until(bs_time_t now) {

    if (now <= channel_status.parsed_time && channel_status.parsing_initialized) {
        // Nothing todo right now
        return;
    }

    //bs_trace_warning_line("parsed: %u now: %u\n", channel_status.parsed_time, now);

    while(channel_status.next_line_len) {
        bs_time_t next_line_time, next_line_duration;
        double nx, ny, nz;

        unsigned int device_index;

        char cmd[64];

        // we support the following:
        // <ts> enable <id> => enables communication from / to this node
        // <ts> disable <id> => disables communication from / to this node
        // <ts> set <id> <x> <y> <z> => sets the position of node <id> at <ts>
        // <ts> move <id> <x> <y> <z> <duration> => moves node <id> to position x,y,z in time duration (starting from the known position at ts (0,0,0) if not set before

        // we have a new line to parse...
        int read = sscanf(channel_status.next_line, "%"SCNtime" %63s %u %le %le %le %"SCNtime, &next_line_time, cmd, &device_index, &nx, &ny, &nz, &next_line_duration);

        if (read >= 1) {

            if (next_line_time <= channel_status.parsed_time && channel_status.parsing_initialized) {
                bs_trace_error_line("time already too late... ignoring: %s\n", channel_status.next_line);
                channel_status.next_line_len = stream_readline(channel_status.next_line, MAXLINESIZE, channel_status.position_stream);
                continue;
            }

            if (next_line_time > now) {
                // next_line_time > now -> parse later
                if (channel_status.parsed_time > next_line_time-1) {
                    bs_trace_error_line("channel_status.parsed_time > next_line_time-1 while parsing %s\n", channel_status.next_line);
                }

                channel_status.parsed_time = next_line_time-1;
                channel_status.parsing_initialized = 1;
                break; // ensures that the next_line buffer is kept
            }
        }

        if (read < 3) {
            bs_trace_error_line("file %s seems corrupted could not parse line %s (res: %d) \n", channel_status.position_stream_path, channel_status.next_line, read);
        }

        if (device_index < 0 || device_index > channel_status.ndevices - 1) {
            bs_trace_warning_line("File %s uses unknown device %d in line %s , skipping... \n", channel_status.position_stream_path, device_index, channel_status.next_line);
            channel_status.next_line_len = stream_readline(channel_status.next_line, MAXLINESIZE, channel_status.position_stream);
            continue;
        }

        struct device_status *ds = &channel_status.devices_status[device_index];

        if (read >= 3 && !strcmp(cmd, "enable")) {
            ds->communication_enabled = 1;
        } else if(read >= 3 && !strcmp(cmd, "disable")) {
            ds->communication_enabled = 0;
        } else if(read >= 6 && !strcmp(cmd, "set")) {
            // we simply set the position for this device directly
            ds->last_pos.x = nx;
            ds->last_pos.y = ny;
            ds->last_pos.z = nz;
            ds->last_time = next_line_time;

            ds->next_pos.x = nx;
            ds->next_pos.y = ny;
            ds->next_pos.z = nz;
            ds->next_time = next_line_time;

            ds->has_position = 1;
            // we need to recalculate the position
            ds->cache_initialized = 0;
        } else if(read >= 7 && !strcmp(cmd, "move")) {
            if (!ds->has_position) {
                bs_trace_error_line("File %s wants to move device %d with unknown current position in line %s \n", channel_status.position_stream_path, device_index, channel_status.next_line);
            }

            struct pos_t cur_pos;
            int err = calc_interpolated_pos(&cur_pos, device_index, next_line_time);

            if (err) {
                bs_trace_error_line("could not generate cur_pos while parsing line %s\n", channel_status.next_line);
            }

            // we need to recalculate the position
            ds->cache_initialized = 0;

            // we set the last pos to the last_known (maybe interpolated) position
            ds->last_pos.x = cur_pos.x;
            ds->last_pos.y = cur_pos.y;
            ds->last_pos.z = cur_pos.z;
            ds->last_time = next_line_time;


            // we now use the new, extracted position and the corresponding duration to set the next position
            // if duration is set to 0, the next position will be used directly
            ds->next_pos.x = nx;
            ds->next_pos.y = ny;
            ds->next_pos.z = nz;
            ds->next_time = next_line_time+next_line_duration;
        } else {
            bs_trace_error_line("file %s seems corrupted could not parse line %s (res: %d) \n", channel_status.position_stream_path, channel_status.next_line, read);
        }

        channel_status.next_line_len = stream_readline(channel_status.next_line, MAXLINESIZE, channel_status.position_stream);
    }
}






/**
 * Calculate the average path loss in dB given a distance in meters
 * Source: Channel_2G4Indoorv1
 */
static double PathLossFromDistance(double distance){
    double PL;
    if ( distance <= 0.0 ){
        if ( distance < 0.0 ){
            bs_trace_warning_line("distance between devices = %f, this seems like an error..\n",distance);
        }
        distance = 0.001;
    }

    PL = channel_status.distance_exp*10.0*log10(distance) + 39.60422483423212045872;
    //Ltotal = 20*log10f + N*log10d  – 28
    //20*log10(2.4e3) - 28 = 39.60422483423212045872

    if ( PL < 20.0 ){
        static int never_complained_about_silly_distance = 1;
        if ( never_complained_about_silly_distance ){
            never_complained_about_silly_distance = 0;
            bs_trace_warning_line("distance between devices is very small (%.3fm).[the pathloos (%.1fdB) has been limited to 20dB] This channel model does not model near field conditions. Are you sure you want to do this? (this warning won't appear anymore)\n",distance, PL);
        }

        PL = 20;
    }

    return PL;
} //PathLossFromDistance()

/**
 * Return the path loss from <tx> -> <rx> in this instant (<Now>)
 */
static double calculate_att(uint tx, uint rx, bs_time_t now) {

    if (!channel_status.devices_status[tx].communication_enabled || !channel_status.devices_status[rx].communication_enabled) {
        return 1000.0; // TODO: this is very arbitrary...
    } else if (!channel_status.devices_status[tx].has_position || !channel_status.devices_status[rx].has_position) {
        return channel_status.attenuation + channel_status.atxtra;
    }

    // get_pos returns a pointer to the cached position at time now
    struct pos_t *pos_tx = get_pos(tx, now);
    struct pos_t *pos_rx = get_pos(rx, now);

    double dist = euclidean_dist(pos_tx, pos_rx);

    // simply return the path loss based on the calculated distance
    return PathLossFromDistance(dist) + channel_status.atxtra;
}

/**
 * Initialize the channel
 */
int channel_init(int argc, char *argv[], uint n_devs) {
  n_devices = n_devs;

  channel_positional_argparse(argc, argv, &args);

  channel_status.ndevices = n_devs;
  channel_status.attenuation = args.att;
  channel_status.atxtra = args.attextra;

  channel_status.distance_exp = args.distance_exp;
  channel_status.position_stream_path = args.position_stream_path;
  channel_status.parsed_time = 0;
  channel_status.parsing_initialized = 0; // nothing parsed yet

  channel_status.devices_status = (struct device_status *)bs_calloc(n_devs, sizeof(struct device_status));

  uint device_index;
  for (device_index = 0 ; device_index < n_devs; device_index++) {
        struct device_status *ds = &channel_status.devices_status[device_index];

        ds->communication_enabled = 1;
        ds->has_position = 0;
        ds->last_pos.x = 0;
        ds->last_pos.y = 0;
        ds->last_pos.z = 0;
        ds->last_time = 0;

        ds->next_pos.x = 0;
        ds->next_pos.y = 0;
        ds->next_pos.z = 0;
        ds->next_time = 0;


        ds->cache_initialized = 0;
        ds->cached_pos.x = 0;
        ds->cached_pos.y = 0;
        ds->cached_pos.z = 0;
        ds->cached_time = 0;
  }

  if (channel_status.position_stream_path != NULL) {
      bs_trace_warning_line("using stream %s\n", channel_status.position_stream_path);
    channel_status.position_stream = bs_fopen(channel_status.position_stream_path, "r");
    channel_status.next_line_len = stream_readline(channel_status.next_line, MAXLINESIZE, channel_status.position_stream);
  }

  bs_trace_warning_line("init finished %d\n", channel_status.next_line_len);

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
 *  now        : current time
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
  advance_until(now);
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
    free(channel_status.devices_status);
    if (channel_status.position_stream != NULL) {
        fclose(channel_status.position_stream);
    }
}
