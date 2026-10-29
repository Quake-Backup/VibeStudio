#pragma once

// Rich seeds for the legacy model decoders (FMT-10), shared by
// model_format_legacy_fuzz_smoke_test.cpp and parser_fuzz_smoke_test.cpp.
// They reach the parts of each format that the minimal parser_fuzz seeds do
// not: MDL skin and frame groups and onseam texture coordinates, MD2 strip and
// fan GL commands with two skins, and MD3 with two surfaces, two frames and
// tags. Every byte is laid out here from the published formats (Quake
// Specifications chapter 5 and `modelgen.h`, Quake II `qfiles.h`, Quake III
// `md3.h`); no game data is used.

#include <QByteArray>
#include <QVector>

#include <cstring>

namespace vibestudio::tests
{
inline void seedLe16(QByteArray *bytes, quint32 value)
{
	bytes->append(char(value & 0xff));
	bytes->append(char((value >> 8) & 0xff));
}
inline void seedLe32(QByteArray *bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8)
		bytes->append(char((value >> shift) & 0xff));
}
inline void seedFloat(QByteArray *bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	seedLe32(bytes, raw);
}
inline QByteArray seedName(const QByteArray &name, int size)
{
	QByteArray bytes = name.left(size);
	bytes.append(QByteArray(size - bytes.size(), '\0'));
	return bytes;
}

// Quake MDL (IDPO 6): a 16x8 skin group of two members and a single skin; six
// texture vertices, two of them on the seam; two front and two back faces,
// the back ones using the seam vertices; a single frame and a group of two.
inline QByteArray richQuakeMdl()
{
	constexpr int width = 16, height = 8, vertices = 6, triangles = 4;
	QByteArray bytes("IDPO");
	seedLe32(&bytes, 6);
	for (float value : {1.0f, 1.0f, 1.0f, -8.0f, -8.0f, -8.0f})
		seedFloat(&bytes, value);
	seedFloat(&bytes, 12.0f); // bounding radius
	for (float value : {0.0f, 0.0f, 4.0f})
		seedFloat(&bytes, value); // eye position
	seedLe32(&bytes, 2);		  // skins
	seedLe32(&bytes, width);
	seedLe32(&bytes, height);
	seedLe32(&bytes, vertices);
	seedLe32(&bytes, triangles);
	seedLe32(&bytes, 2); // frame entries: one single, one group
	seedLe32(&bytes, 1); // sync type
	seedLe32(&bytes, 0); // flags
	seedFloat(&bytes, 4.0f);
	// Skin group: count, end times, then the members back to back.
	seedLe32(&bytes, 1);
	seedLe32(&bytes, 2);
	seedFloat(&bytes, 0.1f);
	seedFloat(&bytes, 0.2f);
	for (int member = 0; member < 2; ++member)
		for (int pixel = 0; pixel < width * height; ++pixel)
			bytes.append(char((pixel * 7 + member * 31) & 0xff));
	seedLe32(&bytes, 0);
	for (int pixel = 0; pixel < width * height; ++pixel)
		bytes.append(char(pixel < 32 ? 240 + pixel % 16 : (pixel * 3) & 0x7f)); // fullbright row
	// onseam, s, t: vertices 4 and 5 sit on the seam at s = 0.
	const int st[vertices][3] = {{0, 2, 1}, {0, 6, 1}, {0, 6, 6}, {0, 2, 6}, {0x20, 0, 2}, {0x20, 0, 5}};
	for (const auto &v : st)
		for (int value : v)
			seedLe32(&bytes, quint32(value));
	// facesfront, then three vertex indices.
	const int faces[triangles][4] = {{1, 0, 1, 2}, {1, 0, 2, 3}, {0, 4, 5, 1}, {0, 5, 2, 1}};
	for (const auto &face : faces)
		for (int value : face)
			seedLe32(&bytes, quint32(value));
	const auto appendPose = [&bytes](const QByteArray &name, int lift) {
		bytes.append(QByteArray(4, '\0'));		  // packed mins
		bytes.append(QByteArray(3, '\x40') + '\0'); // packed maxs
		bytes.append(seedName(name, 16));
		const int positions[vertices][3] = {{0, 0, 0}, {16, 0, 0}, {16, 16, 0}, {0, 16, 0}, {0, 0, 16}, {0, 16, 16}};
		for (int v = 0; v < vertices; ++v)
		{
			for (int axis = 0; axis < 3; ++axis)
				bytes.append(char(positions[v][axis] + (axis == 2 ? lift : 0)));
			bytes.append(char((v * 17) % 162)); // normal index
		}
	};
	seedLe32(&bytes, 0);
	appendPose("stand1", 0);
	// Frame group: count, packed bounds, end times, then the poses.
	seedLe32(&bytes, 1);
	seedLe32(&bytes, 2);
	bytes.append(QByteArray(4, '\0'));
	bytes.append(QByteArray(3, '\x40') + '\0');
	seedFloat(&bytes, 0.1f);
	seedFloat(&bytes, 0.2f);
	appendPose("run1", 2);
	appendPose("run2", 4);
	return bytes;
}

