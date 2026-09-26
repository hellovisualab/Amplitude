#pragma once

#include "Core/AmpTypes.h"
#include "Core/AmpWav.h"

#include <array>
#include <memory>

namespace Amp
{
	/** Decoded song audio: one track per lane instrument (spec 10.1.1). Missing instruments stay empty. */
	struct FSongAudio
	{
		int32_t SampleRate = 44100;
		std::array<FPcmTrack, NumLanes> Lanes;

		int64_t GetLengthFrames() const;
		double GetLengthMs() const;
		bool HasAnyAudio() const;
	};

	/**
	 * Real-time multitrack mixer. Runs on the audio render thread; all setters must be called from
	 * that thread (the Unreal wrapper forwards them as synth commands).
	 *
	 * - Each lane has its own gain with a linear ramp (200ms mute/unmute crossfades, spec 10.3.2).
	 * - Playback rate is applied by resampling, so 0.5x also drops the pitch by an octave (Slow Motion).
	 * - The play head may start before 0 to give the player a silent lead-in.
	 */
	class FStemMixer
	{
	public:
		void SetOutputFormat(int32_t InSampleRate, int32_t InNumChannels);
		void SetSong(std::shared_ptr<const FSongAudio> InSong);
		void Seek(double SongMs);
		void SetPlaying(bool bInPlaying) { bPlaying = bInPlaying; }
		void SetRate(double InRate) { Rate = InRate > 0.0 ? InRate : 1.0; }
		void SetLaneGain(int32_t Lane, float TargetGain, double RampMs);
		void SetMasterGain(float TargetGain, double RampMs = 50.0);
		/** -1 disables solo; otherwise only that lane is audible (debug/testing mode). */
		void SetSoloLane(int32_t Lane) { SoloLane = Lane; }

		/** Renders NumFrames interleaved frames into Out (always writes every sample). */
		void Render(float* Out, int32_t NumFrames);

		double GetPositionMs() const;
		double GetPositionFrames() const { return PositionFrames; }
		bool IsPlaying() const { return bPlaying; }
		double GetRate() const { return Rate; }
		float GetLaneGain(int32_t Lane) const { return LaneGains[static_cast<size_t>(Lane)].Current; }
		int32_t GetOutputSampleRate() const { return OutputSampleRate; }
		int32_t GetOutputChannels() const { return OutputChannels; }

	private:
		struct FRampedGain
		{
			float Current = 1.0f;
			float Target = 1.0f;
			float Step = 0.0f;

			void RampTo(float InTarget, int32_t Frames);
			float Tick();
		};

		std::shared_ptr<const FSongAudio> Song;
		std::array<FRampedGain, NumLanes> LaneGains;
		FRampedGain MasterGain;
		double PositionFrames = 0.0;
		double Rate = 1.0;
		int32_t OutputSampleRate = 48000;
		int32_t OutputChannels = 2;
		int32_t SoloLane = -1;
		bool bPlaying = false;
	};
}
