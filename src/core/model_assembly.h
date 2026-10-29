#pragma once

#include "core/model_document.h"
#include "core/model_pose.h"
#include "core/model_q3_animation.h"
#include "core/model_skin_bindings.h"

#include <QHash>
#include <memory>
#include <optional>

namespace vibestudio
{
inline constexpr int modelAssemblyMaxParts = 32;
inline constexpr qint64 modelAssemblyMaxSourceBytes = 1024 * 1024;
inline constexpr qint64 modelAssemblyMaxInputBytes = 256 * 1024 * 1024;

enum class ModelAssemblySource
{
	File,
	Package
};
struct ModelAssemblySkin
{
	QString source;
	ModelAssemblySource sourceKind = ModelAssemblySource::File;
	int entryIndex = -1; // -1 requires a unique package path; files always use -1.
};
struct ModelAssemblyPart
{
	QString id, source, parent, tag;
	ModelAssemblySource sourceKind = ModelAssemblySource::File;
	ModelVec3 translation, rotation;
	double scale = 1;
	int firstFrame = 0, lastFrame = -1;
	double framesPerSecond = 10, phase = 0;
	bool loop = true, interpolate = true;
	std::optional<ModelAssemblySkin> skin;
	// Preview roll about the part's own X axis, in revolutions per minute, as
	// Quake III spins a weapon's _barrel model (schema 4; native exports leave it out).
	double spinRpm = 0;
};
struct ModelAssemblyQ3Animation
{
	ModelQ3AnimationConfig config;
	QString lowerPart, upperPart;
	int lowerAnimation = 22, upperAnimation = 11; // LEGS_IDLE / TORSO_STAND
};
// How attachment tags blend between stored frames. Recipes without the field
// keep ShortestArc; schema 4 stores Quake3Renderer, the game's per-axis blend.
enum class ModelTagInterpolation
{
	ShortestArc,
	Quake3Renderer
};
struct ModelAssembly
{
	QString name = QStringLiteral("Assembly");
	QVector<ModelAssemblyPart> parts;
	std::optional<ModelAssemblyQ3Animation> q3Animation;
	ModelTagInterpolation tagInterpolation = ModelTagInterpolation::ShortestArc;
};
// Stable recipe/CLI identifiers: "shortest-arc" and "quake3".
QString modelTagInterpolationId(ModelTagInterpolation mode);
std::optional<ModelTagInterpolation> modelTagInterpolationFromId(const QString &id);
// The schema a recipe needs: 1 ordinary, 2 native animation, 3 linked skins,
// 4 attachment settings (tag interpolation, joint attachments).
int modelAssemblySchemaVersion(const ModelAssembly &assembly);
// A part tag "joint:<name>" attaches to that joint of a skeletal (MD5, IQM or
// skeletal source) parent instead of an ordinary tag.
QString modelAssemblyJointPrefix();
struct ModelAssemblyContext
{
	// File references are relative to the assembly source's directory.
	QString directory;
	std::shared_ptr<const PackageArchiveReader> archive;
	QString paletteId;
};
struct ModelAssemblySkinInput
{
	QString source, resolvedPath;
	ModelAssemblySource sourceKind = ModelAssemblySource::File;
	qsizetype entryIndex = -1;
	QByteArray sha256;
	qint64 bytes = 0;
	ModelSkinBindingPlan bindings;
};
struct ModelAssemblyInput
{
	QString part, source;
	ModelAssemblySource sourceKind = ModelAssemblySource::File;
	QByteArray sha256;
	qint64 bytes = 0;
	ModelMesh mesh;
	std::optional<ModelAssemblySkinInput> skin;
};
struct ModelAssemblyResolved
{
	QByteArray recipeSha256;
	QVector<ModelAssemblyInput> inputs;
	QVector<int> order;
	QStringList notes;
};
struct ModelAssemblyPose
{
	// A transient, single-pose composite. The recipe and original inputs retain
	// independent frames, native metadata and tags; baking is explicitly lossy.
	ModelMesh mesh;
	QVector<int> surfaceParts;
	QVector<ModelAnimationSample> samples;
	QVector<ModelTag> partTransforms;
	QHash<int, QImage> embeddedSkins;
	QStringList notes;
};

bool validateModelAssembly(const ModelAssembly &assembly, QString *error = nullptr, QVector<int> *order = nullptr);
QJsonObject modelAssemblyJson(const ModelAssembly &assembly);
bool parseModelAssembly(const QByteArray &bytes, ModelAssembly *assembly, QString *error = nullptr);
QByteArray modelAssemblyFingerprint(const ModelAssembly &assembly);
// Linked skins change only the immutable resolved model, never its input file.
bool validateModelAssemblySkin(const ModelAssemblySkin &skin, QString *error = nullptr);
bool resolveModelAssemblySkin(const ModelAssemblySkin &skin, const ModelAssemblyContext &context, ModelAssemblyInput *input,
							  QString *error = nullptr, const ModelWorkControl &control = {});
bool modelAssemblyInputProtectsPath(const ModelAssemblyInput &input, const QString &path);
QJsonObject modelAssemblySkinInputJson(const ModelAssemblySkinInput &skin);
// All 31 slots must fit their bound models, including unselected clips.
bool validateModelAssemblyQ3Animation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, QString *error = nullptr);
bool exportModelAssemblyQ3Animation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const QString &path,
									const QString &sourcePath, bool overwrite, bool dryRun, QString *error = nullptr,
									const ModelWorkControl &control = {}, const std::shared_ptr<const PackageArchiveReader> &archive = {});
