#include "TestFramework.h"

#include "Core/AmpSfxSynth.h"
#include "Core/AmpSongClock.h"
#include "Core/AmpStemMixer.h"
#include "Core/AmpWav.h"

#include <cstring>

using namespace Amp;

namespace
{
	/** 7-channel test file: channel c holds the constant value (c + 1) * 1000 on every frame. */
	std::vector<uint8_t> MakeSevenChannelWav(int32_t Frames)
	{
		std::vector<int16_t> Samples;
		for (int32_t Frame = 0; Frame < Frames; ++Frame)
		{
			for (int32_t Channel = 0; Channel < 7; ++Channel)
			{
				Samples.push_back(static_cast<int16_t>((Channel + 1) * 1000));
			}
		}
		return EncodeWav16(Samples, 7, 44100);
	}

	void PutU16(std::vector<uint8_t>& Out, uint32_t Value)
	{
		Out.push_back(static_cast<uint8_t>(Value & 0xFF));
		Out.push_back(static_cast<uint8_t>((Value >> 8) & 0xFF));
	}

	void PutU32(std::vector<uint8_t>& Out, uint32_t Value)
	{
		PutU16(Out, Value & 0xFFFF);
		PutU16(Out, Value >> 16);
	}

	void PutId(std::vector<uint8_t>& Out, const char* Id)
	{
		Out.insert(Out.end(), Id, Id + 4);
	}

	/** Builds a WAV with an arbitrary fmt chunk and raw data bytes. */
	std::vector<uint8_t> MakeWav(uint16_t Tag, uint16_t Channels, uint16_t Bits, const std::vector<uint8_t>& Data, bool bExtensible = false, uint16_t SubFormat = 1)
	{
		std::vector<uint8_t> Out;
		const uint32_t FmtSize = bExtensible ? 40 : 16;
		PutId(Out, "RIFF");
		PutU32(Out, 4 + 8 + FmtSize + 8 + 12 + 8 + static_cast<uint32_t>(Data.size()));
		PutId(Out, "WAVE");
		PutId(Out, "fmt ");
		PutU32(Out, FmtSize);
		PutU16(Out, bExtensible ? 0xFFFE : Tag);
		PutU16(Out, Channels);
		PutU32(Out, 48000);
		PutU32(Out, 48000u * Channels * Bits / 8u);
		PutU16(Out, static_cast<uint32_t>(Channels * Bits / 8));
		PutU16(Out, Bits);
		if (bExtensible)
		{
			PutU16(Out, 22);
			PutU16(Out, Bits);
			PutU32(Out, 0);
			PutU16(Out, SubFormat);
			for (int32_t Index = 0; Index < 14; ++Index)
			{
				Out.push_back(0);
			}
		}
		// An unrelated chunk before the data (common in files exported by DAWs).
		PutId(Out, "LIST");
		PutU32(Out, 4);
		PutId(Out, "INFO");
		PutId(Out, "data");
		PutU32(Out, static_cast<uint32_t>(Data.size()));
		Out.insert(Out.end(), Data.begin(), Data.end());
		return Out;
	}

	std::shared_ptr<FSongAudio> MakeConstantSong(int32_t SampleRate, int32_t Frames, int16_t Value, int32_t Lane = 0)
	{
		auto Song = std::make_shared<FSongAudio>();
		Song->SampleRate = SampleRate;
		FPcmTrack& Track = Song->Lanes[static_cast<size_t>(Lane)];
		Track.NumChannels = 1;
		Track.Samples.assign(static_cast<size_t>(Frames), Value);
		return Song;
	}
}

AMP_TEST(ClockFollowsWallTimeAndRate)
{
	FSongClock Clock;
	Clock.Reset(-3000.0, 10.0);
	Clock.Advance(11.0, 1.0);
	EXPECT_NEAR(Clock.GetTimeMs(), -2000.0, 1e-9);
	Clock.Advance(12.0, 0.5); // the new rate applies from now on
	EXPECT_NEAR(Clock.GetTimeMs(), -1000.0, 1e-9);
	Clock.Advance(13.0, 1.0);
	EXPECT_NEAR(Clock.GetTimeMs(), -500.0, 1e-9);
	EXPECT_NEAR(Clock.TimeAtWall(13.5), 0.0, 1e-9);
	EXPECT_NEAR(Clock.TimeAtWall(12.9), -600.0, 1e-9);
}