// Quake II MD2 (IDP2 8): two PCX skins, five positions, three triangles drawn
// by one four-vertex strip and one three-vertex fan, two frames.
inline QByteArray richQuake2Md2()
{
	constexpr int width = 64, height = 64, positions = 5, sts = 5, triangles = 3, frames = 2;
	const int frameSize = 40 + positions * 4;
	const int st[sts][2] = {{2, 3}, {20, 3}, {20, 30}, {2, 30}, {40, 40}};
	// Strip 0 1 2 3 draws (0 1 2) and (2 1 3); fan 4 0 3 draws (4 0 3).
	const int tris[triangles][3] = {{0, 1, 2}, {2, 1, 3}, {4, 0, 3}};
	QByteArray commands;
	const auto corner = [&commands, &st](int index) {
		seedFloat(&commands, (float(st[index][0]) + 0.5f) / width);
		seedFloat(&commands, (float(st[index][1]) + 0.5f) / height);
		seedLe32(&commands, quint32(index));
	};
	seedLe32(&commands, 4);
	for (int index : {0, 1, 2, 3})
		corner(index);
	seedLe32(&commands, quint32(-3));
	for (int index : {4, 0, 3})
		corner(index);
	seedLe32(&commands, 0);
	const quint32 offsetSkins = 68, offsetSt = offsetSkins + 2 * 64, offsetTris = offsetSt + sts * 4,
				  offsetFrames = offsetTris + triangles * 12, offsetCommands = offsetFrames + frames * frameSize,
				  offsetEnd = offsetCommands + quint32(commands.size());
	QByteArray bytes("IDP2");
	for (quint32 value : {8u, quint32(width), quint32(height), quint32(frameSize), 2u, quint32(positions), quint32(sts), quint32(triangles),
						  quint32(commands.size() / 4), quint32(frames), offsetSkins, offsetSt, offsetTris, offsetFrames, offsetCommands, offsetEnd})
		seedLe32(&bytes, value);
	bytes.append(seedName("models/test/rich/skin.pcx", 64));
	bytes.append(seedName("models/test/rich/pain.pcx", 64));
	for (const auto &coordinate : st)
	{
		seedLe16(&bytes, quint32(coordinate[0]));
		seedLe16(&bytes, quint32(coordinate[1]));
	}
	for (const auto &triangle : tris)
	{
		for (int index : triangle)
			seedLe16(&bytes, quint32(index)); // positions
		for (int index : triangle)
			seedLe16(&bytes, quint32(index)); // texture coordinates
	}
	const int points[positions][3] = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}, {20, 20, 60}};
	for (int frame = 0; frame < frames; ++frame)
	{
		for (int axis = 0; axis < 3; ++axis)
			seedFloat(&bytes, 0.5f);
		for (int axis = 0; axis < 3; ++axis)
			seedFloat(&bytes, -10.0f);
		bytes.append(seedName(QByteArray("walk") + QByteArray::number(frame + 1), 16));
		for (const auto &point : points)
		{
			for (int axis = 0; axis < 3; ++axis)
				bytes.append(char(point[axis] + frame * 4));
			bytes.append(char(5));
		}
	}
	bytes.append(commands);
	return bytes;
}

