# AI Decorator (Geode mod)

Adds an **AI** button to the Geometry Dash editor. Select a chunk of your layout, type a vibe ("neon glow, purple + cyan"), and an AI places decoration around it.

Targets **Geode 5.10.1 / GD 2.2081**. If your SDK is a different version, change `"geode"` in `mod.json` to match.

## How it works

1. Collects the selected objects (or everything on screen if nothing's selected): ID, position, rotation, scale, color, and whether it's solid/hazard/deco.
2. Builds a palette from object IDs already in your level and selection, plus the *Extra Palette IDs* setting. The AI can only place those IDs, so it can't spawn triggers, orbs, portals or spikes.
3. Sends that to Claude (or any OpenAI-compatible API) and asks for JSON back.
4. Checks every object (ID in palette, inside the area near your layout, scale clamped, capped at *Max Objects*).
5. Creates them all as one undo step, no-touch by default, optionally on a dedicated editor layer, and selects them so you can move/recolor them.

The AI picks color channels and tells you what colors to set for them in the result popup.

## Build it (easiest: GitHub)

1. Make a new GitHub repo and push this folder to it.
2. The included workflow (`.github/workflows/multi-platform.yml`) builds automatically on push.
3. Go to **Actions → latest run → Build Output**, download, unzip, and drop the `.geode` file into your `geode/mods` folder.

## Build it locally

You need the Geode SDK + CLI set up ([docs.geode-sdk.org/getting-started](https://docs.geode-sdk.org/getting-started/)), then:

```sh
geode build
```

## Settings

| Setting | What it does |
|---|---|
| AI Provider | `Anthropic` or `OpenAI-compatible` (OpenAI, OpenRouter, Groq, LM Studio, Ollama…) |
| API Key | Your key. Stored in plain text in your Geode save folder |
| Model | Default `claude-sonnet-5-5`; change it if you switch providers |
| Base URL | For OpenAI-compatible only, e.g. `https://openrouter.ai/api/v1` or `http://localhost:1234/v1` |
| Max Objects | Cap per run (default 150) |
| No-Touch | Makes placed objects have no collision (on by default) |
| Editor Layer | Puts AI objects on their own editor layer |
| Extra Palette IDs | IDs the AI can always use, comma-separated |

## Tips

- **Results depend on the palette.** In an empty level the AI only has the default block and the black block. Put a few deco objects you like (glow pieces, outlines, circles…) in your selection and it'll use those.
- Do it a section at a time (about 1 to 3 screens). Huge selections get downsampled.
- Didn't like it? Ctrl+Z and run it again, or tweak the prompt.
- If the JSON gets cut off, lower *Max Objects*.

## Files

- `src/main.cpp`: editor button, request lifecycle, result popup
- `src/Decorator.cpp`: context gathering, prompt, API calls, parsing/validation, placing objects
- `src/DecoratePopup.cpp`: prompt popup with presets
