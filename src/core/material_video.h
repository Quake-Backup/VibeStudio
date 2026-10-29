#pragma once

// RoQ video for videoMap stages: Quake III Arena and Doom 3 play id's RoQ
// cinematics on surfaces. The decoder follows the RoQ format as Dr. Tim
// Ferguson documented it (the MultimediaWiki "RoQ" page): a chunk stream of
// codebooks of 2x2 and 4x4 YUV cells and vector-quantised frames built from
// 16x16 macroblocks, with motion copies from the previous frame; Doom 3
// files may also carry JPEG frames. Sound chunks are skipped. Playback
// timing follows Quake III's cl_cin.c (frames per second from the file
// header) and Doom 3's Cinematic.cpp (videoMap loop). Credits in
// docs/CREDITS.md, "Materials, Shaders And Textures".
//
// Frames decode on demand: a video keeps its bytes and decodes forward from
// the nearest snapshot to the frame asked for, so playing costs one frame a
// step and scrubbing back costs at most one snapshot interval.

#include "core/material_images.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace vibestudio {

// True when the bytes start with a RoQ signature chunk (0x1084).
bool isRoqVideo(const QByteArray& bytes);

class MaterialVideo {
public:
	// Reads the chunk index; frames decode later. Null with `error` set when
	// the bytes are not a RoQ file or hold no frames.
	static std::shared_ptr<MaterialVideo> open(const QByteArray& bytes, const QString& reference, const QString& path, QString* error = nullptr);
	~MaterialVideo();

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int frameCount() const;
	[[nodiscard]] double framesPerSecond() const;
	[[nodiscard]] double durationSeconds() const;
	[[nodiscard]] QString reference() const;
	[[nodiscard]] QString path() const;
	// "RoQ, 256 x 256, 120 frames at 30 fps".
	[[nodiscard]] QString description() const;

	// The frame showing `seconds` into playback: looping, or holding the last
	// frame when `loop` is false. Thread-safe; status "video".
	[[nodiscard]] MaterialTexturePtr frameAt(double seconds, bool loop = true) const;
	[[nodiscard]] MaterialTexturePtr frame(int index) const;

private:
	MaterialVideo();
	struct Data;
	std::unique_ptr<Data> d;
};

} // namespace vibestudio
