#pragma once

#include <QByteArray>

namespace slopstudio
{
// The copied pose or keys on the system clipboard (MIME type
// application/x-slopstudio-pose+json, or the same JSON as text); empty when
// the clipboard holds neither.
[[nodiscard]] QByteArray modelPoseClipboardFromSystem();
// Puts a copy on the system clipboard under the pose MIME type and as text.
void modelPoseClipboardToSystem(const QByteArray &bytes);
} // namespace slopstudio
