#include "Core/AmpMp3.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// minimp3 (CC0, https://github.com/lieff/minimp3) compiled here with its file helpers disabled.
#if defined(THIRD_PARTY_INCLUDES_START)
THIRD_PARTY_INCLUDES_START
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "ThirdParty/minimp3/minimp3_ex.h"
#if defined(THIRD_PARTY_INCLUDES_END)
THIRD_PARTY_INCLUDES_END
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Amp
{
	bool DecodeMp3Stem(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, FPcmTrack& OutTrack, std::string& OutError)
	{
		if (Data == nullptr || Size == 0)
		{
			OutError = "empty file";
			return false;
		}

		mp3dec_t Decoder;
		std::memset(&Decoder, 0, sizeof(Decoder));
		mp3dec_file_info_t Info;
		std::memset(&Info, 0, sizeof(Info));
		const int Result = mp3dec_load_buf(&Decoder, Data, Size, &Info, nullptr, nullptr);
		if (Result != 0 || Info.buffer == nullptr || Info.samples == 0 || Info.channels <= 0 || Info.hz <= 0)
		{
			std::free(Info.buffer);
			OutError = "not a playable MP3";
			return false;
		}

		const size_t Channels = static_cast<size_t>(Info.channels);
		const size_t KeptChannels = std::min<size_t>(Channels, 2);
		const size_t NumFrames = Info.samples / Channels;
		OutTrack.NumChannels = static_cast<int32_t>(KeptChannels);
		OutTrack.Samples.resize(NumFrames * KeptChannels);
		for (size_t Frame = 0; Frame < NumFrames; ++Frame)
		{
			for (size_t Channel = 0; Channel < KeptChannels; ++Channel)
			{
				OutTrack.Samples[Frame * KeptChannels + Channel] = Info.buffer[Frame * Channels + Channel];
			}
		}
		std::free(Info.buffer);

		OutInfo = FWavInfo();
		OutInfo.SampleRate = Info.hz;
		OutInfo.NumChannels = Info.channels;
		OutInfo.BitsPerSample = 16;
		OutInfo.FormatTag = 1;
		OutInfo.NumFrames = static_cast<int64_t>(NumFrames);
		return true;
	}
}
