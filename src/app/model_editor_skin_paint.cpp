// UV > Paint Skin… in the Mesh Editor (UVSKIN-09): opens the paint dialog on
// the active surface's skin, hands the result back to the model, and paints
// through the editor's own viewport.

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

ModelSkinPaintDialog *ModelEditorTools::openSkinPaint()
{
	ModelEditorDialog *editor = m_editor;
	const auto &mesh = editor->document().mesh();
	const int surface = std::max(0, editor->document().selection().surface);
	// The skin: the surface's MDL skin as indices, else its material image
	// (read at full size from the package when it can be), else blank.
	QImage skin = modelSurfaceIndexedSkin(mesh, surface);
	QString imagePath;
	if (skin.isNull())
	{
		const auto key = modelPreviewSurfaceMaterialKey(surface);
		for (const auto &material : editor->m_materialAssets.materials)
		{
			if (material.key == key && material.ready())
			{
				skin = material.image;
				imagePath = material.imagePath;
				break;
			}
		}
		if (!imagePath.isEmpty() && editor->readPackageFiles)
		{
			const auto files = editor->readPackageFiles({imagePath});
			if (!files.isEmpty() && !files.cbegin().value().isEmpty())
			{
				const QByteArray &bytes = files.cbegin().value();
				const auto format = detectIdTechImageFormat(imagePath, bytes);
				const auto decoded = decodeIdTechImage(imagePath, bytes, generatedIdTechPalette(defaultIdTechPaletteIdForFormat(format)));
				if (decoded.decoded && !decoded.image.isNull() && decoded.image.width() <= 4096 && decoded.image.height() <= 4096)
					skin = decoded.image;
			}
		}
	}
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
