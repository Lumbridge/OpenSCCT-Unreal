#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The key bindings editor of the C# bridge (Bridge/OpenScct.UnrealBridge/Controls/BindingsEditor.cs): the actions a
// side can bind in the Godot client's order with their labels, the keys each has, conflicts, and a draft of both
// sides' layouts that the Options screen applies, saves or throws away. Saving the file itself is the controls
// table's SaveBindings (UScctControlSettings::SaveBindings).
//
// This mirrors Bridge/OpenScct.UnrealBridge/Controls/BindingsExports.cs field for field; change both together and
// bump the version. Strings are UTF-16 and zero terminated; a text handed back is copied as far as it fits and its
// full length is returned. Nothing here needs a session.

constexpr int32 ScctBindingsApiVersion = 1;

/** How a key that already does something takes the new action (BindingCommitMode). */
enum class EScctBindingCommit : int32
{
	Share = 0,   // the key keeps what it does and does the action too (also the mode for a free key)
	Replace = 1, // the key does only the action
	Swap = 2,    // the key and the slot being changed trade what they do
};

/** Bits of the flags Conflict hands back. */
namespace EScctBindingConflict
{
	constexpr int32 AlreadyBound = 1; // the key is already one of the action's: nothing to do
	constexpr int32 CanSwap = 2;      // the key and the slot being changed can trade
}

struct FScctBindingsApi
{
	int32 Version;

	/** Starts editing from the bindings in play; edits already held (a rebuilt screen) are kept. Returns 1. */
	int32 (__cdecl* Begin)();

	/**
	 * The side's rows (bMerc 0 spy, 1 merc), one line per action, fields split by tabs: id, category, label,
	 * description, double tap allowed (1/0), then the action's keys by binding name, primary first.
	 */
	int32 (__cdecl* GetRows)(int32 bMerc, TCHAR* Buffer, int32 Capacity);

	/** Why Key cannot be bound to the action on that side; length 0 when it can. */
	int32 (__cdecl* Check)(int32 bMerc, const TCHAR* ActionId, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/**
	 * What Key does apart from the action, by action names (length 0 when free or already the action's). OldKey is
	 * the slot being changed ("" for a new one); Flags gets EScctBindingConflict bits.
	 */
	int32 (__cdecl* Conflict)(int32 bMerc, const TCHAR* ActionId, const TCHAR* OldKey, const TCHAR* Key, int32* Flags, TCHAR* Buffer, int32 Capacity);

	/** Binds Key to the action in place of OldKey ("" adds a key). Returns 1, or 0 with the reason in Error. */
	int32 (__cdecl* Commit)(int32 bMerc, const TCHAR* ActionId, const TCHAR* OldKey, const TCHAR* Key, int32 Mode, TCHAR* Error, int32 Capacity);

	/** Takes the action off one key; the key's other commands stay. */
	void (__cdecl* Clear)(int32 bMerc, const TCHAR* ActionId, const TCHAR* Key);

	/** Puts a side's keys (the shared ones while shared) back to the defaults in the draft. */
	void (__cdecl* ResetKeys)(int32 bMerc);

	/** 1 when each side has its own key layout in the draft, 0 when both play the spy's. */
	int32 (__cdecl* GetSplit)();
	void (__cdecl* SetSplit)(int32 bSplit);

	/** The draft differs from what is in play / what is in play differs from the file. */
	int32 (__cdecl* HasEdits)();
	int32 (__cdecl* HasUnsaved)();

	/** Puts the draft in play: a running match plays the new keys from its next frame. */
	void (__cdecl* Apply)();

	/** Drops the draft and puts the file's bindings back in play. */
	void (__cdecl* Discard)();

	/** What a key does in the draft, by action names; length 0 when unbound. */
	int32 (__cdecl* Describe)(int32 bMerc, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/** The last failure inside this table. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The size the C# side has (Bridge/Probes/BindingsProbe prints it).
static_assert(sizeof(FScctBindingsApi) == 128, "FScctBindingsApi must match BindingsApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctBindingsApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.BindingsExports"); }
	static constexpr int32 Version = ScctBindingsApiVersion;
};
