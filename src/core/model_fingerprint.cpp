#include "core/model_fingerprint.h"
#include "core/model_document.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <system_error>
#include <thread>
#include <vector>

namespace slopstudio
{
namespace
{
// JSON treats positive and negative zero as the same value. Preserve that
// authoring equivalence while encoding finite IEEE float values exactly.
quint32 canonicalFloat(float value)
{
	return value == 0 ? 0 : std::bit_cast<quint32>(value);
}

class GeometryHash
{
  public:
	GeometryHash(QCryptographicHash &hash, const ModelWorkControl &control, QString *error, qint64 completed = 0)
		: m_hash(hash), m_control(control), m_error(error), m_completed(completed)
	{
	}
	bool word(quint32 value)
	{
		qToLittleEndian(value, m_buffer.data() + m_used);
		m_used += sizeof(value);
		return m_used < m_buffer.size() || flush();
	}
	bool count(qsizetype value) { return word(quint32(quint64(value))) && word(quint32(quint64(value) >> 32)); }
	bool number(float value) { return word(canonicalFloat(value)); }
	bool vector(ModelVec3 value) { return number(value.x) && number(value.y) && number(value.z); }
	// A block digest: a whole number of words, so word() keeps its room.
	bool digest(const QByteArray &value)
	{
		Q_ASSERT(value.size() % qsizetype(sizeof(quint32)) == 0);
		for (const char byte : value)
		{
			m_buffer[m_used++] = byte;
			if (m_used == m_buffer.size() && !flush())
			{
				return false;
			}
		}
		return true;
	}
	bool flush()
	{
		m_completed += m_used;
		if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Serializing, m_completed, 0, m_error))
		{
			return false;
		}
		m_hash.addData(QByteArrayView(m_buffer.data(), qsizetype(m_used)));
		m_used = 0;
		return true;
	}

  private:
	QCryptographicHash &m_hash;
	const ModelWorkControl &m_control;
	QString *m_error;
	std::array<char, 8192> m_buffer{};
	size_t m_used = 0;
	qint64 m_completed = 0;
};

// Keep metadata identity tied to the source serializer, including MDL
// settings, indexed skins, material paths, poses, clips and attachment tags.
// The packed form keeps skeletal clips and influences in a few strings.
// Geometry is hashed separately: constructing millions of tiny JSON arrays
// merely to detect an edit is unnecessary allocation and latency.
QByteArray metadataBytes(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	ModelMesh metadata = mesh;
	for (auto &surface : metadata.surfaces)
	{
		surface.triangles.clear();
		surface.texCoords.clear();
		surface.uvSeams.clear();
		surface.frames.clear();
	}
	const auto json = editableModelJson(metadata, error, control, ModelSourceEncoding::Packed);
	return json.isEmpty() ? QByteArray() : QJsonDocument(json).toJson(QJsonDocument::Compact);
}

qint64 packedSourceBytes(const ModelMesh &mesh, qsizetype metadata)
{
	qint64 bytes = metadata;
	for (const auto &surface : mesh.surfaces)
	{
		bytes += modelSourcePackedSurfaceLength(surface);
	}
	return bytes;
}

QVector<QPair<int, int>> sortedSeams(const ModelSurface &surface)
{
	auto seams = surface.uvSeams.values();
	std::sort(seams.begin(), seams.end());
	return seams;
}

enum class BlockKind
{
	Vectors,
	Triangles,
	TexCoords
};

struct BlockJob
{
	BlockKind kind = BlockKind::Vectors;
	const void *data = nullptr;
	qsizetype count = 0;
	int slot = 0;
	[[nodiscard]] qint64 bytes() const { return qint64(count) * (kind == BlockKind::TexCoords ? 8 : 12); }
};

