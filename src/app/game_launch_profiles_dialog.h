#pragma once

#include "core/build_pipeline.h"

#include <QDialog>
#include <QMap>
#include <QVector>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace vibestudio {

// The user's own launch profiles, after TrenchBroom's game engine profiles:
// add, name and remove profiles and type each one's engine arguments with
// {map}, {mod}, {basedir}, {bsp}, {iwad} and {warp}. The preview fills the
// tokens with the Launch and Test tab's current values. OK stays disabled
// while a profile has a problem, which is shown in words beside it.
class GameLaunchProfilesDialog final : public QDialog {
	Q_OBJECT

public:
	// `starterArguments` are the arguments a new profile starts with, such as
	// the installation's built-in profile's.
	GameLaunchProfilesDialog(const QVector<GameLaunchProfile>& profiles, const QString& starterArguments, QWidget* parent = nullptr);

	void setPreviewTokens(const QMap<QString, QString>& tokens);
	[[nodiscard]] QVector<GameLaunchProfile> profiles() const;
	[[nodiscard]] QString currentProfileId() const;
	// What Add and Remove do.
	void addProfile();
	void removeCurrentProfile();

private:
	void showProfile(int row);
	void storeCurrent();
	void refreshState();
	[[nodiscard]] QString problemFor(const GameLaunchProfile& profile) const;

	QVector<GameLaunchProfile> m_profiles;
	QString m_starter;
	QMap<QString, QString> m_tokens;
	QListWidget* m_list = nullptr;
	QLineEdit* m_name = nullptr;
	QComboBox* m_engine = nullptr;
	QLineEdit* m_arguments = nullptr;
	QLabel* m_preview = nullptr;
	QLabel* m_problem = nullptr;
	QPushButton* m_remove = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
	int m_row = -1;
	bool m_filling = false;
};

} // namespace vibestudio