// Immutable, bounded input snapshots. No model or package is rewritten.
bool resolveModelAssembly(const ModelAssembly &assembly, const ModelAssemblyContext &context, ModelAssemblyResolved *resolved,
						  QString *error = nullptr, const ModelWorkControl &control = {});
// With includeCollision, every part's collision volumes (sampled at the part's
// own frame) are carried through its attachment basis and uniform scale into
// pose->mesh.collisionBoxes as <part>_<volume>; at most 64 in all.
bool sampleModelAssembly(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, double seconds, ModelAssemblyPose *pose,
						 QString *error = nullptr, const ModelWorkControl &control = {}, bool includeCollision = false);
// One explicit static bake; protects the assembly source and every input.
bool exportModelAssemblyPose(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, double seconds, const QString &path,
							 const QString &sourcePath, bool overwrite, bool dryRun, QString *error = nullptr,
							 const ModelWorkControl &control = {}, ModelExportReport *report = nullptr,
							 const std::shared_ptr<const PackageArchiveReader> &archive = {}, bool includeCollision = false);

class ModelAssemblyDocument
{
  public:
	const ModelAssembly &assembly() const
	{
		return m_state.assembly;
	}
	QString selectedPart() const
	{
		return m_state.selected;
	}
	QString path() const
	{
		return m_path;
	}
	QString directory() const
	{
		return m_directory;
	}
	QByteArray sourceFingerprint() const
	{
		return m_sourceHash;
	}
	QString recoverySource() const
	{
		return m_recoverySource;
	}
	bool isModified() const;
	bool canUndo() const
	{
		return !m_undo.isEmpty();
	}
	bool canRedo() const
	{
		return !m_redo.isEmpty();
	}
	bool setAssembly(const ModelAssembly &assembly, const QString &directory, QString *error = nullptr);
	// Recovery has no save binding, even for an empty recipe. Its original source
	// remains protected when Save As explicitly permits replacement elsewhere.
	bool restoreDraft(const ModelAssembly &assembly, const QString &directory, const QString &selected, const QString &sourcePath,
					  QString *error = nullptr);
	bool load(const QString &path, QString *error = nullptr, const ModelWorkControl &control = {});
	bool save(const QString &path, bool overwrite = false, QString *error = nullptr, const ModelWorkControl &control = {},
			  bool dryRun = false, const std::shared_ptr<const PackageArchiveReader> &archive = {});
	bool setPart(const QString &previousId, const ModelAssemblyPart &part, QString *error = nullptr);
	// Binding native animation to an assembly without one also selects Quake III
	// renderer tag interpolation, in the same undo step.
	bool setQ3Animation(const std::optional<ModelAssemblyQ3Animation> &animation, QString *error = nullptr);
	bool setTagInterpolation(ModelTagInterpolation mode, QString *error = nullptr);
	bool removeBranch(const QString &id, QString *error = nullptr);
	void selectPart(const QString &id);
	bool undo();
	bool redo();

  private:
	struct State
	{
		ModelAssembly assembly;
		QString selected;
	};
	State m_state;
	QVector<State> m_undo, m_redo;
	QString m_path, m_resolvedPath, m_directory;
	QString m_recoverySource, m_recoveryResolvedSource;
	QByteArray m_sourceHash, m_savedHash;
	bool m_recovered = false;
	void commit(State next);
};
} // namespace vibestudio
