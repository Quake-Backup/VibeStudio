#pragma once

#include "core/model_stage.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;

namespace vibestudio
{

// The review before Stage in Package or Stage and Place (SI-2): every file the
// set stages with its role, size and whether it replaces a package or staged
// file, the materials the model uses resolved against the package, and what
// the exports left out. Replacing existing files is chosen here; staging stays
// disabled while files would be replaced and that is not chosen.
class ModelStageReviewDialog final : public QDialog
{
  public:
	ModelStageReviewDialog(const ModelStageReview &review, bool place, bool replaceExisting, QWidget *parent = nullptr);
	[[nodiscard]] bool replaceExisting() const;

  private:
	void refreshState();
	ModelStageReview m_review;
	QCheckBox *m_replace = nullptr;
	QLabel *m_blocked = nullptr;
	QPushButton *m_stage = nullptr;
};

} // namespace vibestudio
