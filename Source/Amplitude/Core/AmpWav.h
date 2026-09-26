#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Amp
{
	struct FWavInfo
	{
		int32_t SampleRate = 0;
		int32_t NumChannels = 0;
		int32_t BitsPerSample = 0;
		/** 1 = integer PCM, 3 = IEEE float (WAVE_FORMAT_EXTENSIBLE is resolved to one of these). */
		int32_t FormatTag = 0;
		int64_t NumFrames = 0;
		size_t DataOffset = 0;
		size_t DataSize = 0;
	};

	/** 16-bit PCM audio with one or two interleaved channels. Kept as int16 to halve memory for long multitrack songs. */
	struct FPcmTrack
	{
		int32_t NumChannels = 0;
		std::vector<int16_t> Samples;

		int64_t GetNumFrames() const { return NumChannels > 0 ? static_cast<int64_t>(Samples.size()) / NumChannels : 0; }
		bool IsEmpty() const { return Samples.empty(); }
	};

	/** Parses the RIFF/WAVE header. Supports 8/16/24/32-bit PCM, 32/64-bit float and WAVE_FORMAT_EXTENSIBLE. */
	bool ParseWavHeader(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, std::string& OutError);

	/**
	 * Decodes the requested channels of a multichannel WAV into separate mono tracks.
	 * Channel indices outside the file produce empty tracks.
	 */
	bool DecodeWavChannels(const uint8_t* Data, size_t Size, const std::vector<int32_t>& ChannelIndices, FWavInfo& OutInfo, std::vector<FPcmTrack>& OutTracks, std::string& OutError);

	/** Decodes a stem file; mono stays mono, anything wider keeps its first two channels. */
	bool DecodeWavStem(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, FPcmTrack& OutTrack, std::string& OutError);

	/** Writes a 16-bit PCM WAV (used by tests and tools). Samples are interleaved. */
	std::vector<uint8_t> EncodeWav16(const std::vector<int16_t>& Interleaved, int32_t NumChannels, int32_t SampleRate);
}