// One array's digest over the same little-endian words the stream writes.
// It reads only its own array, so several run at once.
QByteArray hashBlock(const BlockJob &job)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	// 1,536 vectors or triangles, or 2,304 texture coordinates, at a time.
	std::array<char, 18432> buffer;
	const qsizetype width = job.kind == BlockKind::TexCoords ? 8 : 12;
	const qsizetype batch = qsizetype(buffer.size()) / width;
	for (qsizetype first = 0; first < job.count; first += batch)
	{
		const qsizetype used = std::min(batch, job.count - first);
		char *out = buffer.data();
		const auto put = [&out](quint32 value)
		{
			qToLittleEndian(value, out);
			out += sizeof(value);
		};
		switch (job.kind)
		{
		case BlockKind::Vectors:
		{
			const auto *values = static_cast<const ModelVec3 *>(job.data) + first;
			for (qsizetype index = 0; index < used; ++index)
			{
				put(canonicalFloat(values[index].x));
				put(canonicalFloat(values[index].y));
				put(canonicalFloat(values[index].z));
			}
			break;
		}
		case BlockKind::Triangles:
		{
			const auto *values = static_cast<const ModelTriangle *>(job.data) + first;
			for (qsizetype index = 0; index < used; ++index)
			{
				put(quint32(values[index].a));
				put(quint32(values[index].b));
				put(quint32(values[index].c));
			}
			break;
		}
		case BlockKind::TexCoords:
		{
			const auto *values = static_cast<const ModelTexCoord *>(job.data) + first;
			for (qsizetype index = 0; index < used; ++index)
			{
				put(canonicalFloat(values[index].u));
				put(canonicalFloat(values[index].v));
			}
			break;
		}
		}
		hash.addData(QByteArrayView(buffer.data(), used * width));
	}
	return hash.result();
}

// Below this, starting threads costs more than the hashing they would share.
constexpr qint64 parallelHashBytes = 512 * 1024;
constexpr unsigned maximumHashHelpers = 7;

// Gives every bulk array of one model a digest slot, taking the digest from
// the blocks of an earlier state when the array is the same shared array, and
// hashes the rest.
class BlockPlan
{
  public:
	explicit BlockPlan(const ModelStateBlocks *known) : m_known(known)
	{
		// Slot 0: every empty array.
		m_digests.push_back(QCryptographicHash::hash(QByteArrayView(), QCryptographicHash::Sha256));
	}
	int vectors(const QVector<ModelVec3> &array)
	{
		return resolve(array, m_known ? &m_known->vectors : nullptr, &m_next.vectors, BlockKind::Vectors);
	}
	int triangles(const QVector<ModelTriangle> &array)
	{
		return resolve(array, m_known ? &m_known->triangles : nullptr, &m_next.triangles, BlockKind::Triangles);
	}
	int texCoords(const QVector<ModelTexCoord> &array)
	{
		return resolve(array, m_known ? &m_known->texCoords : nullptr, &m_next.texCoords, BlockKind::TexCoords);
	}
	[[nodiscard]] const QByteArray &digest(int slot) const { return m_digests[size_t(slot)]; }
	[[nodiscard]] qint64 hashedBytes() const { return m_hashedBytes; }

