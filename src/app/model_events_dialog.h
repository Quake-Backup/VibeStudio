#pragma once

#include "app/live_status_label.h"
#include "core/model_document.h"

#include <QDialog>

class QComboBox;
class QPushButton;
class QTableWidget;

namespace vibestudio
{
// Skeleton > Animation Events: the frame commands of one skeletal clip (or,
// for a model without joints, one vertex animation) in an editable table of
// frame and command line. Apply returns one EditAnimationEvents edit that
// replaces the clip's list.
class ModelEventsDialog final : public QDialog
{
  public:
	// `frame` picks the clip shown first and the frame new events start on.
	ModelEventsDialog(const ModelMesh &mesh, int frame, QWidget *parent = nullptr);
	[[nodiscard]] ModelEdit edit() const;
	// Adds a row at the frame shown in the editor (or the clip's first frame).
	void addEvent();
	// The table's events; false with the reason in the status line.
	bool tableEvents(QVector<ModelAnimationEvent> *events) const;

  private:
	ModelMesh m_mesh;
	int m_frame = 0;
	QComboBox *m_clip = nullptr;
	QTableWidget *m_table = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_remove = nullptr;
	void load();
	void validate();
	[[nodiscard]] bool skeletal() const;
	[[nodiscard]] int frameCount() const;
};
} // namespace vibestudio
