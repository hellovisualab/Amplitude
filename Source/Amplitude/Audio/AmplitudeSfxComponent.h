#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Core/AmpSfxSynth.h"

#include "AmplitudeSfxComponent.generated.h"

/** Plays the procedurally synthesised feedback sounds (spec 10.4) on the audio render thread. */
UCLASS(ClassGroup = Amplitude)
class AMPLITUDE_API UAmplitudeSfxComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UAmplitudeSfxComponent(const FObjectInitializer& ObjectInitializer);

	void Play(Amp::ESfx Sfx);
	void StopAllSounds();
	void SetSfxGain(float LinearGain);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	/** Only touched on the audio render thread (via synth commands). */
	Amp::FSfxSynth Synth;
};
