#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Amp
{
	/** Feedback sounds from spec section 10.4. They are synthesised, so the game needs no audio assets for them. */
	enum class ESfx : uint8_t
	{
		Perfect,
		Good,
		Miss,
		Capture,
		Powerup,
		EnergyLow,
		GameOver,
		SongComplete,
		ShieldBreak,
		LaneClear,
		Countdown,
		UiMove,
		UiConfirm,
		UiBack
	};

	/**
	 * Small procedural sound effect synthesiser (a pool of enveloped oscillator voices).
	 * Like FStemMixer it is driven from the audio render thread.
	 */
	class FSfxSynth
	{
	public:
		void SetOutputFormat(int32_t InSampleRate, int32_t InNumChannels);
		void SetMasterGain(float InGain) { MasterGain = InGain; }
		void Trigger(ESfx Sfx);
		void StopAll();
		void Render(float* Out, int32_t NumFrames);

		int32_t GetActiveVoiceCount() const;

		/** Nominal length of an effect in seconds (spec 10.4.1 / 10.4.2). */
		static double GetDurationSeconds(ESfx Sfx);

	private:
		enum class EWave : uint8_t
		{
			Sine,
			Triangle,
			Square,
			Saw,
			Noise
		};

		struct FVoice
		{
			bool bActive = false;
			EWave Wave = EWave::Sine;
			double DelaySeconds = 0.0;
			double Age = 0.0;
			double Duration = 0.1;
			double Attack = 0.003;
			double StartHz = 440.0;
			double EndHz = 440.0;
			double Phase = 0.0;
			float Gain = 0.5f;
			uint32_t NoiseState = 0x12345678u;
		};

		void AddVoice(EWave Wave, double DelaySeconds, double Duration, double StartHz, double EndHz, float GainDb, double Attack = 0.003);
		static float Oscillate(FVoice& Voice, double PhaseIncrement);

		static constexpr size_t MaxVoices = 48;
		std::array<FVoice, MaxVoices> Voices;
		int32_t SampleRate = 48000;
		int32_t NumChannels = 2;
		float MasterGain = 1.0f;
		uint32_t NoiseSeed = 0x9E3779B9u;
	};
}
