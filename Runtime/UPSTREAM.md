# Unreal simulation source

OpenSCCT Unreal builds its bridge, simulation, bots, dedicated server and content setup from this repository. Runtime changes are reviewed and released here.

The initial source import came from [OpenSCCT revision 647078c3](https://github.com/OpenSCCT/OpenSCCT/tree/647078c3559acc4b415c905eee367aee2c21c248). It includes the original source, credits, build files and test fixtures. The optional user-submitted gun recordings had no recorded redistribution licence and were omitted. The Unreal content converter uses the credited audio and the player's local game files.

To build the bridge from the repository root, run `dotnet build Bridge/OpenScct.UnrealBridge -c Release`. For release builds and dedicated servers, see [Releasing](../docs/RELEASING.md).
