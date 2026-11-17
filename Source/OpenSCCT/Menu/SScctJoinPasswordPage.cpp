#include "Menu/SScctJoinPasswordPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"

namespace PasswordPage
{
	namespace Ui = ScctMenuStyle;

	/** The side choices and what the join takes: the smaller side, spies, mercs, watch. */
	const TCHAR* const SideLabels[] = { TEXT("SMALLER SIDE"), TEXT("SPY"), TEXT("MERCENARY"), TEXT("WATCH") };
	const int32 SideTeams[] = { -1, 0, 1, 255 };
}

void SScctJoinPasswordPage::Construct(const FArguments& InArgs)
{
	namespace Ui = ScctMenuStyle;
	Social = InArgs._Social;
	Prompt = InArgs._Prompt;
	OnDone = InArgs._OnDone;

	const FOnTextCommitted OnEnter = FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			Join();
		}
	});
	PasswordBox = Ui::TextBox(TEXT("the server's password"), FString(), true, OnEnter);

	const bool bFromWebsite = (Prompt.Flags & EScctSocialRequestFlags::FromWebsite) != 0;
	const FString Lead = FString::Printf(TEXT("%s asks for a password. %s never carries it: type it here to join."),
		Prompt.Name.IsEmpty() ? TEXT("This server") : *Prompt.Name, bFromWebsite ? TEXT("The website") : TEXT("Discord"));

	const TSharedRef<SWrapBox> Sides = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PasswordPage::SideLabels); ++Index)
	{
		Sides->AddSlot()
		[
			Ui::Button(Ui::ToText(PasswordPage::SideLabels[Index]),
				FOnClicked::CreateLambda([this, Index] { SideIndex = Index; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Index] { return SideIndex == Index; }))
		];
	}

	ChildSlot
	[
		SNew(SBox).WidthOverride(560.0f)
		[
			Ui::Glass(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ Ui::Title(Ui::ToText(TEXT("SERVER PASSWORD")), 24.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
				[
					Ui::Text(Ui::ToText(Lead), 13.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 7.0f)[ Ui::Label(TEXT("Password")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ PasswordBox.ToSharedRef() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 7.0f)[ Ui::Label(TEXT("Side")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Sides ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						Ui::Button(Ui::ToText(TEXT("JOIN")), FOnClicked::CreateLambda([this] { Join(); return FReply::Handled(); }), true, Ui::Accent(), 150.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						Ui::Button(Ui::ToText(TEXT("CANCEL")), FOnClicked::CreateLambda([this]
						{
							UE_LOG(LogOpenSCCT, Log, TEXT("Menu: the join to %s was cancelled at its password"), *Prompt.Name);
							OnDone.ExecuteIfBound();
							return FReply::Handled();
						}))
					]
				])
		]
	];
}

void SScctJoinPasswordPage::Join()
{
	const FString Password = PasswordBox.IsValid() ? PasswordBox->GetText().ToString() : FString();
	if (Password.IsEmpty())
	{
		FSlateApplication::Get().SetKeyboardFocus(PasswordBox);
		return;
	}
	if (UScctSocialSubsystem* Subsystem = Social.Get())
	{
		Subsystem->SubmitPassword(Prompt, Password, PasswordPage::SideTeams[SideIndex]);
	}
	OnDone.ExecuteIfBound();
}
