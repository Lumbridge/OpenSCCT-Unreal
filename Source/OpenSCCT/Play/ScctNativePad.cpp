#include "Play/ScctNativePad.h"

#if PLATFORM_WINDOWS
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Menu/SScctPadPanel.h"
#include "Play/ScctControlSettings.h"
#include "Play/ScctPadMirror.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctControlsApi.h"
#include "Widgets/SViewport.h"

THIRD_PARTY_INCLUDES_START
#pragma push_macro("_HIDE_GLOBAL_ASYNC_STATUS")
#ifndef _HIDE_GLOBAL_ASYNC_STATUS
#define _HIDE_GLOBAL_ASYNC_STATUS
#endif
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include "Microsoft/AllowMicrosoftPlatformAtomics.h"
#include <roapi.h>
#include <wrl.h>
#include <windows.gaming.input.h>
#include <commctrl.h>
#include "Microsoft/HideMicrosoftPlatformAtomics.h"
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#pragma pop_macro("_HIDE_GLOBAL_ASYNC_STATUS")
THIRD_PARTY_INCLUDES_END
#undef LoadImage
#undef DialogBox

namespace ScctNativePad
{
	using namespace ABI::Windows::Gaming::Input;
	using Microsoft::WRL::ComPtr;

	// XInput supplies the ordinary buttons to Unreal. WGI supplies the four independent
	// paddles. Never synthesize a face button for a controller's firmware mapping.
	class FScctPaddleReader
	{
	public:
		FScctPaddleReader()
		{
			const HRESULT Init = ::RoInitialize(RO_INIT_MULTITHREADED);
			bInitialized = SUCCEEDED(Init);
			if (bInitialized || Init == RPC_E_CHANGED_MODE)
			{
				Microsoft::WRL::Wrappers::HStringReference Name(RuntimeClass_Windows_Gaming_Input_Gamepad);
				::RoGetActivationFactory(Name.Get(), IID_PPV_ARGS(&Factory));
			}
		}

		~FScctPaddleReader()
		{
			Pads.Reset();
			Factory.Reset();
			if (bInitialized) ::RoUninitialize();
		}

		uint8 Read(double Now)
		{
			if (!Factory) return 0;
			if (Now >= NextScan)
			{
				Pads.Reset();
				Factory->get_Gamepads(&Pads);
				NextScan = Now + 1.0;
			}
			unsigned Count = 0;
			if (!Pads || FAILED(Pads->get_Size(&Count))) return 0;
			uint8 Result = 0;
			for (unsigned Index = 0; Index < Count; ++Index)
			{
				ComPtr<IGamepad> Pad;
				GamepadReading Reading{};
				if (SUCCEEDED(Pads->GetAt(Index, &Pad)) && SUCCEEDED(Pad->GetCurrentReading(&Reading)))
					Result |= (static_cast<unsigned>(Reading.Buttons) / GamepadButtons_Paddle1) & 15;
			}
			return Result;
		}
	private:
		ComPtr<IGamepadStatics> Factory;
		ComPtr<__FIVectorView_1_Windows__CGaming__CInput__CGamepad> Pads;
		bool bInitialized = false;
		double NextScan = 0;
	};

	using FMask = FScctPadMirror::Mask;
	constexpr FMask PadBit(EScctKey Key) { return 1u << (int(Key) - int(EScctKey::PadA)); }
	constexpr FMask LeftTrigger = 1u << 18, RightTrigger = 1u << 19;
	constexpr unsigned RightStick = 20, LeftStick = 24;
	constexpr FMask Buttons = (1u << RightStick) - 1;