AMP_TEST(ClockSlewsSmallDriftAndResyncsLargeDrift)
{
	FSongClock Clock;
	Clock.Reset(0.0, 0.0);
	Clock.Advance(0.1, 1.0);
	Clock.Sync(120.0); // 20ms ahead: slew 10%
	EXPECT_NEAR(Clock.GetLastDriftMs(), 20.0, 1e-9);
	EXPECT_NEAR(Clock.GetTimeMs(), 102.0, 1e-9);
	EXPECT_EQ(Clock.GetResyncCount(), 0);

	Clock.Advance(0.2, 1.0);
	Clock.Sync(Clock.GetTimeMs() + 80.0); // beyond the 50ms threshold: snap
	EXPECT_EQ(Clock.GetResyncCount(), 1);
	EXPECT_NEAR(Clock.GetTimeMs(), 282.0, 1e-9);

	// Negative drift slows the clock but never moves it before the previous frame.
	Clock.Advance(0.201, 1.0);
	const double Before = 282.0;
	Clock.Sync(Clock.GetTimeMs() - 45.0);
	EXPECT_TRUE(Clock.GetTimeMs() >= Before);
	EXPECT_TRUE(Clock.GetTimeMs() < 283.0);
}

AMP_TEST(ClockConvergesToAudio)
{
	// Audio runs 0.5% faster than the wall clock; the slewed clock must track it within a couple of ms.
	FSongClock Clock;
	Clock.Reset(0.0, 0.0);
	for (int32_t Frame = 1; Frame <= 600; ++Frame)
	{
		const double Wall = Frame / 60.0;
		Clock.Advance(Wall, 1.0);
		Clock.Sync(Wall * 1005.0);
	}
	EXPECT_NEAR(Clock.GetTimeMs(), 10.0 * 1005.0, 2.0);
	EXPECT_EQ(Clock.GetResyncCount(), 0);
}

AMP_TEST(WavRoundTripSevenChannels)
{
	const std::vector<uint8_t> File = MakeSevenChannelWav(100);
	FWavInfo Info;
	std::vector<FPcmTrack> Tracks;
	std::string Error;
	EXPECT_TRUE(DecodeWavChannels(File.data(), File.size(), {0, 1, 2, 3, 4, 5, 9}, Info, Tracks, Error));
	EXPECT_EQ(Info.NumChannels, 7);
	EXPECT_EQ(Info.SampleRate, 44100);
	EXPECT_EQ(Info.NumFrames, int64_t(100));
	EXPECT_EQ(Tracks.size(), size_t(7));
	for (int32_t Lane = 0; Lane < 6; ++Lane)
	{
		EXPECT_EQ(Tracks[static_cast<size_t>(Lane)].GetNumFrames(), int64_t(100));
		EXPECT_EQ(static_cast<int32_t>(Tracks[static_cast<size_t>(Lane)].Samples[50]), (Lane + 1) * 1000);
	}
	EXPECT_TRUE(Tracks[6].IsEmpty()); // channel 9 does not exist
}

AMP_TEST(WavDecodesOtherFormats)
{
	std::string Error;
	FWavInfo Info;
	FPcmTrack Track;

	// 24-bit stereo: 0x400000 (half scale) and -0x400000.
	std::vector<uint8_t> Pcm24 = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0};
	std::vector<uint8_t> File = MakeWav(1, 2, 24, Pcm24);
	EXPECT_TRUE(DecodeWavStem(File.data(), File.size(), Info, Track, Error));
	EXPECT_EQ(Track.NumChannels, 2);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[0]), 16384);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[1]), -16384);

	// 32-bit float mono via WAVE_FORMAT_EXTENSIBLE.
	const float Values[2] = {0.5f, -1.5f};
	std::vector<uint8_t> Floats(sizeof(Values));
	std::memcpy(Floats.data(), Values, sizeof(Values));
	File = MakeWav(3, 1, 32, Floats, true, 3);
	EXPECT_TRUE(DecodeWavStem(File.data(), File.size(), Info, Track, Error));
	EXPECT_EQ(Info.FormatTag, 3);
	EXPECT_EQ(Track.NumChannels, 1);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[0]), 16384);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[1]), -32767); // clamped

	// 8-bit unsigned, 4 channels: stems keep the first two.
	std::vector<uint8_t> Pcm8 = {255, 0, 128, 128};
	File = MakeWav(1, 4, 8, Pcm8);
	EXPECT_TRUE(DecodeWavStem(File.data(), File.size(), Info, Track, Error));
	EXPECT_EQ(Track.NumChannels, 2);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[0]), 127 * 256);
	EXPECT_EQ(static_cast<int32_t>(Track.Samples[1]), -128 * 256);

	// Garbage and unsupported encodings are rejected with a message.
	const uint8_t Junk[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0, 0, 0, 0};
	EXPECT_FALSE(DecodeWavStem(Junk, sizeof(Junk), Info, Track, Error));
	EXPECT_FALSE(Error.empty());
	File = MakeWav(2, 1, 16, Pcm24); // ADPCM
	EXPECT_FALSE(DecodeWavStem(File.data(), File.size(), Info, Track, Error));
}

