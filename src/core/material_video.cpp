#include "core/material_video.h"

#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

// Chunk ids (RoQ format description, MultimediaWiki "RoQ").
constexpr quint16 kRoqSignature = 0x1084;
constexpr quint16 kRoqInfo = 0x1001;
constexpr quint16 kRoqCodebook = 0x1002;
constexpr quint16 kRoqVectorFrame = 0x1011;
constexpr quint16 kRoqJpegFrame = 0x1012;

// Cell codes, two bits each, read from 16-bit flag words high pair first.
constexpr int kCodeSkip = 0;   // keep what the previous frame had
constexpr int kCodeMotion = 1; // copy from the previous frame, displaced
constexpr int kCodeVector = 2; // one 4x4 codebook cell, scaled to the block
constexpr int kCodeSplit = 3;  // four quarter blocks with codes of their own

constexpr int kMaximumSide = 4096;
constexpr qint64 kSnapshotBudget = 48LL * 1024 * 1024;

struct Cell2 {
	std::array<quint8, 4> y {0, 0, 0, 0};
	quint8 u = 128;
	quint8 v = 128;
};

struct Cell4 {
	std::array<quint8, 4> cells {0, 0, 0, 0};
};

// Planes at full resolution (4:4:4), so cells and motion copies write
// whole pixels, and the codebooks in force.
struct DecodeState {
	int frame = -1;
	QByteArray y;
	QByteArray u;
	QByteArray v;
	std::array<Cell2, 256> cells2 {};
	std::array<Cell4, 256> cells4 {};
};

struct FrameChunk {
	qsizetype offset = 0; // the chunk's data
	quint32 size = 0;
	quint16 id = 0;
	quint16 argument = 0;
	// Codebooks read between the previous frame and this one.
	QVector<qsizetype> codebooks;
};

quint8 clampByte(double value)
{
	return static_cast<quint8>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

} // namespace

struct MaterialVideo::Data {
	QByteArray bytes;
	QString reference;
	QString path;
	int width = 0;
	int height = 0;
	double fps = 30.0;
	QVector<FrameChunk> frames;
	int snapshotInterval = 32;

	mutable QMutex mutex;
	mutable DecodeState state;
	mutable QHash<int, DecodeState> snapshots;
	mutable int lastIndex = -1;
	mutable MaterialTexturePtr lastTexture;

	[[nodiscard]] quint16 u16(qsizetype at) const { return qFromLittleEndian<quint16>(bytes.constData() + at); }
	[[nodiscard]] quint32 u32(qsizetype at) const { return qFromLittleEndian<quint32>(bytes.constData() + at); }

	void resetState() const
	{
		state = DecodeState();
		const qsizetype pixels = static_cast<qsizetype>(width) * height;
		state.y = QByteArray(pixels, '\0');
		state.u = QByteArray(pixels, static_cast<char>(128));
		state.v = QByteArray(pixels, static_cast<char>(128));
	}

	void readCodebook(qsizetype at) const
	{
		const quint32 size = u32(at + 2);
		const quint16 argument = u16(at + 6);
		const qsizetype data = at + 8;
		// The argument's high byte counts 2x2 cells, the low byte 4x4 cells;
		// a zero means 256 (for 4x4 cells only when the data has room).
		int count2 = argument >> 8;
		int count4 = argument & 0xFF;
		if (count2 == 0) {
			count2 = 256;
		}
		if (count4 == 0 && static_cast<quint32>(count2) * 6 < size) {
			count4 = 256;
		}
		qsizetype cursor = data;
		const qsizetype end = std::min<qsizetype>(data + size, bytes.size());
		for (int index = 0; index < count2 && cursor + 6 <= end; ++index) {
			Cell2& cell = state.cells2[static_cast<size_t>(index)];
			for (int sample = 0; sample < 4; ++sample) {
				cell.y[static_cast<size_t>(sample)] = static_cast<quint8>(bytes.at(cursor + sample));
			}
			cell.u = static_cast<quint8>(bytes.at(cursor + 4));
			cell.v = static_cast<quint8>(bytes.at(cursor + 5));
			cursor += 6;
		}
		for (int index = 0; index < count4 && cursor + 4 <= end; ++index) {
			for (int corner = 0; corner < 4; ++corner) {
				state.cells4[static_cast<size_t>(index)].cells[static_cast<size_t>(corner)] = static_cast<quint8>(bytes.at(cursor + corner));
			}
			cursor += 4;
		}
	}

