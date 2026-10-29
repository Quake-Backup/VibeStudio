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

namespace vibestudio
{

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
	if (!imagePath->isEmpty() && editor->readPackageFiles)
	{
		const auto files = editor->readPackageFiles({*imagePath});
		if (!files.isEmpty() && !files.cbegin().value().isEmpty())
		{
			const QByteArray &bytes = files.cbegin().value();
			const auto format = detectIdTechImageFormat(*imagePath, bytes);
			const auto decoded = decodeIdTechImage(*imagePath, bytes, generatedIdTechPalette(defaultIdTechPaletteIdForFormat(format)));
			if (decoded.decoded && !decoded.image.isNull() && decoded.image.width() <= 4096 && decoded.image.height() <= 4096)
				skin = decoded.image;
		}
	}
	return skin;
}

ModelBakeDialog *ModelEditorTools::openBake()
{
	ModelEditorDialog *editor = m_editor;
	const auto &mesh = editor->document().mesh();
	const int surface = std::max(0, editor->document().selection().surface);
	auto *dialog = new ModelBakeDialog(mesh, surface, editor->m_preview->frame(), editor);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	QString imagePath;
	const QImage skin = surfaceSkin(editor, surface, &imagePath);
	// An MDL skin is shaded in place as one undo step, keeping its
	// fullbright indices (224 and up) lit.
	if (const int slot = modelSkinPaintMdlSlot(mesh, surface); slot >= 0)
	{
		dialog->setApplyTarget(
			QCoreApplication::translate("VibeStudioModelBake", "Shade MDL skin %1 with an occlusion or lighting map, as one undo step.").arg(slot + 1),
			[editor, surface](const QImage &map, ModelBakeType type, double strength, QString *error)
			{
				const auto &current = editor->document().mesh();
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
			QCoreApplication::translate("VibeStudioModelBake", "Shade %1 with an occlusion or lighting map, or stage a normal map beside it, in the open package.")
				.arg(imagePath),
			[editor, imagePath, skin](const QImage &map, ModelBakeType type, double strength, QString *error)
			{
				QByteArray bytes;
				QString path = imagePath;
				if (type == ModelBakeType::AmbientOcclusion || type == ModelBakeType::Lighting)
				{
					QImage shaded;
					if (!multiplyModelBakeIntoSkin(skin, map, type, strength, -1, &shaded, error) || !encodeModelPaintedSkin(shaded, imagePath, &bytes, error))
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
	// Apply to Model: an MDL skin member as one undo step, or the image
	// staged back into the open package at the path it came from.
	if (const int slot = modelSkinPaintMdlSlot(mesh, surface); slot >= 0)
	{
		dialog->setApplyTarget(
			QCoreApplication::translate("VibeStudioModelSkinPaint", "Replace MDL skin %1 with the painted skin, as one undo step.").arg(slot + 1),
			[editor, surface](const QImage &painted, QString *error)
			{
				ModelEdit edit;
				if (!modelSkinPaintMdlEdit(editor->document().mesh(), surface, painted, &edit, error))
					return false;
				edit.selection = editor->document().selection();
				return editor->applyEdit(edit, error);
			});
	}
	else if (!imagePath.isEmpty() && editor->stageSkinImage)
	{
		dialog->setApplyTarget(
			QCoreApplication::translate("VibeStudioModelSkinPaint", "Stage the painted skin as %1 in the open package.").arg(imagePath),
			[editor, imagePath](const QImage &painted, QString *error)
			{
				QByteArray bytes;
				if (!encodeModelPaintedSkin(painted, imagePath, &bytes, error) || !editor->stageSkinImage(imagePath, bytes, error))
					return false;
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
						editor->m_status->setText(QCoreApplication::translate("VibeStudioModelSkinPaint",
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

} // namespace vibestudio
