# ext_2G4_channel_positional

This is a channel model for ext_2G4_phy_v1

This is a non realistic channel model.
It models NxN independent paths each with a configurable attenuation
This attenuation can also be configured to change over time.

The default atennuation for all paths is set with the command line options
`-at=<attenuation>`.

A file can be used to set the attenuation for some/all of the individual paths.
This file is specified with the command line option `-file=<attenuations_file>`

This file shall have one separate line per path like:<br>
    `x y : {value|"<attenuation_file>"}`<br>
e.g.:
```
	  1 2 : 65
	  2 1 : 65
	  1 3 : "/myfolder/att_file.txt"
	  3 1 : "/myfolder/att_file.txt"
```

Where:

* x is the transmitter number, x = 0..N-1
* y is the receiver number     y != x
* N being the number of devices in the simulation
* value is a floating point value in dB
* `<attenuation_file>` : is the path to a `<attenuation_file>` as described
  below which applies to that path
  Note that this file name shall be provided in between `""`

The file names can be either absolute or relative to `bin/`

For both files, `#` is treated as a comment mark (anything after a `#` is
discarded)
Empty lines are ignored

Note that the Phy assumes the channel to be pseudo-stationary, that is, that the
channel propagation conditions are static enough during a packet reception, that
is, that the coherence time of the channel is much bigger than the typical
packet length.
Therefore the channel model won't be called to reevaluate conditions during a
packet unless the devices which are transmitting change.


### The `<attenuation_file>`:

This file contains 2 columns, the first column represents time in
microseconds, and the second column the attenuation at that given time.
The times shall be in ascending order.<br>
If the simulation time is found in the file, the channel will use the
corresponding attenuation value.<br>
If the simulation time falls in between 2 times in the file, the channel
will interpolate linearly the attenuation.<br>
If the simulation time is before the first time value in the file, the
channel will use the first atttenuation provided in the file.<br>
If the simulation time is after the last time value in the file, the channel
will use the last attenuation provided in the file.<br>