	// A 2x2 cell painted at (x, y), each sample covering scale x scale pixels.
	void paint(int x, int y, const Cell2& cell, int scale) const
	{
		for (int row = 0; row < 2 * scale; ++row) {
			const int py = y + row;
			if (py < 0 || py >= height) {
				continue;
			}
			for (int column = 0; column < 2 * scale; ++column) {
				const int px = x + column;
				if (px < 0 || px >= width) {
					continue;
				}
				const qsizetype at = static_cast<qsizetype>(py) * width + px;
				state.y[at] = static_cast<char>(cell.y[static_cast<size_t>((row / scale) * 2 + column / scale)]);
				state.u[at] = static_cast<char>(cell.u);
				state.v[at] = static_cast<char>(cell.v);
			}
		}
	}

	// A 4x4 codebook cell (four 2x2 cells) painted over a block of 4 * scale.
	void paintCell4(int x, int y, quint8 index, int scale) const
	{
		const Cell4& cell = state.cells4[index];
		const int half = 2 * scale;
		paint(x, y, state.cells2[cell.cells[0]], scale);
		paint(x + half, y, state.cells2[cell.cells[1]], scale);
		paint(x, y + half, state.cells2[cell.cells[2]], scale);
		paint(x + half, y + half, state.cells2[cell.cells[3]], scale);
	}

	void motion(const DecodeState& previous, int x, int y, int size, int dx, int dy) const
	{
		for (int row = 0; row < size; ++row) {
			const int py = y + row;
			const int sy = std::clamp(py + dy, 0, height - 1);
			if (py < 0 || py >= height) {
				continue;
			}
			for (int column = 0; column < size; ++column) {
				const int px = x + column;
				if (px < 0 || px >= width) {
					continue;
				}
				const int sx = std::clamp(px + dx, 0, width - 1);
				const qsizetype to = static_cast<qsizetype>(py) * width + px;
				const qsizetype from = static_cast<qsizetype>(sy) * width + sx;
				state.y[to] = previous.y.at(from);
				state.u[to] = previous.u.at(from);
				state.v[to] = previous.v.at(from);
			}
		}
	}

	void decodeVectorFrame(const FrameChunk& chunk) const
	{
		const DecodeState previous = state;
		const qsizetype end = std::min<qsizetype>(chunk.offset + chunk.size, bytes.size());
		qsizetype cursor = chunk.offset;
		// The argument is the frame's mean motion, a signed byte per axis.
		const int meanX = static_cast<qint8>(chunk.argument >> 8);
		const int meanY = static_cast<qint8>(chunk.argument & 0xFF);
		quint16 flags = 0;
		int flagsLeft = 0;
		const auto nextCode = [&]() {
			if (flagsLeft == 0) {
				if (cursor + 2 > end) {
					return -1;
				}
				flags = u16(cursor);
				cursor += 2;
				flagsLeft = 8;
			}
			--flagsLeft;
			return (flags >> (flagsLeft * 2)) & 0x3;
		};
		const auto nextByte = [&]() {
			if (cursor >= end) {
				return -1;
			}
			return static_cast<int>(static_cast<quint8>(bytes.at(cursor++)));
		};
		for (int top = 0; top < height; top += 16) {
			for (int left = 0; left < width; left += 16) {
				// Four 8x8 blocks: top left, top right, bottom left, bottom right.
				for (int block = 0; block < 4; ++block) {
					const int bx = left + (block & 1) * 8;
					const int by = top + (block >> 1) * 8;
					const int code = nextCode();
					if (code < 0) {
						return;
					}
					if (code == kCodeSkip) {
						continue;
					}
					if (code == kCodeMotion) {
						const int value = nextByte();
						if (value < 0) {
							return;
						}
						motion(previous, bx, by, 8, 8 - (value >> 4) - meanX, 8 - (value & 0xF) - meanY);
					} else if (code == kCodeVector) {
						const int value = nextByte();
						if (value < 0) {
							return;
						}
						paintCell4(bx, by, static_cast<quint8>(value), 2);
					} else if (code == kCodeSplit) {
						for (int quarter = 0; quarter < 4; ++quarter) {
							const int qx = bx + (quarter & 1) * 4;
							const int qy = by + (quarter >> 1) * 4;
							const int sub = nextCode();
							if (sub < 0) {
								return;
							}
							if (sub == kCodeMotion) {
								const int value = nextByte();
								if (value < 0) {
									return;
								}
								motion(previous, qx, qy, 4, 8 - (value >> 4) - meanX, 8 - (value & 0xF) - meanY);
							} else if (sub == kCodeVector) {
								const int value = nextByte();
								if (value < 0) {
									return;
								}
								paintCell4(qx, qy, static_cast<quint8>(value), 1);
							} else if (sub == kCodeSplit) {
								for (int corner = 0; corner < 4; ++corner) {
									const int value = nextByte();
									if (value < 0) {
										return;
									}
									paint(qx + (corner & 1) * 2, qy + (corner >> 1) * 2, state.cells2[static_cast<size_t>(value)], 1);
								}
							}
						}
					}
				}
			}
		}
	}

