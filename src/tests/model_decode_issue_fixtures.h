#pragma once

// A LightWave LWO2 object built in code for the decode issue tests (FMT-02):
// a 32-unit quad of two triangles on surface "wall", and optionally a second
// layer that Doom 3 and q3map2 ignore, which the decoder leaves out as an
// omission. Layout from the LightWave LWO2 specification (docs/CREDITS.md).

#include <QByteArray>

#include <array>
#include <cstring>

namespace vibestudio::tests
{
inline void lwoU16(QByteArray &bytes, quint32 value)
{
	bytes.append(char((value >> 8) & 0xFF));
	bytes.append(char(value & 0xFF));
}
inline void lwoU32(QByteArray &bytes, quint32 value)
{
	for (int shift = 24; shift >= 0; shift -= 8)
		bytes.append(char((value >> shift) & 0xFF));
}
inline void lwoF32(QByteArray &bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	lwoU32(bytes, raw);
}
inline void lwoS0(QByteArray &bytes, const QByteArray &text)
{
	bytes.append(text);
	bytes.append('\0');
	if ((text.size() + 1) % 2 != 0)
		bytes.append('\0');
}
inline QByteArray lwoChunk(const char *id, const QByteArray &data)
{
	QByteArray bytes(id, 4);
	lwoU32(bytes, quint32(data.size()));
	bytes.append(data);
	if (data.size() % 2 != 0)
		bytes.append('\0');
	return bytes;
}
inline QByteArray lwoLayer(int number)
{
	QByteArray bytes;
	lwoU16(bytes, quint32(number));
	lwoU16(bytes, 0);
	for (int i = 0; i < 3; ++i)
		lwoF32(bytes, 0.0f);
	lwoS0(bytes, "layer");
	return bytes;
}
inline QByteArray layeredLightWave(bool secondLayer)
{
	QByteArray tags;
	lwoS0(tags, "wall");
	QByteArray points, uvs("TXUV", 4), pols("FACE", 4), ptag("SURF", 4), surface;
	const float corners[4][5] = {{0, 0, 0, 0, 0}, {32, 0, 0, 1, 0}, {32, 32, 0, 1, 1}, {0, 32, 0, 0, 1}};
	lwoU16(uvs, 2);
	lwoS0(uvs, "uv");
	for (int i = 0; i < 4; ++i)
	{
		for (int axis = 0; axis < 3; ++axis)
			lwoF32(points, corners[i][axis]);
		lwoU16(uvs, quint32(i));
		lwoF32(uvs, corners[i][3]);
		lwoF32(uvs, corners[i][4]);
	}
	for (const auto &triangle : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}})
	{
		lwoU16(pols, 3);
		for (int corner : triangle)
			lwoU16(pols, quint32(corner));
	}
	for (int polygon = 0; polygon < 2; ++polygon)
	{
		lwoU16(ptag, quint32(polygon));
		lwoU16(ptag, 0);
	}
	lwoS0(surface, "wall");
	lwoS0(surface, "");
	QByteArray chunks = lwoChunk("TAGS", tags) + lwoChunk("LAYR", lwoLayer(0)) + lwoChunk("PNTS", points) + lwoChunk("VMAP", uvs) +
						lwoChunk("POLS", pols) + lwoChunk("PTAG", ptag);
	if (secondLayer)
	{
		QByteArray extra, face("FACE", 4);
		for (float value : {64.0f, 0.0f, 0.0f, 96.0f, 0.0f, 0.0f, 64.0f, 32.0f, 0.0f})
			lwoF32(extra, value);
		lwoU16(face, 3);
		for (int corner : {0, 1, 2})
			lwoU16(face, quint32(corner));
		chunks += lwoChunk("LAYR", lwoLayer(1)) + lwoChunk("PNTS", extra) + lwoChunk("POLS", face);
	}
	chunks += lwoChunk("SURF", surface);
	QByteArray bytes("FORM");
	lwoU32(bytes, quint32(4 + chunks.size()));
	bytes.append("LWO2", 4);
	bytes.append(chunks);
	return bytes;
}
} // namespace vibestudio::tests
