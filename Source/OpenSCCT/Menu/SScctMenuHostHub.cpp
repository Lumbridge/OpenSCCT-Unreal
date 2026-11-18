// Create Session (Menu/SScctHostPage.h) and the hub list (Menu/SScctHubPage.h) as pages of the menus' host: each
// page holds the screen the hosting and hub workstreams built, and its own BACK goes back as Escape does.

#include "Menu/ScctMenuPage.h"

#include "Engine/GameInstance.h"
#include "Hub/ScctHubSubsystem.h"
#include "Menu/SScctHostPage.h"
#include "Menu/SScctHubPage.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	class SScctMenuHostGame : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuHostGame) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			const FScctMenuHost Copy = Host;
			ChildSlot
			[
				SNew(SScctHostPage)
				.Dialogs(Host.Dialogs)
				.Online(Host.Online)
				.OnBack(FSimpleDelegate::CreateLambda([Copy] { if (Copy.Back) { Copy.Back(); } }))
			];
		}

		virtual FString GetTitle() const override { return TEXT("HOST GAME"); }
		virtual FString GetKicker() const override { return TEXT("A listen server on this PC"); }
	};

	class SScctMenuHub : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuHub) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			const FScctMenuHost Copy = Host;
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			UScctHubSubsystem* Hub = Visit != nullptr && Visit->GetGameInstance() != nullptr ? Visit->GetGameInstance()->GetSubsystem<UScctHubSubsystem>() : nullptr;
			ChildSlot
			[
				SNew(SScctHubPage)
				.Hub(Hub)
				.Online(Host.Online)
				.OnBack(FSimpleDelegate::CreateLambda([Copy] { if (Copy.Back) { Copy.Back(); } }))
			];
		}

		virtual FString GetTitle() const override { return TEXT("HUB"); }
		virtual FString GetKicker() const override { return TEXT("The master's hubs, or one on this PC"); }
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeHostGame(const FScctMenuHost& Host)
{
	return SNew(SScctMenuHostGame, Host);
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeHub(const FScctMenuHost& Host)
{
	return SNew(SScctMenuHub, Host);
}