	void decodeJpegFrame(const FrameChunk& chunk) const
	{
		const qsizetype size = std::min<qsizetype>(chunk.size, bytes.size() - chunk.offset);
		QImage image = QImage::fromData(QByteArrayView(bytes.constData() + chunk.offset, size), "JPEG");
		if (image.isNull()) {
			return;
		}
		image = image.convertToFormat(QImage::Format_RGB32).scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		for (int y = 0; y < height; ++y) {
			const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			for (int x = 0; x < width; ++x) {
				const double r = qRed(line[x]);
				const double g = qGreen(line[x]);
				const double b = qBlue(line[x]);
				const qsizetype at = static_cast<qsizetype>(y) * width + x;
				state.y[at] = static_cast<char>(clampByte(0.299 * r + 0.587 * g + 0.114 * b));
				state.u[at] = static_cast<char>(clampByte(128.0 - 0.168736 * r - 0.331264 * g + 0.5 * b));
				state.v[at] = static_cast<char>(clampByte(128.0 + 0.5 * r - 0.418688 * g - 0.081312 * b));
			}
		}
	}

	void decodeNext() const
	{
		const int index = state.frame + 1;
		if (index < 0 || index >= frames.size()) {
			return;
		}
		const FrameChunk& chunk = frames.at(index);
		for (qsizetype codebook : chunk.codebooks) {
			readCodebook(codebook);
		}
		if (chunk.id == kRoqJpegFrame) {
			decodeJpegFrame(chunk);
		} else {
			decodeVectorFrame(chunk);
		}
		state.frame = index;
		if (index % snapshotInterval == 0 && !snapshots.contains(index)) {
			snapshots.insert(index, state);
		}
	}

	MaterialTexturePtr texture() const
	{
		// YCbCr to RGB with Quake III's cl_cin.c coefficients.
		auto texture = std::make_shared<MaterialTexture>();
		texture->width = width;
		texture->height = height;
		texture->pixels.resize(static_cast<qsizetype>(width) * height);
		for (qsizetype at = 0; at < texture->pixels.size(); ++at) {
			const double y = static_cast<quint8>(state.y.at(at));
			const double cb = static_cast<quint8>(state.u.at(at)) - 128.0;
			const double cr = static_cast<quint8>(state.v.at(at)) - 128.0;
			texture->pixels[at] = qRgb(clampByte(y + 1.402 * cr), clampByte(y - 0.34414 * cb - 0.71414 * cr), clampByte(y + 1.772 * cb));
		}
		texture->reference = reference;
		texture->path = path;
		texture->status = QStringLiteral("video");
		texture->note = Text::tr("Frame %1 of %2").arg(state.frame + 1).arg(frames.size());
		texture->buildMips();
		return texture;
	}
};

bool isRoqVideo(const QByteArray& bytes)
{
	return bytes.size() >= 8 && qFromLittleEndian<quint16>(bytes.constData()) == kRoqSignature;
}

MaterialVideo::MaterialVideo()
	: d(std::make_unique<Data>())
{
}

MaterialVideo::~MaterialVideo() = default;

