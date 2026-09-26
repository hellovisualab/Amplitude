#include "Core/AmpSfxSynth.h"

#include <algorithm>
#include <cmath>

namespace Amp
{
	namespace
	{
		constexpr double TwoPi = 6.283185307179586;
		/** Amplitude of a 0 dB (reference level) effect, leaving headroom for overlapping voices. */
		constexpr float ReferenceAmplitude = 0.3f;
		/** Alerts sit 3 dB above hit sounds (spec 10.4.3). */
		constexpr float AlertDb = 3.0f;
		constexpr double ReleaseSeconds = 0.005;

		float DbToGain(float Db)
		{
			return std::pow(10.0f, Db / 20.0f);
		}
	}

	void FSfxSynth::SetOutputFormat(int32_t InSampleRate, int32_t InNumChannels)
	{
		SampleRate = std::max(1, InSampleRate);
		NumChannels = std::max(1, InNumChannels);
	}

	double FSfxSynth::GetDurationSeconds(ESfx Sfx)
	{
		switch (Sfx)
		{
		case ESfx::Perfect:
		case ESfx::Good:
			return 0.15;
		case ESfx::Miss:
			return 0.2;
		case ESfx::Capture:
			return 0.3;
		case ESfx::Powerup:
			return 0.25;
		case ESfx::EnergyLow:
			return 0.24;
		case ESfx::GameOver:
			return 0.5;
		case ESfx::SongComplete:
			return 1.0;
		case ESfx::ShieldBreak:
			return 0.15;
		case ESfx::LaneClear:
			return 0.25;
		case ESfx::Countdown:
			return 0.08;
		case ESfx::UiMove:
			return 0.03;
		case ESfx::UiConfirm:
		case ESfx::UiBack:
			return 0.07;
		}
		return 0.1;
	}

	void FSfxSynth::Trigger(ESfx Sfx)
	{
		switch (Sfx)
		{
		case ESfx::Perfect: // high-pitched "ding"
			AddVoice(EWave::Sine, 0.0, 0.15, 1760.0, 1760.0, 0.0f);
			AddVoice(EWave::Sine, 0.0, 0.12, 2640.0, 2640.0, -8.0f);
			break;

		case ESfx::Good: // mid-range "chime"
			AddVoice(EWave::Sine, 0.0, 0.15, 1046.5, 1046.5, 0.0f);
			AddVoice(EWave::Sine, 0.0, 0.12, 1568.0, 1568.0, -9.0f);
			AddVoice(EWave::Triangle, 0.0, 0.1, 523.25, 523.25, -12.0f);
			break;

		case ESfx::Miss: // descending "buzz"
			AddVoice(EWave::Saw, 0.0, 0.2, 240.0, 90.0, -4.0f);
			AddVoice(EWave::Square, 0.0, 0.2, 120.0, 60.0, -12.0f);
			break;

		case ESfx::Capture: // ascending "sweep"
			AddVoice(EWave::Sine, 0.0, 0.3, 330.0, 1320.0, 0.0f, 0.02);
			AddVoice(EWave::Triangle, 0.0, 0.3, 660.0, 2640.0, -8.0f, 0.02);
			break;

		case ESfx::Powerup: // uplifting "sparkle"
			AddVoice(EWave::Sine, 0.00, 0.1, 1318.5, 1318.5, -2.0f);
			AddVoice(EWave::Sine, 0.05, 0.1, 1568.0, 1568.0, -2.0f);
			AddVoice(EWave::Sine, 0.10, 0.1, 2093.0, 2093.0, -2.0f);
			AddVoice(EWave::Sine, 0.15, 0.1, 2637.0, 2637.0, -2.0f);
			break;

		case ESfx::EnergyLow: // pulsing warning (the caller repeats it while energy stays low)
			AddVoice(EWave::Square, 0.00, 0.09, 660.0, 660.0, AlertDb - 9.0f);
			AddVoice(EWave::Square, 0.15, 0.09, 660.0, 660.0, AlertDb - 9.0f);
			break;

		case ESfx::GameOver: // sad descending tone
			AddVoice(EWave::Triangle, 0.00, 0.16, 440.0, 415.3, AlertDb);
			AddVoice(EWave::Triangle, 0.16, 0.16, 370.0, 349.2, AlertDb);
			AddVoice(EWave::Triangle, 0.32, 0.18, 293.7, 220.0, AlertDb);
			AddVoice(EWave::Sine, 0.00, 0.5, 110.0, 55.0, AlertDb - 6.0f);
			break;

		case ESfx::SongComplete: // victory fanfare
			AddVoice(EWave::Square, 0.00, 0.15, 523.25, 523.25, AlertDb - 8.0f);
			AddVoice(EWave::Square, 0.12, 0.15, 659.25, 659.25, AlertDb - 8.0f);
			AddVoice(EWave::Square, 0.24, 0.15, 783.99, 783.99, AlertDb - 8.0f);
			AddVoice(EWave::Square, 0.36, 0.64, 1046.5, 1046.5, AlertDb - 8.0f);
			AddVoice(EWave::Triangle, 0.36, 0.64, 523.25, 523.25, AlertDb - 4.0f);
			break;

		case ESfx::ShieldBreak:
			AddVoice(EWave::Noise, 0.0, 0.12, 1.0, 1.0, -8.0f);
			AddVoice(EWave::Sine, 0.0, 0.15, 1200.0, 300.0, -3.0f);
			break;

		case ESfx::LaneClear:
			AddVoice(EWave::Triangle, 0.0, 0.25, 2000.0, 200.0, -3.0f);
			AddVoice(EWave::Noise, 0.0, 0.2, 1.0, 1.0, -14.0f);
			break;

		case ESfx::Countdown:
			AddVoice(EWave::Sine, 0.0, 0.08, 1000.0, 1000.0, -3.0f);
			break;

		case ESfx::UiMove:
			AddVoice(EWave::Sine, 0.0, 0.03, 1400.0, 1400.0, -12.0f);
			break;

		case ESfx::UiConfirm:
			AddVoice(EWave::Sine, 0.0, 0.07, 880.0, 1320.0, -6.0f);
			break;

		case ESfx::UiBack:
			AddVoice(EWave::Sine, 0.0, 0.07, 660.0, 440.0, -6.0f);
			break;
		}
	}

