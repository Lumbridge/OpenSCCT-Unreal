#include "Play/ScctPlayerController.h"

#include "InputCoreTypes.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctBridgeApi.h"

namespace
{
	struct FButtonKey
	{
		EScctButton Button;
		FKey Key;
	};

	struct FCommandKey
	{
		const TCHAR* Command;
		FKey Key;
	};
}

AScctPlayerController::AScctPlayerController()
{
	bShowMouseCursor = false;
}

void AScctPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || !Session->IsPlaying())
	{
		return;
	}

	// Default bindings. A bindings menu replaces these tables.
	static const FButtonKey Buttons[] = {
		{ EScctButton::Up, EKeys::W },
		{ EScctButton::Down, EKeys::S },
		{ EScctButton::Left, EKeys::A },
		{ EScctButton::Right, EKeys::D },
		{ EScctButton::Fire, EKeys::LeftMouseButton },
		{ EScctButton::AltFire, EKeys::RightMouseButton },
		{ EScctButton::Crouch, EKeys::C },
		{ EScctButton::Roll, EKeys::LeftAlt },
		{ EScctButton::Using, EKeys::E },
		{ EScctButton::BackToWall, EKeys::Q },
		{ EScctButton::Reload, EKeys::R },
		{ EScctButton::HoldBreath, EKeys::LeftShift },
		{ EScctButton::Cloak, EKeys::F },
	};
	static const FCommandKey Commands[] = {
		{ TEXT("Jump"), EKeys::SpaceBar },
		{ TEXT("SwitchHeadset"), EKeys::T },
		{ TEXT("NextGadget"), EKeys::MouseScrollUp },
		{ TEXT("PreviousGadget"), EKeys::MouseScrollDown },
		{ TEXT("UseGadget"), EKeys::G },
		{ TEXT("SwapWeapon"), EKeys::X },
		{ TEXT("SwitchSpyPerspective"), EKeys::V },
	};

	FScctBridgeInput Input = {};
	for (const FButtonKey& Binding : Buttons)
	{
		Input.Buttons[static_cast<int32>(Binding.Button)] = IsInputKeyDown(Binding.Key) ? 1 : 0;
	}

	float MouseX = 0.0f, MouseY = 0.0f;
	GetInputMouseDelta(MouseX, MouseY);
	Input.MouseX = MouseX * MouseCountsPerUnit;
	Input.MouseY = MouseY * MouseCountsPerUnit;
	Session->SetInput(Input);

	for (const FCommandKey& Binding : Commands)
	{
		if (WasInputKeyJustPressed(Binding.Key))
		{
			Session->SendCommand(Binding.Command);
		}
	}
}
