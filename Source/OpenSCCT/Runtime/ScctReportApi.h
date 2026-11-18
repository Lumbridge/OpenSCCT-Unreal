#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The Help & feedback page's report, from the C# bridge (Bridge/OpenScct.UnrealBridge/Menu/ReportExports.cs, the
// Godot client's GameReportContext, ProjectLinks and ReportScreenshotUpload): the details the website's report form is
// prefilled with (the build, menus or offline or online or hub or demo, the map, mode and side, and the local pawn's
// position and movement state), the project's link addresses, and the screenshot upload to the website's capture
// endpoint, which runs only when Menu/SScctHelpFeedbackPage.cpp asks for it (the player pressed Attach screenshot).
//
// This mirrors ReportExports.cs; change both together and bump the version.

constexpr int32 ScctReportApiVersion = 1;

/** Where a link goes (BridgeReportLink, ProjectDestination's order). */
namespace EScctReportLink
{
	constexpr int32 BugReport = 0;
	constexpr int32 FeatureRequest = 1;
	constexpr int32 Issues = 2;
	constexpr int32 Documentation = 3;
	constexpr int32 Contributing = 4;
	constexpr int32 Development = 5;
	constexpr int32 PullRequests = 6;
	constexpr int32 NewPullRequest = 7;
	constexpr int32 Help = 8;
}

/** PollUpload's answers. */
namespace EScctReportUpload
{
	constexpr int32 None = 0;
	constexpr int32 Running = 1;
	constexpr int32 Done = 2;   // the 64 hex digit capture token is written
	constexpr int32 Failed = 3; // the reason is written
}

/** The website refuses a bigger capture (ReportExports.MaxScreenshotBytes). */
constexpr int32 ScctReportMaxScreenshotBytes = 2 * 1024 * 1024;

struct FScctReportApi
{
	int32 Version;

	/** The report details (ProjectLinks.EnvironmentDetails), cut to fit and zero terminated; returns their full length. */
	int32 (__cdecl* GetDetails)(TCHAR* Buffer, int32 Capacity);

	/** A link's address (EScctReportLink); the bug report's carries the details and Capture (null or empty: none). */
	int32 (__cdecl* GetUrl)(int32 Link, const TCHAR* Capture, TCHAR* Buffer, int32 Capacity);

	/** Starts posting a PNG (copied first): 1 started, 0 refused (one already going, empty, or over the limit). */
	int32 (__cdecl* BeginUpload)(const uint8* Png, int32 Length);

	/** EScctReportUpload; a finished upload is reported once. */
	int32 (__cdecl* PollUpload)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctReportApi) == 40, "FScctReportApi must match ReportApi (64-bit)");

template <> struct TScctFeatureInfo<FScctReportApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.ReportExports"); }
	static constexpr int32 Version = ScctReportApiVersion;
};