AMP_TEST(MixerPlaysLeadInSilenceThenAudio)
{
	FStemMixer Mixer;
	Mixer.SetOutputFormat(1000, 2);
	Mixer.SetSong(MakeConstantSong(1000, 1000, 16384));
	Mixer.Seek(-100.0);
	Mixer.SetPlaying(true);

	std::vector<float> Out(200 * 2);
	Mixer.Render(Out.data(), 200);
	EXPECT_NEAR(Out[0], 0.0, 1e-6);
	EXPECT_NEAR(Out[99 * 2], 0.0, 1e-6);
	EXPECT_NEAR(Out[150 * 2], 0.5, 1e-4);
	EXPECT_NEAR(Out[150 * 2 + 1], 0.5, 1e-4);
	EXPECT_NEAR(Mixer.GetPositionMs(), 100.0, 1e-6);

	Mixer.SetPlaying(false);
	Mixer.Render(Out.data(), 200);
	EXPECT_NEAR(Out[10], 0.0, 1e-6);
	EXPECT_NEAR(Mixer.GetPositionMs(), 100.0, 1e-6); // paused: the play head does not move
}

AMP_TEST(MixerResamplesAndHonoursRate)
{
	FStemMixer Mixer;
	Mixer.SetOutputFormat(48000, 2);
	Mixer.SetSong(MakeConstantSong(44100, 44100 * 2, 8192));
	Mixer.SetPlaying(true);
	std::vector<float> Out(4800 * 2);
	Mixer.Render(Out.data(), 4800); // 100ms of output
	EXPECT_NEAR(Mixer.GetPositionMs(), 100.0, 1e-6);
	Mixer.SetRate(0.5);
	Mixer.Render(Out.data(), 4800);
	EXPECT_NEAR(Mixer.GetPositionMs(), 150.0, 1e-6);
	EXPECT_NEAR(Out[1000], 0.25, 1e-3);
}

AMP_TEST(MixerMuteRampTakes200Ms)
{
	FStemMixer Mixer;
	Mixer.SetOutputFormat(1000, 1);
	auto Song = MakeConstantSong(1000, 5000, 16384, 0);
	Song->Lanes[1].NumChannels = 1;
	Song->Lanes[1].Samples.assign(5000, 16384);
	Mixer.SetSong(Song);
	Mixer.SetPlaying(true);
	Mixer.SetLaneGain(0, 0.0f, 200.0);

	std::vector<float> Out(400);
	Mixer.Render(Out.data(), 400);
	// Mono output averages L/R: two lanes at 0.5 each => 1.0 total.
	EXPECT_NEAR(Out[0], 1.0, 0.01);
	EXPECT_NEAR(Out[100], 0.75, 0.01);
	EXPECT_NEAR(Out[250], 0.5, 1e-4);
	EXPECT_NEAR(Mixer.GetLaneGain(0), 0.0, 1e-6);

	Mixer.SetLaneGain(0, 1.0f, 200.0);
	Mixer.SetSoloLane(0);
	Mixer.Render(Out.data(), 400);
	EXPECT_NEAR(Out[399], 0.5, 1e-4); // only lane 0 is audible
}

AMP_TEST(SfxSynthIsBoundedAndFinishes)
{
	FSfxSynth Synth;
	Synth.SetOutputFormat(48000, 2);
	const ESfx All[] = {ESfx::Perfect, ESfx::Good, ESfx::Miss, ESfx::Capture, ESfx::Powerup, ESfx::EnergyLow, ESfx::GameOver,
		ESfx::SongComplete, ESfx::ShieldBreak, ESfx::LaneClear, ESfx::Countdown, ESfx::UiMove, ESfx::UiConfirm, ESfx::UiBack};
	for (const ESfx Sfx : All)
	{
		Synth.Trigger(Sfx);
		EXPECT_TRUE(Synth.GetActiveVoiceCount() > 0);
		std::vector<float> Out(static_cast<size_t>(48000 * 1.2) * 2);
		Synth.Render(Out.data(), static_cast<int32_t>(Out.size() / 2));
		double Peak = 0.0;
		bool bFinite = true;
		for (const float Sample : Out)
		{
			bFinite = bFinite && std::isfinite(Sample);
			Peak = std::max(Peak, static_cast<double>(std::abs(Sample)));
		}
		EXPECT_TRUE(bFinite);
		EXPECT_TRUE(Peak > 0.01);
		EXPECT_TRUE(Peak <= 1.0);
		EXPECT_EQ(Synth.GetActiveVoiceCount(), 0);
		// Nominal durations from the spec.
		EXPECT_TRUE(FSfxSynth::GetDurationSeconds(Sfx) <= 1.0);
	}
	EXPECT_NEAR(FSfxSynth::GetDurationSeconds(ESfx::Perfect), 0.15, 1e-9);
	EXPECT_NEAR(FSfxSynth::GetDurationSeconds(ESfx::Miss), 0.2, 1e-9);
	EXPECT_NEAR(FSfxSynth::GetDurationSeconds(ESfx::SongComplete), 1.0, 1e-9);
}
