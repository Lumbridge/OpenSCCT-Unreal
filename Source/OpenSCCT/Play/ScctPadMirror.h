#pragma once

#include <array>
#include <cstdint>

// Each bit is a controller button or stick direction. Pair only distinct presses of the same
// control with the same injected input. A lone controller press breaks its learning streak.
class FScctPadMirror
{
public:
	using Mask = std::uint32_t;
	static constexpr double PairSeconds = 0.060;
	static constexpr int Confirmations = 3;

	bool Sample(Mask Next, double Now)
	{
		const Mask Pressed = Next & ~Held;
		Held = Next;
		for (unsigned Index = 0; Index < Slots.size(); ++Index)
		{
			if (!(Pressed & (Mask(1) << Index))) continue;
			FPair& Pair = Slots[Index];
			if (!Pair.Paired) Pair.Count = 0;
			Pair.At = Now;
			Pair.Paired = false;
		}
		// The keyboard message may have reached Windows before the pad reading changed.
		if (Pending && Now - PendingAt <= PairSeconds)
		{
			const Mask Candidates = Pressed & PendingMask;
			if (Single(Candidates))
			{
				const bool Drop = Match(Candidates, Pending) >= Confirmations || PendingEquivalent;
				Pending = 0;
				return Drop;
			}
		}
		return false;
	}

	bool Input(int Code, Mask Allowed, double Now, bool KnownEquivalent, bool Continuous = false)
	{
		Mask Candidates = 0;
		for (unsigned Index = 0; Index < Slots.size(); ++Index)
		{
			const Mask Bit = Mask(1) << Index;
			if (!(Allowed & Bit)) continue;
			const FPair& Pair = Slots[Index];
			if (Continuous && (Held & Bit) && Pair.Code == Code && Pair.Count >= Confirmations)
				return true;
			if (!Pair.Paired && Now - Pair.At <= PairSeconds) Candidates |= Bit;
		}
		if (Single(Candidates))
		{
			Pending = 0;
			return Match(Candidates, Code) >= Confirmations || KnownEquivalent;
		}
		Pending = Code;
		PendingAt = Now;
		PendingMask = Allowed;
		PendingEquivalent = KnownEquivalent;
		return false;
	}

	bool MayMatchPending() const
	{
		if (!Pending) return false;
		if (PendingEquivalent) return true;
		for (unsigned Index = 0; Index < Slots.size(); ++Index)
			if ((PendingMask & (Mask(1) << Index)) && Slots[Index].Code == Pending && Slots[Index].Count >= Confirmations - 1)
				return true;
		return false;
	}
	void ForgetPending() { Pending = 0; }

private:
	struct FPair
	{
		double At = -1e9;
		int Code = 0;
		int Count = 0;
		bool Paired = true;
	};
	static bool Single(Mask Bits) { return Bits && !(Bits & (Bits - 1)); }

	int Match(Mask Bit, int Code)
	{
		unsigned Index = 0;
		while ((Bit >>= 1) != 0) ++Index;
		FPair& Pair = Slots[Index];
		Pair.Count = Pair.Code == Code ? (Pair.Count < Confirmations ? Pair.Count + 1 : Confirmations) : 1;
		Pair.Code = Code;
		Pair.Paired = true;
		return Pair.Count;
	}

	std::array<FPair, 32> Slots{};
	Mask Held = 0;
	int Pending = 0;
	double PendingAt = 0;
	Mask PendingMask = 0;
	bool PendingEquivalent = false;
};
