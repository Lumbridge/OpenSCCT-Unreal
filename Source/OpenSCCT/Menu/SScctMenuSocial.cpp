// The social workstream's screens as pages of the menus' host: the Account page (Menu/SScctAccountPage.h) and the
// password page a friend's or the website's join asks for (Menu/SScctJoinPasswordPage.h). Each page holds the screen
// as it was built, and its own BACK, JOIN or CANCEL goes back as Escape does.

#include "Menu/ScctMenuPage.h"

#include "Engine/GameInstance.h"
#include "Menu/SScctAccountPage.h"
#include "Menu/SScctJoinPasswordPage.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Social/ScctAccountSubsystem.h"
#include "Social/ScctSocialSubsystem.h"

namespace
{
	template <typename TSubsystem>
	TSubsystem* ScctSocialPageSubsystem(const FScctMenuHost& Host)
	{
		const UScctOnlineSubsystem* Visit = Host.Online.Get();
		UGameInstance* Instance = Visit != nullptr ? Visit->GetGameInstance() : nullptr;
		return Instance != nullptr ? Instance->GetSubsystem<TSubsystem>() : nullptr;
	}

	class SScctMenuAccount : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuAccount) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			const FScctMenuHost Copy = Host;
			ChildSlot
			[
				SNew(SScctAccountPage)
				.Account(ScctSocialPageSubsystem<UScctAccountSubsystem>(Host))
				.Online(Host.Online)
				.OnBack(FSimpleDelegate::CreateLambda([Copy] { if (Copy.Back) { Copy.Back(); } }))
				.OnOpen([Copy](const FString& Page) { if (Copy.Open) { Copy.Open(Page); } })
			];
		}

		virtual FString GetTitle() const override { return TEXT("ACCOUNT"); }
		virtual FString GetKicker() const override { return TEXT("Name, rank and career"); }
	};

	class SScctMenuJoinPassword : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuJoinPassword) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost, const FScctSocialPasswordPrompt& Prompt)
		{
			Host = InHost;
			const FScctMenuHost Copy = Host;
			ChildSlot
			[
				SNew(SScctJoinPasswordPage)
				.Social(ScctSocialPageSubsystem<UScctSocialSubsystem>(Host))
				.Prompt(Prompt)
				.OnDone(FSimpleDelegate::CreateLambda([Copy] { if (Copy.Back) { Copy.Back(); } }))
			];
		}

		virtual FString GetTitle() const override { return TEXT("PASSWORD"); }
		virtual FString GetKicker() const override { return TEXT("A friend's join"); }
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeAccount(const FScctMenuHost& Host)
{
	return SNew(SScctMenuAccount, Host);
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeJoinPassword(const FScctMenuHost& Host, const FScctSocialPasswordPrompt& Prompt)
{
	return SNew(SScctMenuJoinPassword, Host, Prompt);
}