	// Checkpoints run on the calling thread only (core/model_work.h); helper
	// threads hash blocks and nothing else.
	bool run(const ModelWorkControl &control, QString *error)
	{
		for (const auto &job : m_jobs)
		{
			m_hashedBytes += job.bytes();
		}
		std::atomic<size_t> next{0};
		std::atomic<qint64> completed{0};
		std::atomic<bool> stop{false};
		const auto work = [&](bool checkpoints)
		{
			for (;;)
			{
				const auto index = next.fetch_add(1, std::memory_order_relaxed);
				if (index >= m_jobs.size() || stop.load(std::memory_order_relaxed))
				{
					return;
				}
				const auto &job = m_jobs[index];
				m_digests[size_t(job.slot)] = hashBlock(job);
				const auto done = completed.fetch_add(job.bytes(), std::memory_order_relaxed) + job.bytes();
				if (checkpoints && !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, 0, error))
				{
					stop.store(true, std::memory_order_relaxed);
					return;
				}
			}
		};
		// Joined on every way out of this function, an exception included.
		struct Helpers
		{
			std::vector<std::thread> threads;
			~Helpers()
			{
				for (auto &thread : threads)
				{
					if (thread.joinable())
					{
						thread.join();
					}
				}
			}
		};
		Helpers joined;
		auto &helpers = joined.threads;
		if (m_hashedBytes >= parallelHashBytes && m_jobs.size() > 1)
		{
			const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
			const unsigned wanted = std::min({cores - 1, maximumHashHelpers, unsigned(m_jobs.size() - 1)});
			try
			{
				for (unsigned helper = 0; helper < wanted; ++helper)
				{
					helpers.emplace_back(work, false);
				}
			}
			catch (const std::system_error &)
			{
				// No more threads to be had: the ones started and this one finish the work.
			}
		}
		try
		{
			work(true);
		}
		catch (...)
		{
			stop.store(true, std::memory_order_relaxed);
			throw;
		}
		for (auto &helper : helpers)
		{
			helper.join();
		}
		return !stop.load(std::memory_order_relaxed);
	}

	ModelStateBlocks take()
	{
		const auto fill = [this](auto &blocks)
		{
			for (auto it = blocks.begin(); it != blocks.end(); ++it)
			{
				it->digest = m_digests[size_t(m_slots.value(it.key()))];
			}
		};
		fill(m_next.vectors);
		fill(m_next.triangles);
		fill(m_next.texCoords);
		return std::move(m_next);
	}

  private:
	template <typename T>
	int resolve(const QVector<T> &array, const QHash<const void *, ModelStateBlocks::Block<T>> *known,
				QHash<const void *, ModelStateBlocks::Block<T>> *next, BlockKind kind)
	{
		if (array.isEmpty())
		{
			return 0;
		}
		const void *key = array.constData();
		// The same array again in this model, such as a duplicated pose.
		if (const auto seen = next->constFind(key); seen != next->cend())
		{
			if (seen->array.size() == array.size())
			{
				return m_slots.value(key);
			}
			// One allocation seen at two lengths: hash this use on its own.
			const int slot = int(m_digests.size());
			m_digests.emplace_back();
			m_jobs.push_back({kind, key, array.size(), slot});
			return slot;
		}
		const int slot = int(m_digests.size());
		m_digests.emplace_back();
		const auto earlier = known ? known->constFind(key) : typename QHash<const void *, ModelStateBlocks::Block<T>>::const_iterator();
		if (known && earlier != known->cend() && earlier->array.size() == array.size())
		{
			m_digests.back() = earlier->digest;
		}
		else
		{
			m_jobs.push_back({kind, key, array.size(), slot});
		}
		m_slots.insert(key, slot);
		next->insert(key, {array, {}});
		return slot;
	}

	const ModelStateBlocks *m_known;
	ModelStateBlocks m_next;
	QHash<const void *, int> m_slots;
	std::vector<QByteArray> m_digests;
	std::vector<BlockJob> m_jobs;
	qint64 m_hashedBytes = 0;
};
} // namespace

QByteArray modelStateFingerprint(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	return summarizeModelState(mesh, error, control).fingerprint;
}

