# Online: the prototype in the hello; missing tracks (for docs/xdl-format/README.md §9.1 and the online section of HWVX-SDL/README.md)

- MSG_HELLO's fingerprint covers the PS2 prototype too: with an import mode other than
  `off`, the mode key and the prototype's identity (FNV-1a of its files' paths and sizes,
  `Port_ProtoIdentity`, proto.c) go into the hash, and the summary reads
  "..., PS2 prototype (<mode>, <6 hex>)". Machines with a different mode or prototype are
  refused like ones with other mods ("Mods or prototype differ"). `off` leaves the hello
  as before.
- A client given a track id it doesn't have leaves the session ("The host's track isn't
  here: <id>") instead of loading the level the track is made from.
- Player ports 5-8 that start at a free spawn of ports 1-4 keep their own port (named by
  `Port_OnlineTilePort`). In Drag Race the script then renumbers them by lane
  (ActionSetPlayerOrder), so the round goes to the lane's number.
- tools/online8_test.sh: `GAP=P` leaves port P empty (its bot leaves in the lobby);
  HWVX_ONLINE_TEST `lobby=SECONDS`; every other port must sync at the host.
