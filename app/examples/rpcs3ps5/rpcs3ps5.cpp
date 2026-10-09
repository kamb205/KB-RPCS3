/*
 * RPCS3 PS5 - its program, made from the PS5 Vulkan Template's UI starter.
 *
 * One complete design of PS5_VKHomebrewUI's kit (ps5/ui/kit.hpp) fills the
 * screen: its procedural backdrop, its scene, its frosted overlay and its post
 * overlay, its sounds on the console's audio output, its rumble and its light
 * bar colour. There is no switcher: every button is the design's, and holding
 * OPTIONS for a second leaves (back to the menu, or out of a title of one
 * program). The design is this title's copy of the kit's "aurora", in
 * kit/screen.cpp, to change at will.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "kit.hpp"
#include "kit/library.hpp"
#include "kit/requests.hpp"
#include "kit/core.hpp"
#include "kit/tiles.hpp"

#include <fstream>
#include <string>
#include <vector>

// The design on screen: this title's own, in kit/screen.cpp
std::unique_ptr<hui::app::Concept> makeScreen(hui::app::Context &context)
{
	return ps5ui::make_screen(context);
}

class VulkanExample : public ps5ui::KitExample
{
public:
	// What a design receives: the fonts, the sample content, the live numbers
	// it may show and the settings it may change
	hui::Settings choices;
	hui::app::Telemetry telemetry;
	std::unique_ptr<hui::app::Context> context;
	std::unique_ptr<hui::app::Concept> screen;
	hui::app::Frame frame;
	hui::ui::Feedback feedback;
	ps5ui::HoldToLeave leaving;
	hui::gfx::DrawList leavingList;
	double fpsSeconds{ 0.0 };
	int fpsFrames{ 0 };
	// Started from a game's PS5 home-screen tile: its launch screen, then the game (main.cpp)
	bool splash{ false };
	float splashTime{ 0.0f };
	std::uint32_t splashBackground{ 0 }, splashCover{ 0 };
	int splashCoverW{ 0 }, splashCoverH{ 0 };
	std::string splashTitle;

	VulkanExample() : KitExample()
	{
		title = "KB-RPCS3";
		name = "rpcs3ps5";
		// The kit draws the whole screen and owns the pad
		settings.overlay = false;
		ps5.ownOverlay = true;
		defaultClearColor = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		prepareKit();
		rpcs3ps5::library().scan(&kit.renderer);
		reportScan();
		context = std::make_unique<hui::app::Context>(hui::app::Context{ kit.fonts, kit.catalog, telemetry, choices });
		screen = makeScreen(*context);
		screen->enter();
		kit.apply(choices);
		if (!rpcs3ps5::launch_splash().empty()) {
			prepareSplash(rpcs3ps5::launch_splash());
		} else {
			feedback.play(hui::audio::Cue::welcome);
		}
		prepared = true;
	}

	// An image file as a texture (0 when it can't be read)
	std::uint32_t loadTexture(const std::vector<std::uint8_t> &file, int *width = nullptr, int *height = nullptr)
	{
		int w = 0, h = 0, channels = 0;
		stbi_uc *rgba = file.empty() ? nullptr : stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &channels, 4);
		if (!rgba) {
			return 0;
		}
		const std::uint32_t texture = kit.renderer.create_texture(w, h, rgba);
		stbi_image_free(rgba);
		if (width) *width = w;
		if (height) *height = h;
		return texture;
	}

	void prepareSplash(const std::string &boot)
	{
		splash = true;
		for (const rpcs3ps5::Game &game : rpcs3ps5::library().games()) {
			if (game.boot != boot) {
				continue;
			}
			splashTitle = game.title;
			std::vector<std::uint8_t> file;
			if (rpcs3ps5::game_file(game, "PIC1.PNG", file)) {
				splashBackground = loadTexture(file);
			}
			std::ifstream cover(rpcs3ps5::cover_path(game.title_id), std::ios::binary);
			file.assign(std::istreambuf_iterator<char>(cover), {});
			splashCover = loadTexture(file, &splashCoverW, &splashCoverH);
			if (!splashCover && game_file(game, "ICON0.PNG", file)) {
				splashCover = loadTexture(file, &splashCoverW, &splashCoverH);
			}
			break;
		}
		rpcs3ps5::trace("splash: %s (background %u, cover %ux%u)", splashTitle.c_str(), splashBackground, splashCoverW, splashCoverH);
	}

	// The launch screen: the game's background, dimmed, its box art on the left and its name
	void drawSplash(float dt)
	{
		using hui::gfx::Color;
		splashTime += dt;
		if (splashTime > 2.6f) {
			quit = true; // main.cpp starts the game
		}
		const float fade = std::min(1.0f, splashTime / 0.5f) * std::min(1.0f, std::max(0.0f, (2.6f - splashTime) / 0.3f));
		const float W = hui::gfx::kVirtualWidth, H = hui::gfx::kVirtualHeight;
		frame.reset();
		frame.backdrop.mode = hui::gfx::BackdropMode::gradient;
		frame.backdrop.colors[0] = Color::rgb(0x000000);
		frame.backdrop.colors[1] = Color::rgb(0x05070c);
		frame.backdrop.colors[2] = Color::rgb(0x10131c);
		hui::gfx::DrawList &list = frame.scene;
		list.push_opacity(fade);
		if (splashBackground) {
			// a slow push-in
			const float grow = 1.0f + 0.03f * std::min(1.0f, splashTime / 2.6f);
			const float w = W * grow, h = H * grow;
			list.image(splashBackground, { (W - w) * 0.5f, (H - h) * 0.5f, w, h }, hui::gfx::kFullUv, Color::rgb(0xffffff));
		}
		list.gradient_rect_h({ 0, 0, W, H }, 0, Color::rgb(0x000000, 0.78f), Color::rgb(0x000000, 0.25f));
		float textX = 160.0f;
		if (splashCover && splashCoverH > 0) {
			const float h = 720.0f;
			const float w = std::min(620.0f, h * (float)splashCoverW / (float)splashCoverH);
			const float y = (H - h) * 0.5f;
			list.glow({ 160, y, w, h }, 12, 60, Color::rgb(0x000000, 0.6f));
			list.image(splashCover, { 160, y, w, h }, hui::gfx::kFullUv, Color::rgb(0xffffff), 12);
			textX = 160.0f + w + 90.0f;
		}
		hui::ui::text(list, kit.fonts.semibold, splashTitle, textX, H * 0.5f - 10, 64, Color::rgb(0xffffff));
		const int dots = 1 + (int)(splashTime * 3.0f) % 3;
		hui::ui::text(list, kit.fonts.regular, std::string("Starting") + std::string(dots, '.'), textX, H * 0.5f + 60, 30,
			Color::rgb(0xffffff, 0.75f));
		list.pop_opacity();
		kit.renderer.begin();
		kit.renderer.backdrop(frame.backdrop);
		kit.renderer.draw(frame.scene);
		buildKitCommandBuffer();
		submitFrame();
	}

	void reportScan()
	{
		const auto &lib = rpcs3ps5::library();
		rpcs3ps5::set_status(std::to_string(lib.games().size()) + " games found" +
			(lib.firmware_installed() ? "" : "  \xC2\xB7  PS3 system software not installed yet"));
	}

	// What the home screen asked for this frame
	void handleRequests()
	{
		// A game was deleted (on the tiles thread): list the library again
		if (rpcs3ps5::take_rescan_wanted()) {
			vkDeviceWaitIdle(device);
			rpcs3ps5::library().scan(&kit.renderer);
		}
		const rpcs3ps5::Pending pending = rpcs3ps5::take_request();
		const auto &games = rpcs3ps5::library().games();
		const bool valid_game = pending.argument >= 0 && pending.argument < (int)games.size();
		switch (pending.what) {
		case rpcs3ps5::Request::tile_add:
			if (valid_game)
				rpcs3ps5::tile_add_async(games[pending.argument]);
			break;
		case rpcs3ps5::Request::tile_remove:
			if (valid_game)
				rpcs3ps5::tile_remove_async(games[pending.argument]);
			break;
		case rpcs3ps5::Request::delete_game:
		case rpcs3ps5::Request::delete_game_and_saves:
			if (valid_game)
				rpcs3ps5::game_delete_async(games[pending.argument], pending.what == rpcs3ps5::Request::delete_game_and_saves);
			break;
		case rpcs3ps5::Request::none:
			break;
		case rpcs3ps5::Request::rescan:
			vkDeviceWaitIdle(device);
			rpcs3ps5::library().scan(&kit.renderer);
			reportScan();
			break;
		case rpcs3ps5::Request::boot:
			if (pending.argument >= 0 && pending.argument < (int)games.size()) {
				if (!rpcs3ps5::core_linked()) {
					rpcs3ps5::set_status("Booting " + games[pending.argument].title + ": the emulator core is not linked into this build yet");
				} else if (!rpcs3ps5::library().firmware_installed()) {
					rpcs3ps5::set_status("Install the PS3 system software first (Install tab)");
				} else if (rpcs3ps5::core_busy() || rpcs3ps5::tiles_busy()) {
					rpcs3ps5::set_status("Wait for the install to finish");
				} else {
					// The home screen closes; main.cpp runs the game and brings it back after
					rpcs3ps5::set_boot_path(games[pending.argument].boot);
					quit = true;
				}
			}
			break;
		case rpcs3ps5::Request::game_settings:
			rpcs3ps5::set_status("Per-game settings: coming with the emulator core");
			break;
		case rpcs3ps5::Request::install_firmware:
			if (rpcs3ps5::library().firmware_pup_present()) {
				rpcs3ps5::core_install_firmware_async(rpcs3ps5::Library::root() + "/PS3UPDAT.PUP");
			} else {
				rpcs3ps5::set_status("No PS3UPDAT.PUP in " + rpcs3ps5::Library::root());
			}
			break;
		case rpcs3ps5::Request::install_packages:
			rpcs3ps5::core_install_packages_async();
			break;
		case rpcs3ps5::Request::install_licenses: {
			const int count = rpcs3ps5::core_install_licenses();
			rpcs3ps5::set_status(count ? std::to_string(count) + " licence(s) (.rap) installed  \xC2\xB7  PSN games that need them can start now"
			                           : "No .rap files in " + rpcs3ps5::Library::root() + "/exdata: copy them there over FTP, then choose this again");
			break;
		}
		case rpcs3ps5::Request::toggle_setting:
			// The row past the settings is a game's "use the global settings"
			if (pending.argument >= rpcs3ps5::setting_count()) {
				rpcs3ps5::reset_game_settings();
				rpcs3ps5::set_status(rpcs3ps5::settings_game_title() + " uses the global settings");
			} else {
				rpcs3ps5::cycle_setting(pending.argument);
				rpcs3ps5::set_status(std::string(rpcs3ps5::setting_label(pending.argument)) + ": " +
					rpcs3ps5::setting_text(pending.argument) +
					(rpcs3ps5::settings_for_game() ? "  \xC2\xB7  saved for " + rpcs3ps5::settings_game_title() : "  \xC2\xB7  saved for all games"));
			}
			break;
		}
	}

	// L3 + R3 (both sticks pressed): the screen to <data>/screenshots/home-<time>.ppm
	bool screenshotCombo = false;
	void screenshotOnCombo()
	{
		const bool both = (ps5_pad().held & (PAD_L3 | PAD_R3)) == (PAD_L3 | PAD_R3);
		if (both && !screenshotCombo && !ps5.captureNow) {
			const std::string dir = rpcs3ps5::Library::root() + "/screenshots";
			mkdir(dir.c_str(), 0777);
			chmod(dir.c_str(), 0777);
			char name[64];
			std::snprintf(name, sizeof(name), "/home-%ld.ppm", (long)time(nullptr));
			ps5.screenshotPath = dir + name;
			ps5.captureNow = true;
			rpcs3ps5::set_status("Screenshot saved: rpcs3/screenshots" + std::string(name));
			feedback.play(hui::audio::Cue::select);
		}
		screenshotCombo = both;
	}

	// Holding OPTIONS for a second leaves; a press is the design's
	void holdOptions(float dt)
	{
		leaving.label = ps5.optionsEnds ? "Back to the menu" : "Close";
		if (!ps5.frameBudget && leaving.update(dt, (ps5_pad().held & PAD_OPTIONS) != 0)) {
			quit = true;
		}
	}

	void updateTelemetry()
	{
		if (benchmark.active) {
			// A test run's pictures must not depend on the console's timing
			telemetry.push(16.7f);
			telemetry.fps = 59.9f;
			telemetry.average_ms = 16.7f;
		} else {
			telemetry.push(frameTimer * 1000.0f);
			fpsSeconds += frameTimer;
			fpsFrames++;
			if (fpsSeconds >= 0.5) {
				telemetry.fps = (float)(fpsFrames / fpsSeconds);
				telemetry.average_ms = (float)(fpsSeconds * 1000.0 / fpsFrames);
				fpsSeconds = 0.0;
				fpsFrames = 0;
			}
			telemetry.voices = kit.mixer.active_voices();
		}
		telemetry.draw_calls = kit.renderer.last_draw_calls();
		telemetry.instances = kit.renderer.last_instances();
	}

	void render() override
	{
		if (!prepared) {
			return;
		}
		prepareFrame();
		const float dt = std::min(frameTimer, 0.05f);
		if (splash) {
			kit.tick(dt);
			drawSplash(dt);
			return;
		}
		// A test run ignores the pad: the design shows its entrance and rests
		hui::InputFrame input = kit.input();
		if (ps5.frameBudget) {
			input = hui::InputFrame{};
			input.connected = true;
		}
		holdOptions(dt);
		screenshotOnCombo();
		updateTelemetry();
		screen->update(input, dt, feedback);
		// The home screen never ends on CIRCLE: holding OPTIONS closes the title
		handleRequests();
		if (context->settings_changed) {
			context->settings_changed = false;
			kit.apply(choices);
		}
		kit.play(feedback, screen->info().sounds, choices.haptics);
		feedback.clear();
		if (choices.light_bar) {
			kit.light_bar(screen->info().accent);
		}
		kit.tick(dt);

		// backdrop, scene, [the glass copy], overlay, post: the order a design is drawn in
		frame.reset();
		frame.glass_texture = kit.renderer.glass_texture();
		screen->draw(frame);
		kit.renderer.begin();
		kit.renderer.backdrop(frame.backdrop);
		kit.renderer.draw(frame.scene);
		if (frame.glass) {
			kit.renderer.glass();
		}
		kit.renderer.draw(frame.overlay);
		kit.renderer.backdrop(frame.post);
		leavingList.clear();
		leaving.draw(leavingList, kit.fonts, ps5ui::active_theme());
		kit.renderer.draw(leavingList);
		buildKitCommandBuffer();
		submitFrame();
	}

	~VulkanExample() override
	{
		if (device) {
			vkDeviceWaitIdle(device);
			rpcs3ps5::library().release(kit.renderer);
			if (splashBackground) kit.renderer.destroy_texture(splashBackground);
			if (splashCover) kit.renderer.destroy_texture(splashCover);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
