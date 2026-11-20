# OpenSCCT Unreal dedicated server

Extract the archive and start the server from its folder. On Linux, run `./svm-server --config server.json`. On Windows, run `.\svm-server.exe --config server.json`. The archive includes its .NET runtime and a built-in TestBox map. It can start on a LAN or accept direct connections with no account service or other game installed.

In the Unreal client, use the LAN browser or direct connect to the server's address with port `28900`. Allow UDP `28900` for gameplay and UDP `28889` for LAN discovery. To run several servers on a host, give each a distinct game port and enable the LAN discovery port on only one. Direct connect works for the others.

For a regular match, the server reads the active Unreal content build that matches its packaged converter recipe. Set `OPENSCCT_UNREAL_CONTENT_DIR` to the folder containing `steam-current.json` to select another Unreal installation. For a manually prepared server, set `maps` in `server.json` to the `svmap` directory alongside `shared/chars`. Put additional community maps in `community/svmap/` beside the config. Set `rotation` to installed map IDs, for example `MounE` and `AquaE`. Clients and servers check map and character gameplay data on connection. The `responsiveSmoke` setting applies to every player in the match.

## Running a public instance

For a public master listing, add a `master` object with `url`, `listed: true`, and the service's required registration credentials. Make the config readable only to the operator and service. A listed server requires player accounts by default. Removing the `master` object runs the standalone server. A public Internet instance can set `lanPort` to `0`.

On Linux with systemd, install each extracted version under `/opt/openscct-unreal/server/` and point `current` at the version to run. Install the included unit under `/etc/systemd/system/` and place the instance config in `/var/lib/openscct-unreal-server/server.json`. Give the service account access to the configured map directory. The unit's private state directory holds this instance's match logs, bans and demos. Use `systemctl daemon-reload` followed by `systemctl enable --now openscct-unreal-server`.

The official Unreal instance can share an address with another edition using its own game port, listing name and unit. The match browser checks replication protocol and content hashes. A player using an incompatible release needs a server built for that release.
