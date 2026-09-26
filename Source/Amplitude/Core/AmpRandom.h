#pragma once

#include <cstddef>
#include <cstdint>

namespace Amp
{
	/**
	 * PCG32 generator. Used instead of <random> so powerup spawns are identical on every platform
	 * for a given seed (std distributions are implementation defined).
	 */
	class FRandom
	{
	public:
		explicit FRandom(uint64_t InSeed = 0x853c49e6748fea9bULL) { Reset(InSeed); }

		void Reset(uint64_t InSeed)
		{
			State = 0;
			NextU32();
			State += InSeed;
			NextU32();
		}

		uint32_t NextU32()
		{
			const uint64_t OldState = State;
			State = OldState * 6364136223846793005ULL + Increment;
			const uint32_t XorShifted = static_cast<uint32_t>(((OldState >> 18u) ^ OldState) >> 27u);
			const uint32_t Rotation = static_cast<uint32_t>(OldState >> 59u);
			return (XorShifted >> Rotation) | (XorShifted << ((~Rotation + 1u) & 31u));
		}

		/** Uniform double in [0, 1). */
		double NextDouble() { return static_cast<double>(NextU32()) / 4294967296.0; }

		/** Uniform double in [Min, Max). */
		double Range(double Min, double Max) { return Min + (Max - Min) * NextDouble(); }

		/** Uniform integer in [Min, MaxInclusive]. */
		int32_t RangeInt(int32_t Min, int32_t MaxInclusive)
		{
			if (MaxInclusive <= Min)
			{
				return Min;
			}
			const uint32_t Span = static_cast<uint32_t>(MaxInclusive - Min) + 1u;
			return Min + static_cast<int32_t>(NextU32() % Span);
		}

		/** Picks an index proportionally to Weights. Returns Count - 1 if every weight is zero. */
		size_t PickWeighted(const double* Weights, size_t Count)
		{
			double Total = 0.0;
			for (size_t Index = 0; Index < Count; ++Index)
			{
				Total += Weights[Index] > 0.0 ? Weights[Index] : 0.0;
			}
			if (Count == 0 || Total <= 0.0)
			{
				return Count == 0 ? 0 : Count - 1;
			}
			double Roll = NextDouble() * Total;
			for (size_t Index = 0; Index < Count; ++Index)
			{
				const double Weight = Weights[Index] > 0.0 ? Weights[Index] : 0.0;
				if (Roll < Weight)
				{
					return Index;
				}
				Roll -= Weight;
			}
			return Count - 1;
		}

	private:
		static constexpr uint64_t Increment = 1442695040888963407ULL;
		uint64_t State = 0;
	};
}
