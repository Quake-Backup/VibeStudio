/* Private SlopStudio extension to the pinned backend, GPL-3.0-or-later. */
#ifndef SLOPSTUDIO_PORTAUDIO_H
#define SLOPSTUDIO_PORTAUDIO_H
#include "portaudio.h"
#define SLOP_PA_TIMESTAMP_ERROR (1UL << 16)
#define SLOP_PA_HOST_ERROR (1UL << 17)
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _WIN32
/* Call only after Pa_IsStreamActive returns zero, before closing the stream. */
unsigned long SlopPaWasapi_GetStoppedFlags(PaStream *stream);
#endif
#ifdef __cplusplus
}
#endif
#endif
