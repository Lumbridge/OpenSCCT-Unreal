#pragma once

#include "CoreMinimal.h"
#include "OpenSCCT.h"
#include "Runtime/ScctAccountApi.h"
#include "Runtime/ScctAudioCheckApi.h"
#include "Runtime/ScctControlsApi.h"
#include "Runtime/ScctDemoApi.h"
#include "Runtime/ScctEquipmentApi.h"
#include "Runtime/ScctEventsApi.h"
#include "Runtime/ScctFlicFlocApi.h"
#include "Runtime/ScctFxTraceApi.h"
#include "Runtime/ScctGlassApi.h"
#include "Runtime/ScctHubActivityApi.h"
#include "Runtime/ScctHostApi.h"
#include "Runtime/ScctHubApi.h"
#include "Runtime/ScctHubPadsApi.h"
#include "Runtime/ScctHubSocialApi.h"
#include "Runtime/ScctHudApi.h"
#include "Runtime/ScctMenuApi.h"
#include "Runtime/ScctMarkSurfaceApi.h"
#include "Runtime/ScctOnlineApi.h"
#include "Runtime/ScctPawnLookApi.h"
#include "Runtime/ScctSocialApi.h"
#include "Runtime/ScctPlayApi.h"
#include "Runtime/ScctProjectionsApi.h"
#include "Runtime/ScctSmokeApi.h"
#include "Runtime/ScctSoftBodyApi.h"
#include "Runtime/ScctVisionApi.h"
#include "Runtime/ScctVoiceApi.h"
#include "Runtime/ScctWorldApi.h"
#include "Runtime/ScctWorldEmitterApi.h"
#include "Test/ScctGrabAreaApi.h"
#include "Test/ScctLessonApi.h"

// The bridge's feature tables by their C++ type: which bridge class hands each one over and which version the
// header here mirrors. A feature subsystem asks for its table like this, at any time and as often as it likes
// (the module fetches each table once and keeps it):
//
//   const FScctEventsApi* Events = ScctFeature<FScctEventsApi>();
//   if (Events == nullptr) { return; }   // the reason is in the log, once
//
// A new feature table needs its header included above and one line in the list below.

template <typename TTable>
struct TScctFeatureInfo;

#define SCCT_FEATURE_TABLE(TTable, BridgeClass, VersionConstant) \
	template <> struct TScctFeatureInfo<TTable> \
	{ \
		static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge." BridgeClass); } \
		static constexpr int32 Version = VersionConstant; \
	}

SCCT_FEATURE_TABLE(FScctAccountApi, "AccountExports", ScctAccountApiVersion);
SCCT_FEATURE_TABLE(FScctAudioCheckApi, "AudioCheckExports", ScctAudioCheckApiVersion);
SCCT_FEATURE_TABLE(FScctControlsApi, "ControlsExports", ScctControlsApiVersion);
SCCT_FEATURE_TABLE(FScctDemoApi, "DemoExports", ScctDemoApiVersion);
SCCT_FEATURE_TABLE(FScctEquipmentApi, "EquipmentExports", ScctEquipmentApiVersion);
SCCT_FEATURE_TABLE(FScctEventsApi, "EventsExports", ScctEventsApiVersion);
SCCT_FEATURE_TABLE(FScctFlicFlocApi, "FlicFlocExports", ScctFlicFlocApiVersion);
SCCT_FEATURE_TABLE(FScctFxTraceApi, "FxTraceExports", ScctFxTraceApiVersion);
SCCT_FEATURE_TABLE(FScctGlassApi, "GlassExports", ScctGlassApiVersion);
SCCT_FEATURE_TABLE(FScctHostApi, "HostExports", ScctHostApiVersion);
SCCT_FEATURE_TABLE(FScctHubApi, "HubExports", ScctHubApiVersion);
SCCT_FEATURE_TABLE(FScctHubSocialApi, "HubSocialExports", ScctHubSocialApiVersion);
SCCT_FEATURE_TABLE(FScctHubPadsApi, "HubPadsExports", ScctHubPadsApiVersion);
SCCT_FEATURE_TABLE(FScctHubActivityApi, "HubActivityExports", ScctHubActivityApiVersion);
SCCT_FEATURE_TABLE(FScctHudApi, "HudExports", ScctHudApiVersion);
SCCT_FEATURE_TABLE(FScctMenuApi, "MenuExports", ScctMenuApiVersion);
SCCT_FEATURE_TABLE(FScctMarkSurfaceApi, "MarkSurfaceExports", ScctMarkSurfaceApiVersion);
SCCT_FEATURE_TABLE(FScctOnlineApi, "OnlineExports", ScctOnlineApiVersion);
SCCT_FEATURE_TABLE(FScctPawnLookApi, "PawnLookExports", ScctPawnLookApiVersion);
SCCT_FEATURE_TABLE(FScctSocialApi, "SocialExports", ScctSocialApiVersion);
SCCT_FEATURE_TABLE(FScctPlayApi, "PlayExports", ScctPlayApiVersion);
SCCT_FEATURE_TABLE(FScctProjectionsApi, "ProjectionExports", ScctProjectionsApiVersion);
SCCT_FEATURE_TABLE(FScctSmokeApi, "SmokeExports", ScctSmokeApiVersion);
SCCT_FEATURE_TABLE(FScctSoftBodyApi, "SoftBodyExports", ScctSoftBodyApiVersion);
SCCT_FEATURE_TABLE(FScctVisionApi, "VisionExports", ScctVisionApiVersion);
SCCT_FEATURE_TABLE(FScctVoiceApi, "VoiceExports", ScctVoiceApiVersion);
SCCT_FEATURE_TABLE(FScctWorldApi, "WorldExports", ScctWorldApiVersion);
SCCT_FEATURE_TABLE(FScctWorldEmitterApi, "WorldEmitterExports", ScctWorldEmitterApiVersion);
SCCT_FEATURE_TABLE(FScctGrabAreaApi, "GrabAreaExports", ScctGrabAreaApiVersion);
SCCT_FEATURE_TABLE(FScctLessonApi, "LessonExports", ScctLessonApiVersion);

#undef SCCT_FEATURE_TABLE

/**
 * The entry points of a bridge feature, or null when the bridge is not up or does not have the feature in the
 * version this game was built against. The pointer stays valid until the game module shuts down.
 */
template <typename TTable>
const TTable* ScctFeature(FString* OutError = nullptr)
{
	// Once fetched a table never changes or goes away while the game runs, so callers that ask every frame get
	// it back without a lookup. A failure is asked again (the content folder may have been set since).
	static const TTable* Kept = nullptr;
	if (Kept != nullptr)
	{
		if (OutError != nullptr)
		{
			OutError->Reset();
		}
		return Kept;
	}

	FString Error;
	Kept = static_cast<const TTable*>(FOpenSCCTModule::Get().FindFeatureTable(
		TScctFeatureInfo<TTable>::TypeName(), sizeof(TTable), TScctFeatureInfo<TTable>::Version, Error));
	if (OutError != nullptr)
	{
		*OutError = MoveTemp(Error);
	}
	return Kept;
}
