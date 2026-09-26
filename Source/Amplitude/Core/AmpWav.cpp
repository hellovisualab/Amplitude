#include "Core/AmpWav.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Amp
{
	namespace
	{
		constexpr uint16_t FormatPcm = 1;
		constexpr uint16_t FormatFloat = 3;
		constexpr uint16_t FormatExtensible = 0xFFFE;

		uint16_t ReadU16(const uint8_t* Bytes)
		{
			return static_cast<uint16_t>(Bytes[0] | (Bytes[1] << 8));
		}

		uint32_t ReadU32(const uint8_t* Bytes)
		{
			return static_cast<uint32_t>(Bytes[0]) | (static_cast<uint32_t>(Bytes[1]) << 8) | (static_cast<uint32_t>(Bytes[2]) << 16) | (static_cast<uint32_t>(Bytes[3]) << 24);
		}

		bool ChunkIdEquals(const uint8_t* Bytes, const char* Id)
		{
			return std::memcmp(Bytes, Id, 4) == 0;
		}

		int16_t FloatToInt16(double Value)
		{
			const double Clamped = std::clamp(Value, -1.0, 1.0);
			return static_cast<int16_t>(std::lrint(Clamped * 32767.0));
		}

		/** Reads one sample and converts it to int16. */
		int16_t ReadSample(const uint8_t* Bytes, const FWavInfo& Info)
		{
			if (Info.FormatTag == FormatFloat)
			{
				if (Info.BitsPerSample == 64)
				{
					double Value;
					std::memcpy(&Value, Bytes, sizeof(Value));
					return FloatToInt16(Value);
				}
				float Value;
				std::memcpy(&Value, Bytes, sizeof(Value));
				return FloatToInt16(static_cast<double>(Value));
			}

			switch (Info.BitsPerSample)
			{
			case 8:
				return static_cast<int16_t>((static_cast<int32_t>(Bytes[0]) - 128) * 256);
			case 16:
				return static_cast<int16_t>(ReadU16(Bytes));
			case 24:
			{
				const int32_t Value = static_cast<int32_t>((static_cast<uint32_t>(Bytes[0]) << 8) | (static_cast<uint32_t>(Bytes[1]) << 16) | (static_cast<uint32_t>(Bytes[2]) << 24)) >> 8;
				return static_cast<int16_t>(Value >> 8);
			}
			case 32:
				return static_cast<int16_t>(static_cast<int32_t>(ReadU32(Bytes)) >> 16);
			default:
				return 0;
			}
		}
	}

	bool ParseWavHeader(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, std::string& OutError)
	{
		OutInfo = FWavInfo();
		if (Data == nullptr || Size < 12 || !ChunkIdEquals(Data, "RIFF") || !ChunkIdEquals(Data + 8, "WAVE"))
		{
			OutError = "not a RIFF/WAVE file";
			return false;
		}

		bool bHasFormat = false;
		bool bHasData = false;
		size_t Offset = 12;
		while (Offset + 8 <= Size)
		{
			const uint8_t* Chunk = Data + Offset;
			const size_t ChunkSize = ReadU32(Chunk + 4);
			const size_t BodyOffset = Offset + 8;
			// Streaming writers sometimes leave sizes at 0 or 0xFFFFFFFF; clamp to what we actually have.
			const size_t Available = Size - BodyOffset;

			if (ChunkIdEquals(Chunk, "fmt "))
			{
				if (ChunkSize < 16 || Available < 16)
				{
					OutError = "truncated fmt chunk";
					return false;
				}
				const uint8_t* Format = Data + BodyOffset;
				uint16_t Tag = ReadU16(Format);
				OutInfo.NumChannels = ReadU16(Format + 2);
				OutInfo.SampleRate = static_cast<int32_t>(ReadU32(Format + 4));
				OutInfo.BitsPerSample = ReadU16(Format + 14);
				if (Tag == FormatExtensible)
				{
					if (ChunkSize < 40 || Available < 40)
					{
						OutError = "truncated WAVE_FORMAT_EXTENSIBLE header";
						return false;
					}
					// The first two bytes of the sub-format GUID hold the real format tag.
					Tag = ReadU16(Format + 24);
				}
				OutInfo.FormatTag = Tag;
				bHasFormat = true;
			}
			else if (ChunkIdEquals(Chunk, "data"))
			{
				OutInfo.DataOffset = BodyOffset;
				OutInfo.DataSize = std::min(ChunkSize, Available);
				bHasData = true;
				if (bHasFormat)
				{
					break;
				}
			}

			const size_t Advance = std::min(ChunkSize, Available);
			Offset = BodyOffset + Advance + (Advance & 1u);
		}

		if (!bHasFormat || !bHasData)
		{
			OutError = bHasFormat ? "missing data chunk" : "missing fmt chunk";
			return false;
		}
		if (OutInfo.FormatTag != FormatPcm && OutInfo.FormatTag != FormatFloat)
		{
			OutError = "unsupported WAV encoding (only PCM and IEEE float are supported)";
			return false;
		}
		const bool bValidPcm = OutInfo.FormatTag == FormatPcm && (OutInfo.BitsPerSample == 8 || OutInfo.BitsPerSample == 16 || OutInfo.BitsPerSample == 24 || OutInfo.BitsPerSample == 32);
		const bool bValidFloat = OutInfo.FormatTag == FormatFloat && (OutInfo.BitsPerSample == 32 || OutInfo.BitsPerSample == 64);
		if (!bValidPcm && !bValidFloat)
		{
			OutError = "unsupported bit depth";
			return false;
		}
		if (OutInfo.NumChannels <= 0 || OutInfo.SampleRate <= 0)
		{
			OutError = "invalid channel count or sample rate";
			return false;
		}

		const size_t FrameBytes = static_cast<size_t>(OutInfo.NumChannels) * static_cast<size_t>(OutInfo.BitsPerSample / 8);
		OutInfo.NumFrames = static_cast<int64_t>(OutInfo.DataSize / FrameBytes);
		return true;
	}

	bool DecodeWavChannels(const uint8_t* Data, size_t Size, const std::vector<int32_t>& ChannelIndices, FWavInfo& OutInfo, std::vector<FPcmTrack>& OutTracks, std::string& OutError)
	{
		if (!ParseWavHeader(Data, Size, OutInfo, OutError))
		{
			return false;
		}

		const size_t SampleBytes = static_cast<size_t>(OutInfo.BitsPerSample / 8);
		const size_t FrameBytes = SampleBytes * static_cast<size_t>(OutInfo.NumChannels);
		const size_t NumFrames = static_cast<size_t>(OutInfo.NumFrames);
		const uint8_t* Samples = Data + OutInfo.DataOffset;

		OutTracks.assign(ChannelIndices.size(), FPcmTrack());
		for (size_t TrackIndex = 0; TrackIndex < ChannelIndices.size(); ++TrackIndex)
		{
			const int32_t Channel = ChannelIndices[TrackIndex];
			if (Channel < 0 || Channel >= OutInfo.NumChannels)
			{
				continue;
			}
			FPcmTrack& Track = OutTracks[TrackIndex];
			Track.NumChannels = 1;
			Track.Samples.resize(NumFrames);
			const uint8_t* Cursor = Samples + static_cast<size_t>(Channel) * SampleBytes;
			for (size_t Frame = 0; Frame < NumFrames; ++Frame, Cursor += FrameBytes)
			{
				Track.Samples[Frame] = ReadSample(Cursor, OutInfo);
			}
		}
		return true;
	}

	bool DecodeWavStem(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, FPcmTrack& OutTrack, std::string& OutError)
	{
		if (!ParseWavHeader(Data, Size, OutInfo, OutError))
		{
			return false;
		}

		const int32_t KeptChannels = std::min(OutInfo.NumChannels, 2);
		const size_t SampleBytes = static_cast<size_t>(OutInfo.BitsPerSample / 8);
		const size_t FrameBytes = SampleBytes * static_cast<size_t>(OutInfo.NumChannels);
		const size_t NumFrames = static_cast<size_t>(OutInfo.NumFrames);
		const uint8_t* Samples = Data + OutInfo.DataOffset;

		OutTrack.NumChannels = KeptChannels;
		OutTrack.Samples.resize(NumFrames * static_cast<size_t>(KeptChannels));
		size_t Write = 0;
		for (size_t Frame = 0; Frame < NumFrames; ++Frame)
		{
			const uint8_t* FrameStart = Samples + Frame * FrameBytes;
			for (int32_t Channel = 0; Channel < KeptChannels; ++Channel)
			{
				OutTrack.Samples[Write++] = ReadSample(FrameStart + static_cast<size_t>(Channel) * SampleBytes, OutInfo);
			}
		}
		return true;
	}

	std::vector<uint8_t> EncodeWav16(const std::vector<int16_t>& Interleaved, int32_t NumChannels, int32_t SampleRate)
	{
		const uint32_t DataBytes = static_cast<uint32_t>(Interleaved.size() * sizeof(int16_t));
		std::vector<uint8_t> Out;
		Out.reserve(44 + DataBytes);

		auto PutId = [&Out](const char* Id) { Out.insert(Out.end(), Id, Id + 4); };
		auto PutU16 = [&Out](uint32_t Value)
		{
			Out.push_back(static_cast<uint8_t>(Value & 0xFF));
			Out.push_back(static_cast<uint8_t>((Value >> 8) & 0xFF));
		};
		auto PutU32 = [&PutU16](uint32_t Value)
		{
			PutU16(Value & 0xFFFF);
			PutU16(Value >> 16);
		};

		const uint32_t Channels = static_cast<uint32_t>(NumChannels);
		const uint32_t Rate = static_cast<uint32_t>(SampleRate);
		PutId("RIFF");
		PutU32(36 + DataBytes);
		PutId("WAVE");
		PutId("fmt ");
		PutU32(16);
		PutU16(FormatPcm);
		PutU16(Channels);
		PutU32(Rate);
		PutU32(Rate * Channels * 2);
		PutU16(Channels * 2);
		PutU16(16);
		PutId("data");
		PutU32(DataBytes);
		for (const int16_t Sample : Interleaved)
		{
			PutU16(static_cast<uint16_t>(Sample));
		}
		return Out;
	}
}
