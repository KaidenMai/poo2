#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "Decorator.hpp"
#include "DecoratePopup.hpp"

using namespace geode::prelude;

class $modify(AIEditorUI, EditorUI) {
	struct Fields {
		async::TaskHolder<web::WebResponse> m_task;
		Ref<Notification> m_loading;
		bool m_busy = false;
	};

	bool init(LevelEditorLayer* editorLayer) {
		if (!EditorUI::init(editorLayer)) return false;

		auto spr = ButtonSprite::create("AI", "goldFont.fnt", "GJ_button_04.png", .8f);
		spr->setScale(.7f);
		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIEditorUI::onAIDecorate));
		btn->setID("ai-decorate-button"_spr);

		// Put it next to undo/redo if that menu exists, otherwise top center
		if (auto menu = this->getChildByID("undo-menu")) {
			menu->addChild(btn);
			menu->updateLayout();
		}
		else {
			auto menu = CCMenu::create();
			menu->setID("ai-decorate-menu"_spr);
			auto win = CCDirector::get()->getWinSize();
			menu->setPosition({win.width / 2, win.height - 25.f});
			menu->addChild(btn);
			this->addChild(menu);
		}
		return true;
	}

	void onAIDecorate(CCObject*) {
		if (m_fields->m_busy) {
			Notification::create("The AI is still decorating...", NotificationIcon::Info)->show();
			return;
		}
		if (string::trim(Mod::get()->getSettingValue<std::string>("api-key")).empty()) {
			FLAlertLayer::create(
				"AI Decorator",
				"Add your <cy>API key</c> in this mod's settings first (Geode menu > AI Decorator > settings).",
				"OK"
			)->show();
			return;
		}
		DecoratePopup::create([this](std::string const& prompt) {
			this->startDecorate(prompt);
		})->show();
	}

	void startDecorate(std::string const& prompt) {
		auto ctx = ai::buildContext(this);
		if (ctx.objects.empty()) {
			FLAlertLayer::create(
				"AI Decorator",
				"Nothing to decorate! Select some of your layout, or move the camera so it's on screen.",
				"OK"
			)->show();
			return;
		}

		std::string url;
		auto req = ai::makeRequest(ctx, prompt, url);

		m_fields->m_busy = true;
		m_fields->m_loading = Notification::create("AI is decorating...", NotificationIcon::Loading, 0.f);
		m_fields->m_loading->show();

		m_fields->m_task.spawn(
			"AI Decorate",
			req.post(url),
			[this, ctx = std::move(ctx)](web::WebResponse res) mutable {
				// Make sure we touch the editor on the main thread
				Loader::get()->queueInMainThread([this, ctx = std::move(ctx), res = std::move(res)]() mutable {
					this->finishDecorate(ctx, res);
				});
			}
		);
	}

	void finishDecorate(ai::Context const& ctx, web::WebResponse& res) {
		m_fields->m_busy = false;
		if (m_fields->m_loading) {
			m_fields->m_loading->hide();
			m_fields->m_loading = nullptr;
		}

		auto textRes = ai::extractText(res);
		if (!textRes) {
			FLAlertLayer::create("AI Decorator", "Request failed:\n" + textRes.unwrapErr(), "OK")->show();
			return;
		}
		std::string text = textRes.unwrap();

		auto planRes = ai::parsePlan(text, ctx);
		if (!planRes) {
			log::warn("AI response that failed to parse:\n{}", text);
			FLAlertLayer::create("AI Decorator", planRes.unwrapErr(), "OK")->show();
			return;
		}
		ai::Plan p = planRes.unwrap();

		int placed = ai::applyPlan(this, p);
		if (placed == 0) {
			FLAlertLayer::create("AI Decorator", "The game refused to create the objects. Check the log.", "OK")->show();
			return;
		}

		std::string msg = fmt::format("Placed <cg>{}</c> objects", placed);
		if (p.rejected > 0) msg += fmt::format(" (skipped {} invalid)", p.rejected);
		msg += ". They're selected now - <cy>Ctrl+Z</c> undoes the whole batch.";
		if (!p.summary.empty()) msg += "\n\n" + p.summary;
		FLAlertLayer::create(nullptr, "AI Decorator", msg, "Nice", nullptr, 380.f)->show();
	}
};