// Quake III MD3 (IDP3 15): two surfaces with their own shaders, two frames
// and two tags per frame, written field by field.
inline QByteArray richQuake3Md3()
{
	constexpr int frames = 2, tags = 2, vertices = 4, triangles = 2;
	QByteArray frameBlock, tagBlock, surfaces;
	for (int frame = 0; frame < frames; ++frame)
	{
		for (float value : {-16.0f, -16.0f, 0.0f, 16.0f, 16.0f, 24.0f, 0.0f, 0.0f, 0.0f, 30.0f})
			seedFloat(&frameBlock, value);
		frameBlock.append(seedName(QByteArray("frame") + QByteArray::number(frame), 16));
		for (int tag = 0; tag < tags; ++tag)
		{
			tagBlock.append(seedName(tag ? QByteArray("tag_flash") : QByteArray("tag_weapon"), 64));
			for (float value : {float(tag * 8), 0.0f, float(frame * 2), 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f})
				seedFloat(&tagBlock, value);
		}
	}
	for (int surface = 0; surface < 2; ++surface)
	{
		QByteArray tris, shaders, st, xyz;
		for (int value : {0, 1, 2, 0, 2, 3})
			seedLe32(&tris, quint32(value));
		shaders.append(seedName(surface ? QByteArray("models/test/rich/glass") : QByteArray("models/test/rich/hull"), 64));
		seedLe32(&shaders, 0);
		for (float value : {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f})
			seedFloat(&st, value);
		for (int frame = 0; frame < frames; ++frame)
		{
			const int corners[vertices][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
			for (const auto &c : corners)
			{
				seedLe16(&xyz, quint32(qint16(c[0] * 512 + surface * 1024)));
				seedLe16(&xyz, quint32(qint16(c[1] * 512)));
				seedLe16(&xyz, quint32(qint16(frame * 64)));
				seedLe16(&xyz, 0x0080);
			}
		}
		const quint32 offsetTris = 108, offsetShaders = offsetTris + quint32(tris.size()), offsetSt = offsetShaders + quint32(shaders.size()),
					  offsetXyz = offsetSt + quint32(st.size()), end = offsetXyz + quint32(xyz.size());
		QByteArray header("IDP3");
		header.append(seedName(surface ? QByteArray("glass") : QByteArray("hull"), 64));
		for (quint32 value : {0u, quint32(frames), 1u, quint32(vertices), quint32(triangles), offsetTris, offsetShaders, offsetSt, offsetXyz, end})
			seedLe32(&header, value);
		surfaces += header + tris + shaders + st + xyz;
	}
	const quint32 offsetFrames = 108, offsetTags = offsetFrames + quint32(frameBlock.size()), offsetSurfaces = offsetTags + quint32(tagBlock.size()),
				  offsetEnd = offsetSurfaces + quint32(surfaces.size());
	QByteArray bytes("IDP3");
	seedLe32(&bytes, 15);
	bytes.append(seedName("models/test/rich/rich.md3", 64));
	for (quint32 value : {0u, quint32(frames), quint32(tags), 2u, 0u, offsetFrames, offsetTags, offsetSurfaces, offsetEnd})
		seedLe32(&bytes, value);
	return bytes + frameBlock + tagBlock + surfaces;
}

struct LegacyModelSeed
{
	const char *name;
	const char *path;
	QByteArray bytes;
};

inline QVector<LegacyModelSeed> richLegacyModelSeeds()
{
	return {{"mdl-groups-onseam", "progs/rich.mdl", richQuakeMdl()},
			{"md2-strip-fan", "models/test/rich/tris.md2", richQuake2Md2()},
			{"md3-surfaces-tags", "models/test/rich/rich.md3", richQuake3Md3()}};
}
} // namespace vibestudio::tests
