#pragma once

#include "CoreMinimal.h"

// The frame-rate and network readouts for this frame (UScctTelemetrySubsystem fills them, ScctHudElementsTelemetry.cpp
// draws them). Text is ready to draw; the painter only lays it out.

/** One line of a readout: a label in the left column (may be empty) and its values. */
struct FScctTelemetryRow
{
	FString Label;
	FString Value;
	bool bAlert = false;   // drawn in the warning colour (a problem now, or one held for the issue hold time)
};

/** A history graph under a readout: values oldest first, spread over the graph's width. */
struct FScctTelemetryGraph
{
	FString Caption;
	TArray<float> Values;
	float Maximum = 1.0f;        // the value at the graph's top
	float Threshold = 0.0f;      // a line across the graph at this value; 0 for none
	float Fill = 1.0f;           // the share of the width the history covers (a short history at start-up)
	FLinearColor Colour = FLinearColor::White;
};

struct FScctTelemetryPanel
{
	bool bShown = false;
	FString Header;              // "NETWORK"; empty for none
	FString Tag;                 // right of the header ("1s sample")
	TArray<FScctTelemetryRow> Rows;
	TArray<FScctTelemetryGraph> Graphs;
	bool bIssue = false;
};

namespace EScctTelemetryCorner
{
	enum Type : int32
	{
		TopRight = 0,
		TopLeft = 1,
		BottomRight = 2,
		BottomLeft = 3,
	};
}

struct FScctTelemetryView
{
	FScctTelemetryPanel Fps;
	FScctTelemetryPanel Net;
	int32 Corner = EScctTelemetryCorner::TopRight;
	float Scale = 1.0f;
	float Opacity = 1.0f;
};