std::shared_ptr<MaterialVideo> MaterialVideo::open(const QByteArray& bytes, const QString& reference, const QString& path, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return std::shared_ptr<MaterialVideo>();
	};
	if (!isRoqVideo(bytes)) {
		return fail(Text::tr("%1 is not a RoQ video.").arg(path));
	}
	std::shared_ptr<MaterialVideo> video(new MaterialVideo());
	Data& data = *video->d;
	data.bytes = bytes;
	data.reference = reference;
	data.path = path;
	const quint16 rate = data.u16(6);
	data.fps = rate > 0 && rate <= 120 ? rate : 30.0;
	QVector<qsizetype> pendingCodebooks;
	qsizetype cursor = 8;
	while (cursor + 8 <= bytes.size()) {
		const quint16 id = data.u16(cursor);
		const quint32 size = data.u32(cursor + 2);
		const quint16 argument = data.u16(cursor + 6);
		const qsizetype payload = cursor + 8;
		if (size > static_cast<quint64>(bytes.size() - payload)) {
			// A truncated last chunk ends the video.
			break;
		}
		if (id == kRoqInfo && size >= 4) {
			data.width = data.u16(payload);
			data.height = data.u16(payload + 2);
		} else if (id == kRoqCodebook) {
			pendingCodebooks << cursor;
		} else if (id == kRoqVectorFrame || id == kRoqJpegFrame) {
			FrameChunk frame;
			frame.offset = payload;
			frame.size = size;
			frame.id = id;
			frame.argument = argument;
			frame.codebooks = pendingCodebooks;
			pendingCodebooks.clear();
			data.frames.push_back(frame);
		}
		cursor = payload + size;
	}
	if (data.width <= 0 || data.height <= 0 || data.width > kMaximumSide || data.height > kMaximumSide) {
		return fail(Text::tr("%1 has no usable picture size.").arg(path));
	}
	if (data.frames.isEmpty()) {
		return fail(Text::tr("%1 holds no frames.").arg(path));
	}
	// Snapshots every few frames, as many as the budget holds.
	const qint64 snapshotBytes = static_cast<qint64>(data.width) * data.height * 3;
	const qint64 snapshots = std::max<qint64>(1, kSnapshotBudget / std::max<qint64>(1, snapshotBytes));
	data.snapshotInterval = std::max(16, static_cast<int>(std::ceil(static_cast<double>(data.frames.size()) / static_cast<double>(snapshots))));
	data.resetState();
	return video;
}

int MaterialVideo::width() const
{
	return d->width;
}

int MaterialVideo::height() const
{
	return d->height;
}

int MaterialVideo::frameCount() const
{
	return static_cast<int>(d->frames.size());
}

double MaterialVideo::framesPerSecond() const
{
	return d->fps;
}

double MaterialVideo::durationSeconds() const
{
	return d->frames.size() / d->fps;
}

QString MaterialVideo::reference() const
{
	return d->reference;
}

QString MaterialVideo::path() const
{
	return d->path;
}

QString MaterialVideo::description() const
{
	return Text::tr("RoQ, %1 x %2, %n frame(s) at %3 fps", nullptr, frameCount()).arg(d->width).arg(d->height).arg(d->fps);
}

MaterialTexturePtr MaterialVideo::frameAt(double seconds, bool loop) const
{
	const int count = frameCount();
	long long index = static_cast<long long>(std::floor(std::max(0.0, seconds) * d->fps));
	index = loop ? index % count : std::min<long long>(index, count - 1);
	return frame(static_cast<int>(index));
}

MaterialTexturePtr MaterialVideo::frame(int index) const
{
	QMutexLocker locker(&d->mutex);
	index = std::clamp(index, 0, frameCount() - 1);
	if (index == d->lastIndex && d->lastTexture) {
		return d->lastTexture;
	}
	// The nearest snapshot at or before the frame; going back, it (or the
	// start) is where decoding resumes, going forward only when it is nearer.
	int best = -1;
	for (auto it = d->snapshots.cbegin(); it != d->snapshots.cend(); ++it) {
		if (it.key() <= index && it.key() > best) {
			best = it.key();
		}
	}
	if (d->state.frame > index) {
		if (best >= 0) {
			d->state = d->snapshots.value(best);
		} else {
			d->resetState();
		}
	} else if (best > d->state.frame) {
		d->state = d->snapshots.value(best);
	}
	while (d->state.frame < index) {
		d->decodeNext();
	}
	d->lastIndex = index;
	d->lastTexture = d->texture();
	return d->lastTexture;
}

} // namespace vibestudio
