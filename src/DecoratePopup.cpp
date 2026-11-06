#include "DecoratePopup.hpp"

using namespace geode::prelude;

namespace {
struct Preset {
	char const* name;
	char const* prompt;
};
constexpr Preset PRESETS[] = {
	{"Glow", "clean modern glow deco, outlined blocks, soft glow along edges, dark background, cyan and purple"},
	{"Spooky", "spooky dark deco, jagged shapes, hanging details, deep red and black, eerie atmosphere"},
	{"Tech", "sci-fi tech deco, straight lines, panels and circuit-like details, blue and white"},
	{"Nature", "natural organic deco, rounded shapes, foliage-like clusters, greens and warm browns"},
	{"Space", "space theme, scattered stars and dots in the background, big faint shapes, dark blue and pink"},
};
}

DecoratePopup* DecoratePopup::create(Callback callback) {
	auto ret = new DecoratePopup();
	if (ret->init(std::move(callback))) {
		ret->autorelease();
		return ret;
	}
	delete ret;
	return nullptr;
}

bool DecoratePopup::init(Callback callback) {
	if (!Popup::init(360.f, 220.f)) return false;
	m_callback = std::move(callback);
	this->setTitle("AI Decorator");

	auto size = m_mainLayer->getContentSize();

	auto hint = CCLabelBMFont::create(
		"Select your layout (or just have it on screen), then describe the vibe.",
		"chatFont.fnt"
	);
	hint->setScale(.6f);
	hint->setPosition({size.width / 2, size.height - 48.f});
	m_mainLayer->addChild(hint);

	m_input = TextInput::create(320.f, "e.g. neon glow, purple + cyan, tech vibe");
	m_input->setCommonFilter(CommonFilter::Any);
	m_input->setMaxCharCount(300);
	m_input->setPosition({size.width / 2, size.height / 2 + 12.f});
	m_mainLayer->addChild(m_input);

	auto presetMenu = CCMenu::create();
	presetMenu->ignoreAnchorPointForPosition(false);
	presetMenu->setContentSize({330.f, 30.f});
	presetMenu->setPosition({size.width / 2, size.height / 2 - 30.f});
	int tag = 0;
	for (auto& preset : PRESETS) {
		auto spr = ButtonSprite::create(preset.name, "bigFont.fnt", "GJ_button_05.png", .6f);
		spr->setScale(.55f);
		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(DecoratePopup::onPreset));
		btn->setTag(tag++);
		presetMenu->addChild(btn);
	}
	presetMenu->setLayout(RowLayout::create()->setGap(6.f));
	m_mainLayer->addChild(presetMenu);

	auto goSpr = ButtonSprite::create("Decorate!", "goldFont.fnt", "GJ_button_01.png", .8f);
	auto goBtn = CCMenuItemSpriteExtra::create(goSpr, this, menu_selector(DecoratePopup::onGo));
	goBtn->setPosition({size.width / 2, 30.f});
	m_buttonMenu->addChild(goBtn);

	return true;
}

void DecoratePopup::onPreset(CCObject* sender) {
	auto tag = static_cast<CCNode*>(sender)->getTag();
	if (tag >= 0 && tag < static_cast<int>(std::size(PRESETS))) {
		m_input->setString(PRESETS[tag].prompt);
	}
}

void DecoratePopup::onGo(CCObject*) {
	auto prompt = m_input->getString();
	auto cb = m_callback;
	this->onClose(nullptr);
	if (cb) cb(prompt);
}