ModelStateSummary summarizeModelState(const ModelMesh &mesh, QString *error, const ModelWorkControl &control, ModelStateBlocks *blocks)
{
	const auto bytes = metadataBytes(mesh, error, control);
	if (bytes.isEmpty())
	{
		return {};
	}
	ModelStateSummary summary;
	summary.packedSourceBytes = packedSourceBytes(mesh, bytes.size());

	struct SurfaceSlots
	{
		int triangles = 0;
		int texCoords = 0;
		QVector<QPair<int, int>> poses;
	};
	BlockPlan plan(blocks);
	QVector<SurfaceSlots> layout;
	layout.reserve(mesh.surfaces.size());
	for (const auto &surface : mesh.surfaces)
	{
		SurfaceSlots entry;
		entry.triangles = plan.triangles(surface.triangles);
		entry.texCoords = plan.texCoords(surface.texCoords);
		entry.poses.reserve(surface.frames.size());
		for (const auto &pose : surface.frames)
		{
			entry.poses.append({plan.vectors(pose.positions), plan.vectors(pose.normals)});
		}
		layout.append(entry);
	}
	if (!plan.run(control, error))
	{
		return {};
	}

	// The identity: the metadata, then every array's length and digest in
	// model order. Lengths and order are part of it; what was reused is not.
	QCryptographicHash hash(QCryptographicHash::Sha256);
	static constexpr char domain[] = "SlopStudio model revision 3";
	hash.addData(QByteArrayView(domain, sizeof(domain)));
	GeometryHash stream(hash, control, error, plan.hashedBytes());
	if (!stream.count(bytes.size()) || !stream.flush())
	{
		return {};
	}
	hash.addData(bytes);
	if (!stream.count(mesh.surfaces.size()))
	{
		return {};
	}
	for (qsizetype index = 0; index < mesh.surfaces.size(); ++index)
	{
		const auto &surface = mesh.surfaces.at(index);
		const auto &entry = layout.at(index);
		if (!stream.count(surface.triangles.size()) || !stream.digest(plan.digest(entry.triangles)) ||
			!stream.count(surface.texCoords.size()) || !stream.digest(plan.digest(entry.texCoords)))
		{
			return {};
		}
		const auto seams = sortedSeams(surface);
		if (!stream.count(seams.size()))
		{
			return {};
		}
		for (const auto &seam : seams)
		{
			if (!stream.word(quint32(seam.first)) || !stream.word(quint32(seam.second)))
			{
				return {};
			}
		}
		if (!stream.count(surface.frames.size()))
		{
			return {};
		}
		for (qsizetype pose = 0; pose < surface.frames.size(); ++pose)
		{
			const auto &geometry = surface.frames.at(pose);
			if (!stream.count(geometry.positions.size()) || !stream.digest(plan.digest(entry.poses.at(pose).first)) ||
				!stream.count(geometry.normals.size()) || !stream.digest(plan.digest(entry.poses.at(pose).second)))
			{
				return {};
			}
		}
	}
	if (!stream.flush())
	{
		return {};
	}
	summary.fingerprint = hash.result();
	if (blocks)
	{
		*blocks = plan.take();
	}
	return summary;
}

QByteArray modelStateLegacyFingerprint(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	const auto bytes = metadataBytes(mesh, error, control);
	if (bytes.isEmpty())
	{
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	static constexpr char domain[] = "SlopStudio model revision 2";
	hash.addData(QByteArrayView(domain, sizeof(domain)));
	GeometryHash geometry(hash, control, error);
	if (!geometry.count(bytes.size()) || !geometry.flush())
	{
		return {};
	}
	hash.addData(bytes);
	if (!geometry.count(mesh.surfaces.size()))
	{
		return {};
	}
	for (const auto &surface : mesh.surfaces)
	{
		if (!geometry.count(surface.triangles.size()))
		{
			return {};
		}
		for (const auto &triangle : surface.triangles)
		{
			if (!geometry.word(quint32(triangle.a)) || !geometry.word(quint32(triangle.b)) || !geometry.word(quint32(triangle.c)))
			{
				return {};
			}
		}
		if (!geometry.count(surface.texCoords.size()))
		{
			return {};
		}
		for (const auto &uv : surface.texCoords)
		{
			if (!geometry.number(uv.u) || !geometry.number(uv.v))
			{
				return {};
			}
		}
		const auto seams = sortedSeams(surface);
		if (!geometry.count(seams.size()))
		{
			return {};
		}
		for (const auto &seam : seams)
		{
			if (!geometry.word(quint32(seam.first)) || !geometry.word(quint32(seam.second)))
			{
				return {};
			}
		}
		if (!geometry.count(surface.frames.size()))
		{
			return {};
		}
		for (const auto &pose : surface.frames)
		{
			if (!geometry.count(pose.positions.size()))
			{
				return {};
			}
			for (const auto point : pose.positions)
			{
				if (!geometry.vector(point))
				{
					return {};
				}
			}
			if (!geometry.count(pose.normals.size()))
			{
				return {};
			}
			for (const auto normal : pose.normals)
			{
				if (!geometry.vector(normal))
				{
					return {};
				}
			}
		}
	}
	if (!geometry.flush())
	{
		return {};
	}
	return hash.result();
}
} // namespace slopstudio
