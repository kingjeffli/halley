#pragma once
#include "assets_editor_window.h"

namespace Halley {
	class PrefabEditor : public AssetEditor {
	public:
		PrefabEditor(UIFactory& factory, Resources& resources, AssetType type, Project& project, ProjectWindow& projectWindow);

		void reload() override;
		void onDoubleClick() override;

	protected:
		void update(Time t, bool moved) override;
		
	private:
		Project& project;
		ProjectWindow& projectWindow;

		void setupWindow();
		void open();
		void updateButton();
	};
}
