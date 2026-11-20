#include "Menu/ScctMenuPage.h"

#include "DynamicRHI.h"
#include "RHIStats.h"
#include "Menu/ScctMenuKit.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
	// D3D's cached RHI memory counters can be compiled out of a Shipping build. Ask the
	// same adapter Windows assigned to this rendering device, including on hybrid laptops.
	TRefCountPtr<IDXGIAdapter3> GetRenderingAdapter()
	{
		TRefCountPtr<IDXGIAdapter3> Result;
		if (RHIGetInterfaceType() == ERHIInterfaceType::D3D12)
		{
			auto* Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			TRefCountPtr<IDXGIFactory4> Factory;
			if (Device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(Factory.GetInitReference()))))
			{
				Factory->EnumAdapterByLuid(Device->GetAdapterLuid(), IID_PPV_ARGS(Result.GetInitReference()));
			}
		}
		else if (RHIGetInterfaceType() == ERHIInterfaceType::D3D11)
		{
			auto* Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			TRefCountPtr<IDXGIDevice> DxgiDevice;
			TRefCountPtr<IDXGIAdapter> Adapter;
			if (Device && SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(DxgiDevice.GetInitReference()))) &&
				SUCCEEDED(DxgiDevice->GetAdapter(Adapter.GetInitReference())))
			{
				Adapter->QueryInterface(IID_PPV_ARGS(Result.GetInitReference()));
			}
		}
		return Result;
	}
#endif

	struct FVideoMemoryUsage
	{
		uint64 Used;
		uint64 Budget;
		bool bShared;
	};

	FVideoMemoryUsage SelectGpuHeap(const FRHIMemoryStats& Stats, bool bIntegrated)
	{
		if (Stats.BudgetLocal > 0)
		{
			return {Stats.UsedLocal, Stats.BudgetLocal, bIntegrated};
		}
		if (bIntegrated && Stats.BudgetSystem > 0)
		{
			return {Stats.UsedSystem, Stats.BudgetSystem, true};
		}
		return {0, 0, bIntegrated};
	}

	class SScctVideoMemory : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctVideoMemory) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&)
		{
			SetCanTick(true);
			ChildSlot
			[
				ScctUi::Card(FString(), SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(Title, STextBlock).Font(ScctUi::Body(true)).Text(FText::FromString(TEXT("Video memory (VRAM)")))
							.ColorAndOpacity(ScctHudStyle::Text())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
					[
						SAssignNew(Amount, STextBlock).Font(ScctUi::Small())
							.ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[
						SAssignNew(BarBox, SBox).HeightOverride(6.0f).Visibility(EVisibility::Collapsed)
						[
							SAssignNew(Bar, SProgressBar).Percent(0.0f).BorderPadding(FVector2D::ZeroVector)
						]
					], FMargin(26, 14))
			];
		}

		virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override
		{
			SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
			if (CurrentTime < NextRefresh) return;
			NextRefresh = CurrentTime + 1.0;
			FRHIMemoryStats Stats;
			if (GDynamicRHI)
			{
				RHIGetMemoryStats(Stats);
#if PLATFORM_WINDOWS
				if (!SelectGpuHeap(Stats, GRHIDeviceIsIntegrated).Budget)
				{
					if (!bCheckedAdapter)
					{
						DxgiAdapter = GetRenderingAdapter();
						bCheckedAdapter = true;
					}
					DXGI_QUERY_VIDEO_MEMORY_INFO Info{};
					if (DxgiAdapter && SUCCEEDED(DxgiAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info)))
					{
						Stats.UsedLocal = Info.CurrentUsage;
						Stats.BudgetLocal = Info.Budget;
					}
				}
#endif
			}
			const FVideoMemoryUsage Usage = SelectGpuHeap(Stats, GRHIDeviceIsIntegrated);
			Title->SetText(FText::FromString(Usage.bShared ? TEXT("Shared GPU memory") : TEXT("Video memory (VRAM)")));
			if (!Usage.Budget)
			{
				Amount->SetText(FText::FromString(TEXT("Usage and budget unavailable on this device.")));
				BarBox->SetVisibility(EVisibility::Collapsed);
				return;
			}
			constexpr double BytesPerGiB = 1024.0 * 1024.0 * 1024.0;
			const bool bOverBudget = Usage.Used > Usage.Budget;
			Amount->SetText(FText::FromString(FString::Printf(TEXT("Game usage %.2f GiB / %.2f GiB budget%s"),
				Usage.Used / BytesPerGiB, Usage.Budget / BytesPerGiB, bOverBudget ? TEXT("  (over budget)") : TEXT(""))));
			Bar->SetPercent(static_cast<float>(FMath::Min(static_cast<double>(Usage.Used) / Usage.Budget, 1.0)));
			Bar->SetFillColorAndOpacity(bOverBudget ? ScctHudStyle::Danger() : ScctUi::Accent());
			BarBox->SetVisibility(EVisibility::HitTestInvisible);
		}

	private:
		double NextRefresh = 0;
		TSharedPtr<STextBlock> Title, Amount;
		TSharedPtr<SBox> BarBox;
		TSharedPtr<SProgressBar> Bar;
#if PLATFORM_WINDOWS
		TRefCountPtr<IDXGIAdapter3> DxgiAdapter;
		bool bCheckedAdapter = false;
#endif
	};
}

TSharedRef<SWidget> ScctUi::VideoMemoryMeter()
{
	return SNew(SScctVideoMemory);
}


#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScctVideoMemoryHeapTest, "OpenSCCT.Menu.VideoMemory.HeapSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FScctVideoMemoryHeapTest::RunTest(const FString&)
{
	FRHIMemoryStats Stats;
	Stats.BudgetLocal = 8;
	Stats.UsedLocal = 9;
	Stats.BudgetSystem = 32;
	Stats.UsedSystem = 12;
	const FVideoMemoryUsage Dedicated = SelectGpuHeap(Stats, false);
	TestEqual(TEXT("Real usage remains visible over the local budget"), Dedicated.Used, uint64(9));
	TestEqual(TEXT("The local GPU budget excludes host memory"), Dedicated.Budget, uint64(8));
	TestTrue(TEXT("An integrated GPU labels its local heap shared"), SelectGpuHeap(Stats, true).bShared);
	Stats.BudgetLocal = 0;
	TestEqual(TEXT("Missing VRAM budget is unavailable on discrete GPUs"), SelectGpuHeap(Stats, false).Budget, uint64(0));
	const FVideoMemoryUsage Shared = SelectGpuHeap(Stats, true);
	TestEqual(TEXT("Integrated fallback uses the reported shared usage"), Shared.Used, uint64(12));
	TestEqual(TEXT("Integrated fallback uses the reported shared budget"), Shared.Budget, uint64(32));
	return true;
}
#endif
