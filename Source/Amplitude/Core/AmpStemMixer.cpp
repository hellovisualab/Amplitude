#include "Core/AmpStemMixer.h"

#include <algorithm>
#include <cmath>

namespace Amp
{
	namespace
	{
		constexpr float Int16ToFloat = 1.0f / 32768.0f;

		/** Linear interpolation of one channel of an interleaved track at a fractional frame. */
		float SampleTrack(const FPcmTrack& Track, int64_t Frame, float Fraction, int32_t Channel)
		{
			const int64_t NumFrames = Track.GetNumFrames();
			const int32_t Stride = Track.NumChannels;
			const size_t Index0 = static_cast<size_t>(Frame * Stride + Channel);
			const float A = static_cast<float>(Track.Samples[Index0]) * Int16ToFloat;
			if (Frame + 1 >= NumFrames)
			{
				return A * (1.0f - Fraction);
			}
			const float B = static_cast<float>(Track.Samples[Index0 + static_cast<size_t>(Stride)]) * Int16ToFloat;
			return A + (B - A) * Fraction;
		}
	}

	int64_t FSongAudio::GetLengthFrames() const
	{
		int64_t Length = 0;
		for (const FPcmTrack& Track : Lanes)
		{
			Length = std::max(Length, Track.GetNumFrames());
		}
		return Length;
	}

	double FSongAudio::GetLengthMs() const
	{
		return SampleRate > 0 ? static_cast<double>(GetLengthFrames()) * 1000.0 / SampleRate : 0.0;
	}

	bool FSongAudio::HasAnyAudio() const
	{
		for (const FPcmTrack& Track : Lanes)
		{
			if (!Track.IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	void FStemMixer::FRampedGain::RampTo(float InTarget, int32_t Frames)
	{
		Target = InTarget;
		if (Frames <= 0)
		{
			Current = Target;
			Step = 0.0f;
			return;
		}
		Step = (Target - Current) / static_cast<float>(Frames);
	}

	float FStemMixer::FRampedGain::Tick()
	{
		if (Step != 0.0f)
		{
			Current += Step;
			if ((Step > 0.0f && Current >= Target) || (Step < 0.0f && Current <= Target))
			{
				Current = Target;
				Step = 0.0f;
			}
		}
		return Current;
	}

	void FStemMixer::SetOutputFormat(int32_t InSampleRate, int32_t InNumChannels)
	{
		OutputSampleRate = std::max(1, InSampleRate);
		OutputChannels = std::max(1, InNumChannels);
	}

	void FStemMixer::SetSong(std::shared_ptr<const FSongAudio> InSong)
	{
		Song = std::move(InSong);
		PositionFrames = 0.0;
		for (FRampedGain& Gain : LaneGains)
		{
			Gain.RampTo(1.0f, 0);
		}
	}

	void FStemMixer::Seek(double SongMs)
	{
		const int32_t SourceRate = Song ? Song->SampleRate : OutputSampleRate;
		PositionFrames = SongMs * static_cast<double>(SourceRate) / 1000.0;
	}

	void FStemMixer::SetLaneGain(int32_t Lane, float TargetGain, double RampMs)
	{
		if (Lane < 0 || Lane >= NumLanes)
		{
			return;
		}
		LaneGains[static_cast<size_t>(Lane)].RampTo(TargetGain, static_cast<int32_t>(RampMs * OutputSampleRate / 1000.0));
	}

	void FStemMixer::SetMasterGain(float TargetGain, double RampMs)
	{
		MasterGain.RampTo(TargetGain, static_cast<int32_t>(RampMs * OutputSampleRate / 1000.0));
	}

	double FStemMixer::GetPositionMs() const
	{
		const int32_t SourceRate = Song ? Song->SampleRate : OutputSampleRate;
		return PositionFrames * 1000.0 / static_cast<double>(SourceRate);
	}

	void FStemMixer::Render(float* Out, int32_t NumFrames)
	{
		const size_t TotalSamples = static_cast<size_t>(NumFrames) * static_cast<size_t>(OutputChannels);
		std::fill(Out, Out + TotalSamples, 0.0f);
		if (!bPlaying || NumFrames <= 0)
		{
			return;
		}

		const int32_t SourceRate = Song ? Song->SampleRate : OutputSampleRate;
		const double Step = static_cast<double>(SourceRate) / static_cast<double>(OutputSampleRate) * Rate;

		for (int32_t Frame = 0; Frame < NumFrames; ++Frame)
		{
			const float Master = MasterGain.Tick();
			float Left = 0.0f;
			float Right = 0.0f;

			const double Position = PositionFrames;
			const int64_t Whole = static_cast<int64_t>(std::floor(Position));
			const float Fraction = static_cast<float>(Position - static_cast<double>(Whole));

			for (int32_t Lane = 0; Lane < NumLanes; ++Lane)
			{
				float Gain = LaneGains[static_cast<size_t>(Lane)].Tick();
				if (SoloLane >= 0 && SoloLane != Lane)
				{
					Gain = 0.0f;
				}
				if (!Song || Gain <= 0.0f || Whole < 0)
				{
					continue;
				}
				const FPcmTrack& Track = Song->Lanes[static_cast<size_t>(Lane)];
				if (Track.IsEmpty() || Whole >= Track.GetNumFrames())
				{
					continue;
				}
				if (Track.NumChannels == 1)
				{
					const float Value = SampleTrack(Track, Whole, Fraction, 0) * Gain;
					Left += Value;
					Right += Value;
				}
				else
				{
					Left += SampleTrack(Track, Whole, Fraction, 0) * Gain;
					Right += SampleTrack(Track, Whole, Fraction, 1) * Gain;
				}
			}

			float* Dest = Out + static_cast<size_t>(Frame) * static_cast<size_t>(OutputChannels);
			if (OutputChannels == 1)
			{
				Dest[0] = 0.5f * (Left + Right) * Master;
			}
			else
			{
				Dest[0] = Left * Master;
				Dest[1] = Right * Master;
			}

			// The play head keeps moving past the end of the audio so the song clock never stalls.
			PositionFrames += Step;
		}
	}
}