	void FSfxSynth::StopAll()
	{
		for (FVoice& Voice : Voices)
		{
			Voice.bActive = false;
		}
	}

	int32_t FSfxSynth::GetActiveVoiceCount() const
	{
		int32_t Count = 0;
		for (const FVoice& Voice : Voices)
		{
			Count += Voice.bActive ? 1 : 0;
		}
		return Count;
	}

	void FSfxSynth::AddVoice(EWave Wave, double DelaySeconds, double Duration, double StartHz, double EndHz, float GainDb, double Attack)
	{
		FVoice* Slot = nullptr;
		for (FVoice& Voice : Voices)
		{
			if (!Voice.bActive)
			{
				Slot = &Voice;
				break;
			}
		}
		if (Slot == nullptr)
		{
			// Steal the voice that is furthest through its sound.
			Slot = &Voices[0];
			for (FVoice& Voice : Voices)
			{
				if (Voice.Age - Voice.DelaySeconds > Slot->Age - Slot->DelaySeconds)
				{
					Slot = &Voice;
				}
			}
		}

		NoiseSeed = NoiseSeed * 1664525u + 1013904223u;

		FVoice& Voice = *Slot;
		Voice = FVoice();
		Voice.bActive = true;
		Voice.Wave = Wave;
		Voice.DelaySeconds = DelaySeconds;
		Voice.Duration = std::max(0.01, Duration);
		Voice.Attack = std::min(Attack, Voice.Duration * 0.5);
		Voice.StartHz = StartHz;
		Voice.EndHz = EndHz;
		Voice.Gain = ReferenceAmplitude * DbToGain(GainDb);
		Voice.NoiseState = NoiseSeed | 1u;
	}

	float FSfxSynth::Oscillate(FVoice& Voice, double PhaseIncrement)
	{
		float Value = 0.0f;
		switch (Voice.Wave)
		{
		case EWave::Sine:
			Value = static_cast<float>(std::sin(TwoPi * Voice.Phase));
			break;
		case EWave::Triangle:
			Value = static_cast<float>(1.0 - 4.0 * std::abs(Voice.Phase - 0.5));
			break;
		case EWave::Square:
			Value = Voice.Phase < 0.5 ? 0.6f : -0.6f;
			break;
		case EWave::Saw:
			Value = static_cast<float>(2.0 * Voice.Phase - 1.0) * 0.7f;
			break;
		case EWave::Noise:
			Voice.NoiseState ^= Voice.NoiseState << 13;
			Voice.NoiseState ^= Voice.NoiseState >> 17;
			Voice.NoiseState ^= Voice.NoiseState << 5;
			Value = static_cast<float>(Voice.NoiseState) / 2147483648.0f - 1.0f;
			break;
		}
		Voice.Phase += PhaseIncrement;
		Voice.Phase -= std::floor(Voice.Phase);
		return Value;
	}

	void FSfxSynth::Render(float* Out, int32_t NumFrames)
	{
		const double Dt = 1.0 / static_cast<double>(SampleRate);
		for (int32_t Frame = 0; Frame < NumFrames; ++Frame)
		{
			float Mix = 0.0f;
			for (FVoice& Voice : Voices)
			{
				if (!Voice.bActive)
				{
					continue;
				}
				const double Time = Voice.Age - Voice.DelaySeconds;
				Voice.Age += Dt;
				if (Time < 0.0)
				{
					continue;
				}
				if (Time >= Voice.Duration)
				{
					Voice.bActive = false;
					continue;
				}

				const double Progress = Time / Voice.Duration;
				const double Frequency = Voice.StartHz * std::pow(Voice.EndHz / Voice.StartHz, Progress);

				double Envelope;
				if (Time < Voice.Attack)
				{
					Envelope = Time / Voice.Attack;
				}
				else
				{
					const double DecayProgress = (Time - Voice.Attack) / std::max(1e-6, Voice.Duration - Voice.Attack);
					Envelope = std::exp(-4.0 * DecayProgress);
				}
				Envelope *= std::min(1.0, (Voice.Duration - Time) / ReleaseSeconds);

				Mix += Oscillate(Voice, Frequency * Dt) * Voice.Gain * static_cast<float>(Envelope);
			}

			const float Sample = std::clamp(Mix * MasterGain, -1.0f, 1.0f);
			float* Dest = Out + static_cast<size_t>(Frame) * static_cast<size_t>(NumChannels);
			for (int32_t Channel = 0; Channel < NumChannels; ++Channel)
			{
				Dest[Channel] = Channel < 2 ? Sample : 0.0f;
			}
		}
	}
}
