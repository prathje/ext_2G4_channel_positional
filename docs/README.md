# ext_2G4_channel_positional

This is an extension of the ext_2G4_channel_multiatt and [ext_2G4_channel_Indoorv1](https://github.com/BabbleSim/ext_2G4_channel_Indoorv1).

Instead of defining attenuations directly, this channel derives the attenuation based on 3D device positions.
This is especially interesting, when there are many devices present.
Devices can be set to positions and also moved to another position over some duration (being interpolated accordingly).
Further, devices can be enabled and disabled entirely.
The base pathloss is calculated as ([here](https://github.com/prathje/ext_2G4_channel_positional/blob/eb904e8efcbede2d1e98236163936f4a2fbfb2e9/src/channel_positional.c#L309C5-L309C85)):
```
PL = channel_status.distance_exp*10.0*log10(distance) + 39.60422483423212045872;
```


The default attenuation for all paths is set with the command line options and is used whenever one of the tx or rx devices has no position
`-at=<attenuation>`.

A file should be used to set the positions for some/all of the devices.
This file is specified with the command line option `-stream=<Stream or file containing the positional for each device>`

The following commands are supported in this file:
- <ts> enable <id> => enables communication from / to this node
- <ts> disable <id> => disables communication from / to this node
- <ts> set <id> <x> <y> <z> => sets the position of node <id> at <ts>
- <ts> move <id> <x> <y> <z> <duration> => moves node <id> to position x,y,z in time duration (starting from the known position at ts (0,0,0) if not set before

Timestamps and durations are in us, whereas coordinates are given in meters.

The file names can be either absolute or relative to `bin/`

For both files, `#` is treated as a comment mark (anything after a `#` is
discarded)
Empty lines are ignored

Note that the Phy assumes the channel to be pseudo-stationary; that is, the
channel propagation conditions are static enough during a packet reception that
is that the coherence time of the channel is much longer than the typical
packet length.
Therefore, the channel model won't be called to reevaluate conditions during a
packet unless the devices that are transmitting change.

An example file for the positions:

```
0 set 0 0 0 0 0
0 set 1 0 0 0 0
0 move 1 100 0 0 60000000
0 move 0 100 0 0 60000000
1 disable 0
30000000 enable 0
45000000 disable 1
```
