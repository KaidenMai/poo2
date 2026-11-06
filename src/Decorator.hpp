#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace ai {

struct ObjInfo {
	int id;
	float x, y;
	float rot;
	float scale;
	int color;
	char type; // S solid, H hazard, D deco, O other
};

// Everything we tell the AI about the area it should decorate.
struct Context {
	std::vector<ObjInfo> objects;
	cocos2d::CCRect area;          // where the AI is allowed to place stuff
	std::map<int, int> palette;    // object id -> times used in level
	std::set<int> allowed;         // ids the AI may place
	std::set<int> usedColors;      // color channels already in use nearby
	bool fromSelection = false;
};

struct PlacedObj {
	int id;
	float x, y;
	float rot = 0.f;
	float scale = 1.f;
	int color = 0;
	int detail = 0;
	int zLayer = 0;   // GD ZLayer enum value, 0 = default
	int zOrder = 0;
	bool flipX = false;
	bool flipY = false;
};

struct Plan {
	std::string summary;
	std::vector<PlacedObj> objects;
	int rejected = 0;
};

// Collect selected objects (or the visible area if nothing is selected).
Context buildContext(EditorUI* ui);

// Builds the HTTP request for whichever provider is configured.
geode::utils::web::WebRequest makeRequest(Context const& ctx, std::string const& userPrompt, std::string& outUrl);

// Pulls the model's text out of the provider response.
geode::Result<std::string> extractText(geode::utils::web::WebResponse& res);

// Turns the model's JSON into a validated list of objects.
geode::Result<Plan> parsePlan(std::string const& text, Context const& ctx);

// Places the objects in the editor (one undo step). Returns how many were created.
int applyPlan(EditorUI* ui, Plan const& plan);

} // namespace ai
