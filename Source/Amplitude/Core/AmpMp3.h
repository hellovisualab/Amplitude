#pragma once

#include "Core/AmpWav.h"

namespace Amp
{
	/**
	 * Decodes an MP3 stem into 16-bit PCM (mono stays mono, stereo stays stereo). The encoder delay and
	 * padding recorded in a LAME/Xing header are removed, so stems encoded alike stay sample-aligned
	 * with each other and with the chart.
	 */
	bool DecodeMp3Stem(const uint8_t* Data, size_t Size, FWavInfo& OutInfo, FPcmTrack& OutTrack, std::string& OutError);
}
