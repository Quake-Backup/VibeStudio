// UV > Paint Skin… in the Mesh Editor (UVSKIN-09): opens the paint dialog on
// the active surface's skin, hands the result back to the model, and paints
// through the editor's own viewport.

#include "app/model_bake_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_skin_paint_dialog.h"
#include "app/model_viewport.h"

#include "core/idtech_image.h"
#include "core/level_materials.h"
#include "core/model_skin_paint.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QPointer>

#include <algorithm>
#include <memory>

namespace slopstudio
{
namespace
{
template <typename T, typename Same> bool sameArray(const QVector<T> &a, const QVector<T> &b, Same same)
{
	if (a.size() != b.size())
		return false;
	// Unedited arrays stay shared between document states.
	return a.constData() == b.constData() || std::equal(a.cbegin(), a.cend(), b.cbegin(), same);
}
bool sameVectors(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	return sameArray(a, b, [](const ModelVec3 &p, const ModelVec3 &q) { return p.x == q.x && p.y == q.y && p.z == q.z; });
}
// The surface's faces and UVs, which a skin is painted or baked against.
bool sameSurfaceLayout(const ModelSurface &a, const ModelSurface &b)
{
	return a.name == b.name && a.vertexCount == b.vertexCount &&
		   sameArray(a.triangles, b.triangles, [](const ModelTriangle &p, const ModelTriangle &q) { return p.a == q.a && p.b == q.b && p.c == q.c; }) &&
		   sameArray(a.texCoords, b.texCoords, [](const ModelTexCoord &p, const ModelTexCoord &q) { return p.u == q.u && p.v == q.v; });
}
// What a bake read: every surface's layout and its pose at the baked frame
// (a bake of every surface lays them all into one map; occlusion sees them all).
bool sameBakeInput(const ModelMesh &then, const ModelMesh &now, int frame)
{
	if (then.surfaces.size() != now.surfaces.size())
		return false;
	for (int s = 0; s < then.surfaces.size(); ++s)
	{
		const auto &a = then.surfaces[s], &b = now.surfaces[s];
		if (!sameSurfaceLayout(a, b) || a.frames.size() != b.frames.size())
			return false;
		if (frame >= 0 && frame < a.frames.size() &&
			(!sameVectors(a.frames[frame].positions, b.frames[frame].positions) || !sameVectors(a.frames[frame].normals, b.frames[frame].normals)))
			return false;
	}
	return true;
}
// The package's image at `path` as it is now, decoded; null when it cannot be read.
QByteArray packageBytes(ModelEditorDialog *editor, const QString &path)
{
	if (path.isEmpty() || !editor->readPackageFiles)
		return {};
	const auto files = editor->readPackageFiles({path});
	return files.isEmpty() ? QByteArray() : files.cbegin().value();
}
QImage packageSkin(ModelEditorDialog *editor, const QString &path)
{
	const QByteArray bytes = packageBytes(editor, path);
	if (bytes.isEmpty())
		return {};
	const auto format = detectIdTechImageFormat(path, bytes);
	const auto decoded = decodeIdTechImage(path, bytes, generatedIdTechPalette(defaultIdTechPaletteIdForFormat(format)));
	if (decoded.decoded && !decoded.image.isNull() && decoded.image.width() <= 4096 && decoded.image.height() <= 4096)
		return decoded.image;
	return {};
}
} // namespace

// The surface's skin: its MDL skin as indices, else its material image (read
// at full size from the package when it can be, with `imagePath` set to its
// package path), else null.
QImage ModelEditorTools::surfaceSkin(ModelEditorDialog *editor, int surface, QString *imagePath)
{
	const auto &mesh = editor->document().mesh();
	QImage skin = modelSurfaceIndexedSkin(mesh, surface);
	if (!skin.isNull())
		return skin;
	const auto key = modelPreviewSurfaceMaterialKey(surface);
	for (const auto &material : editor->m_materialAssets.materials)
	{
		if (material.key == key && material.ready())
		{
			skin = material.image;
			*imagePath = material.imagePath;
			break;
		}
	}
	if (const auto full = packageSkin(editor, *imagePath); !full.isNull())
		skin = full;
	return skin;
}

ModelBakeDialog *ModelEditorTools::openBake()
{
	ModelEditorDialog *editor = m_editor;
	const auto &mesh = editor->document().mesh();
	const int surface = std::max(0, editor->document().selection().surface);
	const int frame = editor->m_preview->frame();
	auto *dialog = new ModelBakeDialog(mesh, surface, frame, editor);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	QString imagePath;
	const QImage skin = surfaceSkin(editor, surface, &imagePath);
	// The dialog stays open while the model is edited. The map fits the
	// faces, UVs and pose it was baked from; an edit that changes those
	// leaves it applying to the wrong texels, so Apply refuses then.
	const auto fits = [editor, baked = mesh, frame](QString *error)
	{
		if (sameBakeInput(baked, editor->document().mesh(), frame))
			return true;
		*error = QCoreApplication::translate("SlopStudioModelBake",
			"The model's faces, UVs or pose changed since Bake Maps opened, so this map no longer lines up with the skin. Close Bake Maps and bake again.");
		return false;
	};
	// An MDL skin is shaded in place as one undo step, keeping its
	// fullbright indices (224 and up) lit.
	if (const int slot = modelSkinPaintMdlSlot(mesh, surface); slot >= 0)
	{
		dialog->setApplyTarget(
			QCoreApplication::translate("SlopStudioModelBake", "Shade MDL skin %1 with an occlusion or lighting map, as one undo step.").arg(slot + 1),
			[editor, surface, slot, fits](const QImage &map, ModelBakeType type, double strength, QString *error)
			{
				const auto &current = editor->document().mesh();
				if (!fits(error))
					return false;
				if (modelSkinPaintMdlSlot(current, surface) != slot)
				{
					*error = QCoreApplication::translate("SlopStudioModelBake",
						"The surface's MDL skin changed since Bake Maps opened. Close Bake Maps and open it again.");
					return false;
				}
				QImage shaded;
				ModelEdit edit;
				if (!multiplyModelBakeIntoSkin(modelSurfaceIndexedSkin(current, surface), map, type, strength, 224, &shaded, error) ||
					!modelSkinPaintMdlEdit(current, surface, shaded, &edit, error))
					return false;
				edit.selection = editor->document().selection();
				return editor->applyEdit(edit, error);
			});
	}
	else if (!imagePath.isEmpty() && !skin.isNull() && editor->stageSkinImage)
	{
		// A package image: occlusion and lighting shade it, a normal map is
		// staged beside it (Doom 3's _local for tangent space).
		dialog->setApplyTarget(
			QCoreApplication::translate("SlopStudioModelBake", "Shade %1 with an occlusion or lighting map, or stage a normal map beside it, in the open package.")
				.arg(imagePath),
			[editor, imagePath, skin, fits](const QImage &map, ModelBakeType type, double strength, QString *error)
			{
				if (!fits(error))
					return false;
				QByteArray bytes;
				QString path = imagePath;
				if (type == ModelBakeType::AmbientOcclusion || type == ModelBakeType::Lighting)
				{
					// Shade the image as the package holds it now, so what was
					// painted or applied since the dialog opened is kept.
					const QImage current = packageSkin(editor, imagePath);
					QImage shaded;
					if (!multiplyModelBakeIntoSkin(current.isNull() ? skin : current, map, type, strength, -1, &shaded, error) ||
						!encodeModelPaintedSkin(shaded, imagePath, &bytes, error))
						return false;
				}
				else
				{
					path = modelBakeCompanionPath(imagePath, type);
					if (!encodeModelPaintedSkin(map, path, &bytes, error))
						return false;
				}
				if (!editor->stageSkinImage(path, bytes, error))
					return false;
				editor->reloadMaterials();
				return true;
			});
	}
	dialog->show();
	return dialog;
}

ModelSkinPaintDialog *ModelEditorTools::openSkinPaint()
{
	ModelEditorDialog *editor = m_editor;
	const auto &mesh = editor->document().mesh();
	const int surface = std::max(0, editor->document().selection().surface);
	QString imagePath;
	const QImage skin = surfaceSkin(editor, surface, &imagePath);
	auto *dialog = new ModelSkinPaintDialog(mesh, surface, editor->m_preview->frame(), skin, editor);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	QPointer<ModelSkinPaintDialog> guard(dialog);
	QPointer<ModelViewport> view(editor->m_preview);
	// The dialog stays open while the model is edited. The painting lines up
	// with the surface's faces and UVs as the dialog last read them (painting
	// in the view reads the model again), and it replaces the whole skin: an
	// edit that moved the UVs or the surface, or another change to the skin
	// since (an applied bake), makes Apply refuse rather than undo it.
	const int surfaceCount = int(mesh.surfaces.size());
	const auto fits = [editor, guard, surface, surfaceCount](QString *error)
	{
		const auto &now = editor->document().mesh();
		const auto *painted = guard ? &guard->mesh() : nullptr;
		if (painted && surface < now.surfaces.size() && surface < painted->surfaces.size() && now.surfaces.size() == surfaceCount &&
			sameSurfaceLayout(painted->surfaces[surface], now.surfaces[surface]))
			return true;
		*error = QCoreApplication::translate("SlopStudioModelSkinPaint",
			"The surface or its UVs changed since Paint Skin opened, so the painting no longer lines up with the model. Close Paint Skin and open it again.");
		return false;
	};
	const auto skinChanged = []
	{
		return QCoreApplication::translate("SlopStudioModelSkinPaint",
			"The skin changed since Paint Skin opened (a bake or another paint was applied to it); applying would discard that change. Close Paint Skin and open it again.");
	};
	// Apply to Model: an MDL skin member as one undo step, or the image
	// staged back into the open package at the path it came from.
	if (const int slot = modelSkinPaintMdlSlot(mesh, surface); slot >= 0)
	{
		// The skins this dialog has seen in the model: the one it opened on
		// and each it applied (Undo and Redo move between them).
		auto known = std::make_shared<QVector<QImage>>(QVector<QImage>{modelSurfaceIndexedSkin(mesh, surface)});
		dialog->setApplyTarget(
			QCoreApplication::translate("SlopStudioModelSkinPaint", "Replace MDL skin %1 with the painted skin, as one undo step.").arg(slot + 1),
			[editor, surface, slot, fits, skinChanged, known](const QImage &painted, QString *error)
			{
				if (!fits(error))
					return false;
				const auto &current = editor->document().mesh();
				if (modelSkinPaintMdlSlot(current, surface) != slot || !known->contains(modelSurfaceIndexedSkin(current, surface)))
				{
					*error = skinChanged();
					return false;
				}
				ModelEdit edit;
				if (!modelSkinPaintMdlEdit(current, surface, painted, &edit, error))
					return false;
				edit.selection = editor->document().selection();
				if (!editor->applyEdit(edit, error))
					return false;
				known->append(modelSurfaceIndexedSkin(editor->document().mesh(), surface));
				return true;
			});
	}
	else if (!imagePath.isEmpty() && editor->stageSkinImage)
	{
		// The package's file as the dialog opened on it and as it staged it.
		auto staged = std::make_shared<QByteArray>(packageBytes(editor, imagePath));
		dialog->setApplyTarget(
			QCoreApplication::translate("SlopStudioModelSkinPaint", "Stage the painted skin as %1 in the open package.").arg(imagePath),
			[editor, imagePath, fits, skinChanged, staged](const QImage &painted, QString *error)
			{
				if (!fits(error))
					return false;
				// Unreadable at opening: nothing to compare with.
				if (!staged->isEmpty() && packageBytes(editor, imagePath) != *staged)
				{
					*error = skinChanged();
					return false;
				}
				QByteArray bytes;
				if (!encodeModelPaintedSkin(painted, imagePath, &bytes, error) || !editor->stageSkinImage(imagePath, bytes, error))
					return false;
				*staged = packageBytes(editor, imagePath);
				editor->reloadMaterials();
				return true;
			});
	}
	// The painted skin in the main view while painting there.
	const auto show = [editor, view, surface](const QImage &painted)
	{
		if (!view)
			return;
		QHash<int, QImage> skins = editor->m_surfaceImages;
		if (!editor->m_mdlPreview.isNull())
			for (int s = 0; s < editor->document().mesh().surfaces.size(); ++s)
				skins.insert(s, editor->m_mdlPreview);
		skins.insert(surface, painted.convertToFormat(QImage::Format_ARGB32));
		view->setGameSkins(skins, {}, false);
	};
	dialog->skinChanged = [guard, show](const QImage &painted)
	{
		if (guard && guard->paintingInView())
			show(painted);
	};
	dialog->paintInView = [editor, guard, view, show](bool on)
	{
		if (!view || !guard)
			return;
		if (!on)
		{
			view->setSkinPaintHandler({});
			view->setGameSkins({}, {}, false);
			editor->refreshGamePreview();
			return;
		}
		// Painting shows the skin, so flat and wireframe shading give way.
		if (editor->m_renderMode->currentIndex() < 2)
			editor->m_renderMode->setCurrentIndex(3);
		show(guard->skin());
		view->setSkinPaintHandler(
			[editor, guard, view](ModelViewportStrokePhase phase, const QPointF &point)
			{
				if (!guard || !view)
					return false;
				switch (phase)
				{
				case ModelViewportStrokePhase::Begin:
				{
					if (view->frame() != guard->frame())
					{
						guard->setPaintInView(true);
						editor->m_status->setText(QCoreApplication::translate("SlopStudioModelSkinPaint",
							"Paint Skin paints frame %1; show it in the view to paint there.").arg(guard->frame()), LiveStatus::Problem);
						return false;
					}
					guard->updateModel(editor->document().mesh());
					ModelSkinPaintProjection projection;
					ModelSkinPaintView paintView;
					QString error;
					if (!view->skinPaintProjection(&projection) || !buildModelSkinPaintView(guard->mesh(), guard->frame(), projection, &paintView, &error))
					{
						editor->m_status->setText(error, LiveStatus::Problem);
						return false;
					}
					return guard->beginViewStroke(point, paintView);
				}
				case ModelViewportStrokePhase::Extend:
					return guard->extendStroke(point);
				case ModelViewportStrokePhase::Finish:
					return guard->finishStroke(true);
				case ModelViewportStrokePhase::Cancel:
					return guard->finishStroke(false);
				}
				return false;
			});
	};
	dialog->show();
	return dialog;
}

} // namespace slopstudio