	enum EDirection { Up, Down, Left, Right };
	FMask StickDirection(unsigned Base, double X, double Y, double Threshold)
	{
		if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) <= Threshold) return 0;
		const EDirection Dir = FMath::Abs(X) > FMath::Abs(Y) ? (X < 0 ? Left : Right) : (Y < 0 ? Down : Up);
		return 1u << (Base + Dir);
	}

	FMask ButtonBit(FKey Key)
	{
		if (Key == EKeys::Gamepad_LeftTrigger) return LeftTrigger;
		if (Key == EKeys::Gamepad_RightTrigger) return RightTrigger;
		for (int Code = int(EScctKey::PadA); Code <= int(EScctKey::PadP4); ++Code)
			if (Key == FScctControls::Keys()[Code].Key) return 1u << (Code - int(EScctKey::PadA));
		return 0;
	}

	FMask MenuEquivalent(int Code)
	{
		switch (Code)
		{
		case VK_ESCAPE: return PadBit(EScctKey::PadB) | PadBit(EScctKey::PadBack) | PadBit(EScctKey::PadStart);
		case VK_SPACE: case VK_RETURN: case VK_LBUTTON: return PadBit(EScctKey::PadA);
		case VK_UP: return PadBit(EScctKey::PadDpadUp) | (1u << (LeftStick + Up));
		case VK_DOWN: return PadBit(EScctKey::PadDpadDown) | (1u << (LeftStick + Down));
		case VK_LEFT: return PadBit(EScctKey::PadDpadLeft) | (1u << (LeftStick + Left));
		case VK_RIGHT: return PadBit(EScctKey::PadDpadRight) | (1u << (LeftStick + Right));
		default: return 0;
		}
	}

	struct FWindowInput
	{
		UINT Message;
		WPARAM W;
		LPARAM L;
		int Code() const
		{
			switch (Message)
			{
			case WM_KEYDOWN: case WM_KEYUP: return int(W);
			case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: return VK_LBUTTON;
			case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: return VK_RBUTTON;
			case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: return VK_MBUTTON;
			case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP: return HIWORD(W) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
			case WM_MOUSEWHEEL: return GET_WHEEL_DELTA_WPARAM(W) > 0 ? 256 : 257;
			default: return 0;
			}
		}
		bool Released() const
		{
			return Message == WM_KEYUP || Message == WM_LBUTTONUP || Message == WM_RBUTTONUP ||
				Message == WM_MBUTTONUP || Message == WM_XBUTTONUP;
		}
		bool Repeat() const { return Message == WM_KEYDOWN && (L & (1u << 30)); }
		int Scan() const { return int((L >> 16) & 0x1ff); }
	};

	class FScctNativePad final : public IInputProcessor
	{
	public:
		explicit FScctNativePad(ULocalPlayer* InPlayer) : Player(InPlayer) {}
		~FScctNativePad() { SetWindow(nullptr); }

		void Tick(float, FSlateApplication& Slate, TSharedRef<ICursor>) override
		{
			const ULocalPlayer* Local = Player.Get();
			const UGameViewportClient* Client = Local ? Local->ViewportClient.Get() : nullptr;
			const TSharedPtr<SWindow> GameWindow = Client ? Client->GetWindow() : nullptr;
			SetWindow(GameWindow && GameWindow->GetNativeWindow() ?
				static_cast<HWND>(GameWindow->GetNativeWindow()->GetOSWindowHandle()) : nullptr);
			const bool bFocused = Window && ::GetForegroundWindow() == Window && GameWindow == Slate.GetActiveTopLevelWindow() &&
				(!GIsEditor || (Client->GetGameViewportWidget() && (Client->GetGameViewportWidget()->HasKeyboardFocus() ||
					Client->GetGameViewportWidget()->HasFocusedDescendants())));
			const double Now = FPlatformTime::Seconds();
			const uint8 Read = Paddles.Read(Now);
			if (!bFocused || !bActive) IgnoreHeld = Read;
			IgnoreHeld &= Read;
			const uint8 Next = bFocused ? Read & ~IgnoreHeld : 0;
			bActive = bFocused;
			if (Local && Next != Sent)
			{
				const FPlatformUserId User = Local->GetPlatformUserId();
				const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(User);
				for (int Index = 0; Device.IsValid() && Index < 4; ++Index)
				{
					if (!((Sent ^ Next) & (1 << Index))) continue;
					const FName Key = ScctPaddleKey(Index).GetFName();
					if (Next & (1 << Index)) Slate.OnControllerButtonPressed(Key, User, Device, false);
					else Slate.OnControllerButtonReleased(Key, User, Device, false);
				}
			}
			Sent = Next;
			if (!bFocused)
			{
				ResetInput();
			}
			else if (Pending.Num() && Now - PendingAt >= FScctPadMirror::PairSeconds)
			{
				FinishPending(false);
			}
		}

		bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Event.GetKey().IsGamepadKey() && !Event.IsRepeat() && ForPlayer(Event))
				Record(Held | ButtonBit(Event.GetKey()));
			return false;
		}
		bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Event.GetKey().IsGamepadKey() && ForPlayer(Event))
				Record(Held & ~ButtonBit(Event.GetKey()));
			return false;
		}
		bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
		{
			if (!ForPlayer(Event)) return false;
			const FKey Key = Event.GetKey();
			const double Value = Event.GetAnalogValue();
			const auto* Settings = UScctControlSettings::Get();
			if (Key == EKeys::Gamepad_LeftTriggerAxis || Key == EKeys::Gamepad_RightTriggerAxis)
			{
				const FMask Bit = Key == EKeys::Gamepad_LeftTriggerAxis ? LeftTrigger : RightTrigger;
				Record(Value > Settings->PadTriggerThreshold ? Held | Bit : Held & ~Bit);
			}
			else
			{
				if (Key == EKeys::Gamepad_LeftX) Move.X = Value;
				else if (Key == EKeys::Gamepad_LeftY) Move.Y = Value;
				else if (Key == EKeys::Gamepad_RightX) Aim.X = Value;
				else if (Key == EKeys::Gamepad_RightY) Aim.Y = Value;
				else return false;
				Record((Held & Buttons) | StickDirection(LeftStick, Move.X, Move.Y, 0.65) |
					StickDirection(RightStick, Aim.X, Aim.Y, FMath::Max(0.5f, Settings->PadDeadzoneRight)));
			}
			return false;
		}

	private:
		bool ForPlayer(const FInputEvent& Event) const
		{
			return bActive && Player.IsValid() && Event.GetPlatformUserId() == Player->GetPlatformUserId();
		}
		bool Playing() const
		{
			const auto* PC = Player.IsValid() ? Cast<AScctPlayerController>(Player->PlayerController) : nullptr;
			return PC && PC->HasPlayInput();
		}
		void Record(FMask Next)
		{
			if (Held == Next) return;
			const double Now = FPlatformTime::Seconds();
			if ((Next & ~Held) && !Playing()) LastMenuPress = Now;
			Held = Next;
			MotionMirror.Sample(Next & (15u << RightStick), Now);
			if (Mirror.Sample(Next, Now) && Pending.Num()) FinishPending(true);
		}
		void ResetInput()
		{
			Mirror = {};
			MotionMirror = {};
			Held = 0;
			Move = Aim = FVector2D::ZeroVector;
			Pending.Reset();
			Swallowed.Reset();
			bHavePoint = false;
			LastMenuPress = -1e9;
		}

		void SetWindow(HWND Next)
		{
			if (Window == Next) return;
			if (Window) ::RemoveWindowSubclass(Window, WindowProc, reinterpret_cast<UINT_PTR>(this));
			Window = Next;
			if (Window && !::SetWindowSubclass(Window, WindowProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
				Window = nullptr;
			ResetInput();
		}
		static LRESULT CALLBACK WindowProc(HWND Hwnd, UINT Message, WPARAM W, LPARAM L, UINT_PTR, DWORD_PTR Data)
		{
			auto* Self = reinterpret_cast<FScctNativePad*>(Data);
			if (Message == WM_NCDESTROY) Self->SetWindow(nullptr);
			else if (Self->HandleMessage({Message, W, L}))
			{
				if (Message == WM_INPUT) return ::DefWindowProcW(Hwnd, Message, W, L);
				return 0;
			}
			return ::DefSubclassProc(Hwnd, Message, W, L);
		}

		// Wait up to the Godot pairing interval only for a potential keyboard-first copy.
		// Replay unmatched injected events, in order, to the original window and focus.
		void FinishPending(bool Drop)
		{
			if (Pending.IsEmpty()) return;
			const auto Messages = MoveTemp(Pending);
			Mirror.ForgetPending();
			const auto& First = Messages[0];
			if (Drop)
			{
				if (First.Message != WM_MOUSEWHEEL && !Messages.ContainsByPredicate([](const auto& M) { return M.Released(); }))
					Swallowed.Add(First.Code(), First.Message == WM_KEYDOWN ? First.Scan() : INDEX_NONE);
			}
			else if (Window && ::GetForegroundWindow() == Window && PendingFocus == FSlateApplication::Get().GetKeyboardFocusedWidget())
			{
				TGuardValue<bool> Replay(bReplaying, true);
				for (const auto& M : Messages) ::SendMessageW(Window, M.Message, M.W, M.L);
			}
		}

		bool HandleMessage(const FWindowInput& Input)
		{
			if (bReplaying || !bActive || ::GetForegroundWindow() != Window) return false;
			const int Code = Input.Code();
			const bool Char = Input.Message == WM_CHAR;
			const bool Motion = Input.Message == WM_MOUSEMOVE || Input.Message == WM_INPUT;
			if (!Code && !Char && !Motion) return false;
			FIntPoint Delta(0, 0);
			if (Input.Message == WM_MOUSEMOVE)
			{
				const FIntPoint Point(static_cast<short>(LOWORD(Input.L)), static_cast<short>(HIWORD(Input.L)));
				if (bHavePoint) Delta = Point - LastPoint;
				LastPoint = Point;
				bHavePoint = true;
			}

			INPUT_MESSAGE_SOURCE Source{};
			if (!::GetCurrentInputMessageSource(&Source) || Source.originId != IMO_INJECTED ||
				(Source.deviceType != IMDT_MOUSE && Source.deviceType != IMDT_KEYBOARD)) return false;
			if (Pending.Num())
			{
				const auto& First = Pending[0];
				if ((Char && First.Message == WM_KEYDOWN && First.Scan() == Input.Scan()) ||
					(Code == First.Code() && (Input.Released() || Input.Repeat())))
				{
					Pending.Add(Input);
					return true;
				}
			}
			if (Char)
				return Swallowed.FindKey(Input.Scan()) != nullptr;
			if (Code && (Input.Released() || Input.Repeat()))
				return Input.Released() ? Swallowed.Remove(Code) != 0 : Swallowed.Contains(Code);
			if (Code) Swallowed.Remove(Code);
			const auto* Settings = UScctControlSettings::Get();
			if (!Settings->bFilterPadMirrors || ScctUi::IsTyping()) return false;
			const auto Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			const bool Capture = Focus && Focus->GetType() == FName(TEXT("SScctPadBindRow")) && StaticCastSharedPtr<SScctPadBindRow>(Focus)->IsCapturing();
			const bool Play = Playing();
			const double Now = FPlatformTime::Seconds();
			if (Motion)
			{
				if (!Play || !Settings->bPadEnabled || !Settings->bPadAim || Pending.Num()) return false;
				if (Input.Message == WM_INPUT)
				{
					RAWINPUT Raw{};
					UINT Size = sizeof(Raw);
					if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(Input.L), RID_INPUT, &Raw, &Size, sizeof(RAWINPUTHEADER)) != sizeof(Raw) ||
						Raw.header.dwType != RIM_TYPEMOUSE || Raw.header.hDevice || (Raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return false;
					Delta = FIntPoint(Raw.data.mouse.lLastX, Raw.data.mouse.lLastY);
				}
				const FMask Direction = StickDirection(RightStick, Delta.X, -double(Delta.Y), 0.0);
				return Direction && MotionMirror.Input(258, Direction, Now, false, true);
			}
			if (Pending.Num()) FinishPending(false);
			const FMask Menu = (!Play || Now - LastMenuPress <= FScctPadMirror::PairSeconds) ? MenuEquivalent(Code) : 0;
			FMask Allowed = Capture ? Buttons : Menu;
			if (!Allowed && Play && Settings->bPadEnabled)
			{
				if (Settings->bPadBinds) Allowed |= Buttons;
				if (Settings->bPadMove) Allowed |= 15u << LeftStick;
			}
			if (!Allowed) return false;
			if (Mirror.Input(Code, Allowed, Now, Capture || Menu != 0))
			{
				if (Input.Message != WM_MOUSEWHEEL) Swallowed.Add(Code, Input.Message == WM_KEYDOWN ? Input.Scan() : INDEX_NONE);
				return true;
			}
			if (Mirror.MayMatchPending() && FSlateApplication::Get().IsGamepadAttached())
			{
				Pending.Add(Input);
				PendingAt = Now;
				PendingFocus = Focus;
				return true;
			}
			return false;
		}

		TWeakObjectPtr<ULocalPlayer> Player;
		FScctPaddleReader Paddles;
		uint8 Sent = 0, IgnoreHeld = 0;
		bool bActive = false, bReplaying = false, bHavePoint = false;
		HWND Window = nullptr;
		FScctPadMirror Mirror, MotionMirror;
		FMask Held = 0;
		FVector2D Move = FVector2D::ZeroVector, Aim = FVector2D::ZeroVector;
		FIntPoint LastPoint = FIntPoint::ZeroValue;
		double LastMenuPress = -1e9, PendingAt = 0;
		TArray<FWindowInput> Pending;
		TWeakPtr<SWidget> PendingFocus;
		TMap<int, int> Swallowed;
	};
}

TSharedRef<IInputProcessor> MakeScctNativePad(ULocalPlayer* Player)
{
	return MakeShared<ScctNativePad::FScctNativePad>(Player);
}
#endif
