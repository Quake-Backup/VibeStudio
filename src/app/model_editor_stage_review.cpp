// Stage in Package's review (SI-2): the files of the step, what they replace,
// and the materials the model uses, before anything is staged.

#include "app/model_editor_dialog.h"
#include "app/model_stage_review_dialog.h"
#include "core/model_stage.h"

namespace vibestudio
{

bool ModelEditorDialog::confirmStage(const ModelStageSet &set, bool place, bool *replace)
{
	// An editor hosted without a package plan has nothing to compare against.
	if (!reviewStage)
		return true;
	const auto review = reviewStage(set, m_document.mesh());
	if (stageReviewChoice)
	{
		const int choice = stageReviewChoice(review);
		if (choice == 2)
			*replace = true;
		return choice > 0 && (review.conflicts() == 0 || *replace);
	}
	ModelStageReviewDialog dialog(review, place, *replace, this);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	*replace = dialog.replaceExisting();
	return true;
}

} // namespace vibestudio
