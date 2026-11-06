#include "Decorator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

using namespace geode::prelude;

namespace ai {

static constexpr size_t MAX_CONTEXT_OBJECTS = 400;
static constexpr size_t MAX_PALETTE = 40;
static constexpr float AREA_MARGIN_X = 150.f;
static constexpr float AREA_MARGIN_Y = 240.f;

// ---------------------------------------------------------------- helpers

static char typeLetter(GameObject* obj) {
	switch (obj->m_objectType) {
		case GameObjectType::Solid: return 'S';
		case GameObjectType::Hazard: return 'H';
		case GameObjectType::Decoration: return 'D';
		default: return 'O';
	}
}

// Triggers, orbs, pads, portals, start pos etc. are all EffectGameObjects.
// We never want the AI placing those.
static bool isGameplaySpecial(GameObject* obj) {
	return typeinfo_cast<EffectGameObject*>(obj) != nullptr;
}

static std::set<int> parseIdList(std::string const& str) {
	std::set<int> out;
	std::stringstream ss(str);
	std::string part;
	while (std::getline(ss, part, ',')) {
		auto r = numFromString<int>(string::trim(part));
		if (r && r.unwrap() > 0) out.insert(r.unwrap());
	}
	return out;
}

static int zLayerFromString(std::string s) {
	s = string::toUpper(string::trim(s));
	if (s == "B5") return -5;
	if (s == "B4") return -3;
	if (s == "B3") return -1;
	if (s == "B2") return 1;
	if (s == "B1") return 3;
	if (s == "T1") return 5;
	if (s == "T2") return 7;
	if (s == "T3") return 9;
	if (s == "T4") return 11;
	return 0;
}

static ObjInfo infoFor(GameObject* obj) {
	ObjInfo i;
	i.id = obj->m_objectID;
	i.x = obj->getPositionX();
	i.y = obj->getPositionY();
	i.rot = obj->getRotation();
	i.scale = std::abs(obj->getScaleX());
	i.color = obj->m_baseColor ? obj->m_baseColor->m_colorID : 0;
	i.type = typeLetter(obj);
	return i;
}

// ---------------------------------------------------------------- context

Context buildContext(EditorUI* ui) {
	Context ctx;
	auto lel = ui->m_editorLayer;

	std::vector<GameObject*> picked;
	auto selected = ui->getSelectedObjects();
	if (selected && selected->count() > 0) {
		ctx.fromSelection = true;
		for (auto obj : CCArrayExt<GameObject*>(selected)) picked.push_back(obj);
	}
	else {
		// nothing selected: use whatever is on screen right now
		auto win = CCDirector::get()->getWinSize();
		auto layer = lel->m_objectLayer;
		auto bl = layer->convertToNodeSpace({0.f, 0.f});
		auto tr = layer->convertToNodeSpace({win.width, win.height});
		CCRect view(bl.x, bl.y, tr.x - bl.x, tr.y - bl.y);
		for (auto obj : CCArrayExt<GameObject*>(lel->m_objects)) {
			if (view.containsPoint(obj->getPosition())) picked.push_back(obj);
		}
	}

	std::sort(picked.begin(), picked.end(), [](GameObject* a, GameObject* b) {
		return a->getPositionX() < b->getPositionX();
	});

	// Downsample evenly if there's a ton of stuff so the prompt stays small
	size_t step = std::max<size_t>(1, picked.size() / MAX_CONTEXT_OBJECTS + (picked.size() % MAX_CONTEXT_OBJECTS ? 1 : 0));
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	for (size_t i = 0; i < picked.size(); i++) {
		auto obj = picked[i];
		auto pos = obj->getPosition();
		minX = std::min(minX, pos.x); maxX = std::max(maxX, pos.x);
		minY = std::min(minY, pos.y); maxY = std::max(maxY, pos.y);
		if (i % step == 0) ctx.objects.push_back(infoFor(obj));
		if (!isGameplaySpecial(obj) && obj->m_baseColor && obj->m_baseColor->m_colorID > 0)
			ctx.usedColors.insert(obj->m_baseColor->m_colorID);
	}
	if (picked.empty()) return ctx;

	ctx.area = CCRect(
		minX - AREA_MARGIN_X, std::max(0.f, minY - AREA_MARGIN_Y),
		(maxX - minX) + AREA_MARGIN_X * 2, (maxY - minY) + AREA_MARGIN_Y * 2
	);

	// Palette: what the creator already uses (minus gameplay stuff + hazards)
	std::map<int, int> counts;
	for (auto obj : CCArrayExt<GameObject*>(lel->m_objects)) {
		if (isGameplaySpecial(obj) || obj->m_objectType == GameObjectType::Hazard) continue;
		counts[obj->m_objectID]++;
	}
	// anything in the selection always counts, even if rare
	for (auto obj : picked) {
		if (isGameplaySpecial(obj) || obj->m_objectType == GameObjectType::Hazard) continue;
		counts[obj->m_objectID] += 1000;
	}
	std::vector<std::pair<int, int>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
	for (size_t i = 0; i < sorted.size() && i < MAX_PALETTE; i++) {
		ctx.palette[sorted[i].first] = sorted[i].second % 1000;
		ctx.allowed.insert(sorted[i].first);
	}
	for (int id : parseIdList(Mod::get()->getSettingValue<std::string>("extra-ids"))) {
		ctx.allowed.insert(id);
		if (!ctx.palette.contains(id)) ctx.palette[id] = 0;
	}
	return ctx;
}

// ---------------------------------------------------------------- prompt

static std::string systemPrompt(int maxObjects) {
	return fmt::format(R"(You are an expert Geometry Dash (2.2) level decorator working directly inside the level editor.
You get the objects in one section of a level plus a palette of object IDs you may use, and you design decoration for that section.

COORDINATES
- 1 block = 30 units. x grows to the right (direction the player travels), y grows upward.
- Positions are object centers. A full block resting on the ground sits at about y=105; the ground line is about y=90.
- Object type letters: S = solid (gameplay), H = hazard, D = decoration, O = other.

DESIGN RULES
- ONLY use object IDs from the palette. Use your knowledge of what those GD object IDs look like.
- Never block the player's path: keep the open space roughly 1-4 blocks above solid tops clear, unless it's subtle background deco on B3-B5.
- Good deco: outlines/edges hugging block surfaces, fill under and behind structures, glow along edges, background shapes and patterns, clusters with varied scale and rotation, motifs repeating every few blocks for rhythm.
- Snap to a 7.5 or 15 unit grid where it makes sense. Rotations in multiples of 15 unless organic.
- Pick 2-4 color channels (1-999) and use them consistently. Prefer channels not already in use unless matching existing style.
- Z layers: B5 B4 B3 B2 B1 are behind, T1 T2 T3 in front. Background deco on B layers.
- Place between 20 and {} objects, all inside the placement area.

OUTPUT
Reply with ONLY one JSON object, no markdown, no commentary:
{{"summary":"1-2 sentences, including the colors you suggest for each channel you used","objects":[{{"id":1,"x":0,"y":0,"rot":0,"scale":1,"color":0,"detail":0,"z":"B1","zorder":0,"flipX":false,"flipY":false}}]}}
Only id, x and y are required per object.)", maxObjects);
}

static std::string userPrompt(Context const& ctx, std::string const& request) {
	std::string out;
	out += fmt::format("Creator's request: {}\n\n", request.empty() ? "tasteful modern decoration that fits the layout" : request);
	out += fmt::format("Placement area: x {:.0f} to {:.0f}, y {:.0f} to {:.0f}\n\n",
		ctx.area.getMinX(), ctx.area.getMaxX(), ctx.area.getMinY(), ctx.area.getMaxY());

	out += "Palette (object id: times already used in level):\n";
	for (auto& [id, count] : ctx.palette) out += fmt::format("{}:{} ", id, count);
	out += "\n\n";

	if (!ctx.usedColors.empty()) {
		out += "Color channels already used here: ";
		for (int c : ctx.usedColors) out += fmt::format("{} ", c);
		out += "\n\n";
	}

	out += "Existing objects (type,id,x,y,rot,scale,color):\n";
	for (auto& o : ctx.objects) {
		out += fmt::format("{},{},{:.1f},{:.1f},{:.0f},{:.2f},{}\n", o.type, o.id, o.x, o.y, o.rot, o.scale, o.color);
	}
	return out;
}

// ---------------------------------------------------------------- request

web::WebRequest makeRequest(Context const& ctx, std::string const& request, std::string& outUrl) {
	auto mod = Mod::get();
	auto provider = mod->getSettingValue<std::string>("provider");
	auto key = mod->getSettingValue<std::string>("api-key");
	auto model = std::string(string::trim(mod->getSettingValue<std::string>("model")));
	key = std::string(string::trim(key));
	if (model.empty()) {
		if (provider == "Gemini") model = "gemini-2.5-flash";
		else if (provider == "Anthropic") model = "claude-sonnet-5-5";
		else model = "gpt-4o-mini";
	}
	int maxObjects = static_cast<int>(mod->getSettingValue<int64_t>("max-objects"));

	auto sys = systemPrompt(maxObjects);
	auto user = userPrompt(ctx, request);

	web::WebRequest req;
	req.timeout(std::chrono::seconds(180));
	req.header("Content-Type", "application/json");

	auto messages = matjson::Value::array();

	if (provider == "OpenAI-compatible" || provider == "Gemini") {
		std::string base = provider == "Gemini"
			? std::string("https://generativelanguage.googleapis.com/v1beta/openai")
			: std::string(string::trim(mod->getSettingValue<std::string>("base-url")));
		while (!base.empty() && base.back() == '/') base.pop_back();
		outUrl = base + "/chat/completions";
		req.header("Authorization", "Bearer " + key);

		messages.push(matjson::makeObject({{"role", "system"}, {"content", sys}}));
		messages.push(matjson::makeObject({{"role", "user"}, {"content", user}}));
		auto body = matjson::makeObject({
			{"model", model},
			{"messages", messages},
		});
		req.bodyJSON(body);
	}
	else {
		outUrl = "https://api.anthropic.com/v1/messages";
		req.header("x-api-key", key);
		req.header("anthropic-version", "2023-06-01");

		messages.push(matjson::makeObject({{"role", "user"}, {"content", user}}));
		auto body = matjson::makeObject({
			{"model", model},
			{"max_tokens", 16000},
			{"system", sys},
			{"messages", messages},
		});
		req.bodyJSON(body);
	}
	return req;
}

// ---------------------------------------------------------------- response

Result<std::string> extractText(web::WebResponse& res) {
	auto raw = res.string().unwrapOr("");
	auto jsonRes = matjson::parse(raw);
	if (!jsonRes) {
		return Err(fmt::format("HTTP {}: {}", res.code(), raw.substr(0, 300)));
	}
	matjson::Value json = jsonRes.unwrap();
	// Google's OpenAI-compatible endpoint wraps errors in an array
	if (json.isArray() && json.size() > 0) json = json[0];

	if (!res.ok()) {
		log::warn("AI request failed ({}): {}", res.code(), raw);
		auto msg = json["error"]["message"].asString().unwrapOr("");
		if (msg.empty()) msg = json["error"].asString().unwrapOr("");
		if (msg.empty()) msg = json["message"].asString().unwrapOr("");
		if (msg.empty()) msg = raw.substr(0, 300);
		return Err(fmt::format("HTTP {}: {}", res.code(), msg));
	}

	// Anthropic: { content: [ {type:"text", text:"..."} ] }
	if (json["content"].isArray()) {
		std::string text;
		for (auto& block : json["content"]) {
			if (block["type"].asString().unwrapOr("") == "text") text += block["text"].asString().unwrapOr("");
		}
		if (!text.empty()) return Ok(text);
	}
	// OpenAI: { choices: [ { message: { content: "..." } } ] }
	if (json["choices"].isArray()) {
		for (auto& choice : json["choices"]) {
			auto text = choice["message"]["content"].asString().unwrapOr("");
			if (!text.empty()) return Ok(text);
		}
	}
	return Err("The AI returned an empty response");
}

Result<Plan> parsePlan(std::string const& text, Context const& ctx) {
	auto start = text.find('{');
	auto end = text.rfind('}');
	if (start == std::string::npos || end == std::string::npos || end <= start) {
		return Err("The AI didn't send back any JSON");
	}
	auto parsed = matjson::parse(std::string_view(text).substr(start, end - start + 1));
	if (!parsed) {
		return Err("The AI's JSON was broken (it may have been cut off - try a lower Max Objects)");
	}
	matjson::Value const json = parsed.unwrap();

	Plan plan;
	plan.summary = json["summary"].asString().unwrapOr("");
	int maxObjects = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-objects"));

	if (!json["objects"].isArray()) return Err("The AI's JSON had no object list");

	for (auto& o : json["objects"]) {
		if ((int)plan.objects.size() >= maxObjects) { plan.rejected++; continue; }

		PlacedObj p;
		p.id = static_cast<int>(o["id"].asInt().unwrapOr(0));
		p.x = static_cast<float>(o["x"].asDouble().unwrapOr(NAN));
		p.y = static_cast<float>(o["y"].asDouble().unwrapOr(NAN));

		if (!ctx.allowed.contains(p.id) || std::isnan(p.x) || std::isnan(p.y) ||
			!ctx.area.containsPoint({p.x, p.y})) {
			plan.rejected++;
			continue;
		}

		p.rot = static_cast<float>(std::fmod(o["rot"].asDouble().unwrapOr(0.0), 360.0));
		p.scale = std::clamp(static_cast<float>(o["scale"].asDouble().unwrapOr(1.0)), 0.1f, 8.f);
		p.color = std::clamp(static_cast<int>(o["color"].asInt().unwrapOr(0)), 0, 999);
		p.detail = std::clamp(static_cast<int>(o["detail"].asInt().unwrapOr(0)), 0, 999);
		p.zLayer = zLayerFromString(o["z"].asString().unwrapOr(""));
		p.zOrder = std::clamp(static_cast<int>(o["zorder"].asInt().unwrapOr(0)), -100, 100);
		p.flipX = o["flipX"].asBool().unwrapOr(false);
		p.flipY = o["flipY"].asBool().unwrapOr(false);
		plan.objects.push_back(p);
	}

	if (plan.objects.empty()) {
		return Err(fmt::format("The AI's objects were all invalid ({} rejected). Try again or add palette IDs.", plan.rejected));
	}
	return Ok(plan);
}

// ---------------------------------------------------------------- placing

int applyPlan(EditorUI* ui, Plan const& plan) {
	auto mod = Mod::get();
	bool noTouch = mod->getSettingValue<bool>("no-touch");
	int editorLayer = static_cast<int>(mod->getSettingValue<int64_t>("editor-layer"));

	// Build a normal GD object string so the game handles all the parsing
	std::string str;
	for (auto& p : plan.objects) {
		str += fmt::format("1,{},2,{:.2f},3,{:.2f}", p.id, p.x, p.y);
		if (p.rot != 0.f) str += fmt::format(",6,{:.2f}", p.rot);
		if (p.scale != 1.f) str += fmt::format(",128,{:.3f},129,{:.3f}", p.scale, p.scale);
		if (p.flipX) str += ",4,1";
		if (p.flipY) str += ",5,1";
		if (p.color > 0) str += fmt::format(",21,{}", p.color);
		if (p.detail > 0) str += fmt::format(",22,{}", p.detail);
		if (p.zLayer != 0) str += fmt::format(",24,{}", p.zLayer);
		if (p.zOrder != 0) str += fmt::format(",25,{}", p.zOrder);
		if (editorLayer != 0) str += fmt::format(",20,{}", editorLayer);
		if (noTouch) str += ",121,1";
		str += ";";
	}

	auto lel = ui->m_editorLayer;
	auto created = lel->createObjectsFromString(str, true, true);
	if (!created || created->count() == 0) return 0;

	// One undo step for the whole batch so Ctrl+Z removes all of it
	lel->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);

	ui->deselectAll();
	ui->selectObjects(created, true);
	ui->updateButtons();
	return static_cast<int>(created->count());
}

} // namespace ai
