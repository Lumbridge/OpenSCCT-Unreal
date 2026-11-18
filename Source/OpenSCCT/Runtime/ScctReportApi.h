#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// Local game details and project links for Help & feedback. Mirrored by Menu/ReportExports.cs.
constexpr int32 ScctReportApiVersion = 2;

namespace EScctReportLink
{
	constexpr int32 FeatureRequest = 1;
	constexpr int32 Issues = 2;
	constexpr int32 Documentation = 3;
	constexpr int32 Contributing = 4;
	constexpr int32 Development = 5;
	constexpr int32 PullRequests = 6;
	constexpr int32 NewPullRequest = 7;
	constexpr int32 Help = 8;
}

struct FScctReportApi
{
	int32 Version;

	/** The local game details (ProjectLinks.EnvironmentDetails), cut to fit and zero terminated; returns their full length. */
	int32 (__cdecl* GetDetails)(TCHAR* Buffer, int32 Capacity);

	/** The address for an EScctReportLink. */
	int32 (__cdecl* GetUrl)(int32 Link, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctReportApi) == 24, "FScctReportApi must match ReportApi (64-bit)");

template <> struct TScctFeatureInfo<FScctReportApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.ReportExports"); }
	static constexpr int32 Version = ScctReportApiVersion;
};
