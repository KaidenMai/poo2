#pragma once

#include <Geode/Geode.hpp>
#include <functional>

class DecoratePopup : public geode::Popup {
public:
	using Callback = std::function<void(std::string const&)>;
	static DecoratePopup* create(Callback callback);

protected:
	geode::TextInput* m_input = nullptr;
	Callback m_callback;

	bool init(Callback callback);
	void onPreset(cocos2d::CCObject* sender);
	void onGo(cocos2d::CCObject* sender);
};
