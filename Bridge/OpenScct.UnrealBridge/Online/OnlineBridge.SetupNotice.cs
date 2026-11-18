using OpenScct.Server.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The lobby's line about a changed setup: the Godot lobby's activity notice (LobbyRoomScreen.Refresh with
/// LobbyPresentation.ChangeNotice), "Host changed map to Aquarius, mode to Story, 2 match rules.". The visit
/// compares the server's setup with the one the lobby showed last and puts the line among the chat's system lines,
/// which the lobby page draws dim. The first setup of a stay in the lobby is taken as is, as a fresh Godot lobby
/// page takes it, so coming back from a round (a random map included) says nothing.
/// </summary>
public sealed partial class OnlineBridge
{
    private LobbySetup? _noticedSetup;

    /// <summary>After the visit's ticks: says what changed in the setup since the last call, in the lobby only.</summary>
    private void FollowSetupNotice(OnlineClient client)
    {
        if (client.State != OnlineClientState.Lobby)
        {
            _noticedSetup = null;
            return;
        }

        LobbySetup setup = client.Lobby.Setup;
        if (_noticedSetup is { } previous && previous != setup)
        {
            string notice = LobbyPresentation.ChangeNotice(previous, setup, LobbyExtras.MapTitle(client.Maps, setup.Map));
            if (notice.Length > 0)
            {
                OnMessage(new SessionMessage(notice));
                Touch();
            }
        }

        _noticedSetup = setup;
    }
}
