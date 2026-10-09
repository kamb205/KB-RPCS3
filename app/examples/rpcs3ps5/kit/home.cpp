// RPCS3 PS5 - the home screen, laid out like the PS3's XMB (cross media bar): categories in a row,
// each category's items in a column under it, a slow translucent wave behind everything and the
// clock in the corner. Drawn from scratch in that style (no Sony artwork or sounds).
// SPDX-License-Identifier: GPL-3.0-or-later
//
//   Left / Right   category: Settings, Game, Install, System
//   Up / Down      item;  Cross: start / choose;  Triangle: a game's options;  Circle: back
//   Square         look for games again
//
// The previous design (UI kit "Aurora Shelf") is in git history.

#include "concepts/concepts.hpp"
#include "core/tween.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"

#include "core.hpp"
#include "library.hpp"
#include "requests.hpp"
#include "tiles.hpp"
#include "recommend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace hui::screen
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Color kWhite = Color::rgb(0xffffff);

// The bar: the focused category sits at kBarX; the others follow at kCategoryGap
constexpr float kBarX = 470.0f;
constexpr float kBarY = 300.0f;      // centre of the category icons
constexpr float kCategoryGap = 210.0f;
constexpr float kItemX = kBarX;      // items line up under the focused category
constexpr float kFirstItemY = 470.0f; // centre of the focused item
constexpr float kItemPitch = 104.0f;
constexpr float kGameItemPitch = 150.0f;

enum Category : int
{
	kSettings = 0,
	kGame,
	kInstall,
	kSystem,
	kCategoryCount
};
constexpr const char *kCategoryNames[kCategoryCount] = {"Settings", "Game", "Install", "System"};

// The Settings column: the PC version's settings tabs, each a page of RPCS3's settings in those sections
struct SettingsTab
{
	const char *title;
	const char *sections[2];
};
constexpr SettingsTab kTabs[] = {
    {"CPU", {"Core", nullptr}},          {"GPU", {"Video", nullptr}},       {"Audio", {"Audio", nullptr}},
    {"I/O", {"Input/Output", nullptr}},  {"System", {"System", nullptr}},   {"Network", {"Net", nullptr}},
    {"Emulator", {"Miscellaneous", "Savestate"}},
};
constexpr int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
constexpr const char *kVibration[] = {"Off", "Low", "Medium", "High"};

// Plain-text lines of at most `width` characters
std::vector<std::string> wrap(const std::string &text, std::size_t width)
{
	std::vector<std::string> lines;
	std::string line;
	std::size_t at = 0;
	while (at < text.size()) {
		std::size_t end = text.find(' ', at);
		if (end == std::string::npos)
			end = text.size();
		const std::string word = text.substr(at, end - at);
		if (!line.empty() && line.size() + 1 + word.size() > width) {
			lines.push_back(line);
			line.clear();
		}
		line += (line.empty() ? "" : " ") + word;
		at = end + 1;
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

constexpr const char *kTechniques[] = {
    "A category bar with item columns, as on the PS3",
    "Translucent wave ribbons over a slow gradient",
    "Springs for every movement; game icons from ICON0.PNG",
};

// A PS3-style month colour for the background (the XMB changed its colour with the month)
Color month_colour()
{
	static const std::uint32_t kMonths[12] = {0x8c8c94, 0xb89b2a, 0x5f9b30, 0xc06a92, 0x2f8f3a, 0x8a5bb8,
	                                          0x2fa0b8, 0x2d58b8, 0x7a4fb0, 0xb87a2a, 0x8a6a3a, 0xb03030};
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_r(&now, &local);
	return Color::rgb(kMonths[local.tm_mon % 12]);
}

class Home final : public app::Concept
{
public:
	explicit Home(app::Context &context) : context_(context)
	{
		theme_ = month_colour();
		category_pos_.snap(static_cast<float>(category_));
	}

	const app::ConceptInfo &info() const override
	{
		static const app::ConceptInfo kInfo{
		    "home", "RPCS3 PS5", "The PS3 library and the emulator's home, XMB style",
		    "examples/rpcs3ps5/kit/home.cpp", audio::SoundSet::glass, Color::rgb(0x9ec8ff), kTechniques,
		};
		return kInfo;
	}

	void enter() override
	{
		age_ = 0.0f;
		close_options();
	}

	void update(const InputFrame &input, float dt, app::Feedback &feedback) override
	{
		age_ += dt;
		clock_ += dt;
		clamp_items();

		if (dialog_open_) {
			update_dialog(input, feedback);
		} else if (page_open_) {
			update_page(input, feedback);
		} else if (options_open_) {
			update_options(input, feedback);
		} else {
			update_bar(input, feedback);
			offer_new_games();
		}

		category_pos_.target = static_cast<float>(category_);
		category_pos_.update(dt, 14.0f);
		for (int c = 0; c < kCategoryCount; ++c) {
			item_pos_[c].target = static_cast<float>(item_[c]);
			item_pos_[c].update(dt, 16.0f);
		}
		options_.target = options_open_ ? 1.0f : 0.0f;
		options_.update(dt, context_.settings.reduced_motion ? 40.0f : 14.0f);
		option_pos_.target = static_cast<float>(option_);
		option_pos_.update(dt, 22.0f);
		nudge_.update(dt, 9.0f);
		page_pos_.target = static_cast<float>(page_sel_);
		page_pos_.update(dt, 20.0f);
	}

	void draw(app::Frame &frame) const override
	{
		// A dark gradient tinted with the month's colour; the waves are drawn on top
		frame.backdrop.mode = gfx::BackdropMode::gradient;
		frame.backdrop.colors[0] = gfx::mix(theme_, Color::rgb(0x000000), 0.72f);
		frame.backdrop.colors[1] = gfx::mix(theme_, Color::rgb(0x000000), 0.35f);
		frame.backdrop.colors[2] = gfx::mix(theme_, kWhite, 0.25f);
		frame.backdrop.params[0] = 0.5f;
		frame.backdrop.params[1] = 0.55f;
		frame.backdrop.params[2] = 0.35f;
		frame.backdrop.time = clock_;

		gfx::DrawList &list = frame.scene;
		draw_waves(list);
		list.push_opacity(tween::stagger(age_, 0, 0.05f, 0.5f));
		draw_clock(list);
		draw_items(list);
		draw_bar(list);
		draw_status(list);
		draw_hints(list);
		list.pop_opacity();
		if (options_.value > 0.01f)
			draw_options(frame.scene);
		if (page_open_)
			draw_page(frame.scene);
		if (dialog_open_)
			draw_dialog(frame.scene);
		if (page_open_ || dialog_open_)
			draw_hints(frame.scene);
	}

private:
	static const std::vector<rpcs3ps5::Game> &games()
	{
		return rpcs3ps5::library().games();
	}
	static int game_count()
	{
		return static_cast<int>(games().size());
	}

	// ---- the items of each category ----

	int item_count(int category) const
	{
		switch (category) {
		case kSettings: return kTabCount + (rpcs3ps5::settings_for_game() ? 2 : 1);
		case kGame: return std::max(game_count(), 1); // "No games" placeholder
		case kInstall: return 3;
		default: return 4;
		}
	}

	void clamp_items()
	{
		for (int c = 0; c < kCategoryCount; ++c)
			item_[c] = std::clamp(item_[c], 0, item_count(c) - 1);
	}

	struct Item
	{
		std::string label;
		std::string detail;
	};

	Item item(int category, int index) const
	{
		const rpcs3ps5::Library &lib = rpcs3ps5::library();
		switch (category) {
		case kSettings:
			if (index < kTabCount) {
				const int count = tab_setting_count(index);
				return {kTabs[index].title, std::to_string(count) + " settings" +
				                                (rpcs3ps5::settings_for_game() ? "  \xC2\xB7  for " + rpcs3ps5::settings_game_title() : "  \xC2\xB7  all games")};
			}
			if (!rpcs3ps5::settings_for_game())
				return {"Vibration", kVibration[rpcs3ps5::vibration_level() & 3] + std::string("  \xC2\xB7  how strongly the controller rumbles")};
			if (index == kTabCount)
				return {"Recommended Settings", "RPCS3's settings for this game, tuned for the PS5"};
			return {"Use the global settings", rpcs3ps5::core_settings_custom(rpcs3ps5::settings_game_id())
			                                       ? "This game has its own settings: Cross removes them"
			                                       : "This game uses the global settings"};
		case kGame:
			if (!game_count())
				return {"No games yet", "Copy PS3 games into " + rpcs3ps5::Library::root() + "/games/, then press Square"};
			{
				const rpcs3ps5::Game &game = games()[static_cast<std::size_t>(index)];
				return {game.title, game.title_id + (game.disc ? "  \xC2\xB7  disc" : "  \xC2\xB7  installed")};
			}
		case kInstall: {
			static const char *kLabels[] = {"Install PS3 System Software", "Install Package Files", "Install Licences (RAP)"};
			std::string detail;
			if (index == 0)
				detail = lib.firmware_installed()
				             ? "Installed: " + lib.firmware_version() + (lib.firmware_pup_present() ? "  \xC2\xB7  Cross reinstalls" : "")
				             : (lib.firmware_pup_present() ? std::string("PS3UPDAT.PUP found: Cross installs it")
				                                           : "Put PS3UPDAT.PUP in " + rpcs3ps5::Library::root());
			else if (index == 1)
				detail = "PKG files in " + rpcs3ps5::Library::root() + "/packages/";
			else
				detail = "RAP files in " + rpcs3ps5::Library::root() + "/exdata/";
			return {kLabels[index], detail};
		}
		default: {
			switch (index) {
			case 0: return {"System Software", lib.firmware_installed() ? "PS3 " + lib.firmware_version() : "Not installed"};
			case 1: return {"Games", std::to_string(lib.games().size()) + " found"};
			case 2: return {"Data Folder", rpcs3ps5::Library::root() + "  (FTP: /data/homebrew/PPSA99303/rpcs3)"};
			default: return {"Emulator", std::string("RPCS3  \xC2\xB7  ") + rpcs3ps5::core_status() + "  \xC2\xB7  your own console only (GPL-2.0)"};
			}
		}
		}
	}

	// ---- every setting: the cache, the settings page, the recommended settings dialog ----

	// The settings of the scope the Settings column shows (global, or the game whose settings are open)
	const std::vector<rpcs3ps5::Setting> &settings() const
	{
		const std::string scope = rpcs3ps5::settings_for_game() ? rpcs3ps5::settings_game_id() : std::string();
		if (!settings_valid_ || settings_scope_ != scope) {
			settings_ = rpcs3ps5::core_settings_all(scope);
			settings_scope_ = scope;
			settings_valid_ = true;
		}
		return settings_;
	}
	void invalidate_settings()
	{
		settings_valid_ = false;
	}
	static bool in_tab(const rpcs3ps5::Setting &setting, int tab)
	{
		for (const char *section : kTabs[tab].sections)
			if (section && setting.section == section)
				return true;
		return false;
	}
	int tab_setting_count(int tab) const
	{
		int count = 0;
		for (const auto &setting : settings())
			count += in_tab(setting, tab);
		return count;
	}
	int scope_game_index() const
	{
		for (int i = 0; i < game_count(); ++i)
			if (games()[static_cast<std::size_t>(i)].title_id == rpcs3ps5::settings_game_id())
				return i;
		return item_[kGame];
	}

	void open_page(int tab)
	{
		page_open_ = true;
		page_tab_ = tab;
		page_rows_.clear();
		const auto &all = settings();
		for (std::size_t i = 0; i < all.size(); ++i)
			if (in_tab(all[i], tab))
				page_rows_.push_back(i);
		page_sel_ = 0;
		page_pos_.snap(0.0f);
	}

	// The next (dir +1) or previous (-1) value of a setting
	static std::string step_value(const rpcs3ps5::Setting &setting, int dir)
	{
		switch (setting.kind) {
		case 'b':
			return setting.value == "true" ? "false" : "true";
		case 'e': {
			if (setting.choices.empty())
				return setting.value;
			const int n = static_cast<int>(setting.choices.size());
			int at = 0;
			for (int i = 0; i < n; ++i)
				if (setting.choices[static_cast<std::size_t>(i)] == setting.value)
					at = i;
			return setting.choices[static_cast<std::size_t>(((at + dir) % n + n) % n)];
		}
		case 'i': {
			if (setting.choices.size() != 2)
				return setting.value;
			const long long lo = std::strtoll(setting.choices[0].c_str(), nullptr, 10);
			const long long hi = std::strtoll(setting.choices[1].c_str(), nullptr, 10);
			const long long range = hi - lo;
			long long step = range <= 100 ? 1 : range <= 1000 ? 5 : range <= 10000 ? 50 : range / 100;
			if (setting.label.find("Resolution Scale") != std::string::npos)
				step = 25;
			const long long value = std::clamp(std::strtoll(setting.value.c_str(), nullptr, 10) + dir * step, lo, hi);
			return std::to_string(value);
		}
		case 'f': {
			if (setting.choices.size() != 2)
				return setting.value;
			const double lo = std::strtod(setting.choices[0].c_str(), nullptr), hi = std::strtod(setting.choices[1].c_str(), nullptr);
			const double step = (hi - lo) / 100.0;
			char text[32];
			std::snprintf(text, sizeof(text), "%g", std::clamp(std::strtod(setting.value.c_str(), nullptr) + dir * step, lo, hi));
			return text;
		}
		default:
			return setting.value;
		}
	}

	void change_setting(std::size_t index, const std::string &value, app::Feedback &feedback)
	{
		const rpcs3ps5::Setting setting = settings()[index];
		const std::string scope = rpcs3ps5::settings_for_game() ? rpcs3ps5::settings_game_id() : std::string();
		if (setting.locked || setting.kind == 's') {
			rpcs3ps5::set_status(setting.label + (setting.locked ? " is fixed on the PS5" : " is changed in the config file"));
			feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.5f);
			return;
		}
		if (rpcs3ps5::core_setting_set(scope, setting.path, value)) {
			invalidate_settings();
			rpcs3ps5::set_status(setting.label + ": " + value + (scope.empty() ? "  \xC2\xB7  all games" : "  \xC2\xB7  " + rpcs3ps5::settings_game_title()));
			feedback.play(audio::Cue::toggle);
		} else {
			rpcs3ps5::set_status(setting.label + ": " + value + " was not accepted");
			feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.5f);
		}
	}

	void update_page(const InputFrame &input, app::Feedback &feedback)
	{
		const int count = static_cast<int>(page_rows_.size());
		if (input.nav == Direction::up || input.nav == Direction::down || input.is_pressed(Action::page_prev) ||
		    input.is_pressed(Action::page_next)) {
			const int move = input.nav == Direction::up ? -1 : input.nav == Direction::down ? 1 : input.is_pressed(Action::page_prev) ? -10 : 10;
			const int next = std::clamp(page_sel_ + move, 0, std::max(count - 1, 0));
			if (next != page_sel_) {
				page_sel_ = next;
				feedback.play(audio::Cue::focus, 1.0f, 0.3f);
			}
		}
		if (count && (input.nav == Direction::left || input.nav == Direction::right || input.is_pressed(Action::confirm))) {
			const std::size_t index = page_rows_[static_cast<std::size_t>(page_sel_)];
			const int dir = input.nav == Direction::left ? -1 : 1;
			change_setting(index, step_value(settings()[index], dir), feedback);
		}
		if (count && input.is_pressed(Action::west)) {
			const std::size_t index = page_rows_[static_cast<std::size_t>(page_sel_)];
			change_setting(index, settings()[index].def, feedback);
		}
		if (input.is_pressed(Action::back)) {
			page_open_ = false;
			feedback.play(audio::Cue::modal_close);
		}
	}

	enum class Dialog
	{
		prompt,  // a game was added: use its recommended settings?
		details, // the changes, from the prompt
		manage,  // Triangle > Recommended Settings
	};

	void open_dialog(int game, Dialog mode)
	{
		if (game < 0 || game >= game_count())
			return;
		dialog_open_ = true;
		dialog_mode_ = mode;
		dialog_game_ = game;
		dialog_button_ = 0;
		dialog_scroll_ = 0;
		recommendation_ = rpcs3ps5::recommendation_for(games()[static_cast<std::size_t>(game)]);
	}

	std::vector<const char *> dialog_buttons() const
	{
		switch (dialog_mode_) {
		case Dialog::prompt: return {"Use recommended settings", "More details", "No thanks"};
		case Dialog::details: return {"Use these settings", "Back"};
		default: return {"Use recommended settings", "Use the global settings", "Close"};
		}
	}

	void update_dialog(const InputFrame &input, app::Feedback &feedback)
	{
		const int buttons = static_cast<int>(dialog_buttons().size());
		if (input.nav == Direction::left || input.nav == Direction::right) {
			const int next = std::clamp(dialog_button_ + (input.nav == Direction::right ? 1 : -1), 0, buttons - 1);
			if (next != dialog_button_) {
				dialog_button_ = next;
				feedback.play(audio::Cue::focus, 1.0f, 0.3f);
			}
		}
		if (input.nav == Direction::up)
			dialog_scroll_ = std::max(0, dialog_scroll_ - 1);
		if (input.nav == Direction::down)
			dialog_scroll_ = std::min(dialog_scroll_ + 1, std::max(0, static_cast<int>(recommendation_.changes.size()) - 1));
		const rpcs3ps5::Game &game = games()[static_cast<std::size_t>(dialog_game_)];
		const auto apply = [&] {
			const bool ok = rpcs3ps5::apply_recommendation(game);
			invalidate_settings();
			rpcs3ps5::set_status(game.title + (ok ? ": recommended settings in use" : ": recommended settings in use (some were not accepted)"));
			feedback.play(audio::Cue::launch);
			dialog_open_ = false;
		};
		if (input.is_pressed(Action::confirm)) {
			switch (dialog_mode_) {
			case Dialog::prompt:
				if (dialog_button_ == 0) {
					apply();
				} else if (dialog_button_ == 1) {
					dialog_mode_ = Dialog::details;
					dialog_button_ = 0;
					feedback.play(audio::Cue::select);
				} else {
					rpcs3ps5::mark_offered(game);
					rpcs3ps5::set_status(game.title + " keeps the global settings  \xC2\xB7  Triangle > Recommended Settings to use them later");
					feedback.play(audio::Cue::modal_close);
					dialog_open_ = false;
				}
				break;
			case Dialog::details:
				if (dialog_button_ == 0) {
					apply();
				} else {
					dialog_mode_ = Dialog::prompt;
					dialog_button_ = 1;
					feedback.play(audio::Cue::back);
				}
				break;
			case Dialog::manage:
				if (dialog_button_ == 0) {
					apply();
				} else if (dialog_button_ == 1) {
					rpcs3ps5::revert_to_global(game);
					invalidate_settings();
					rpcs3ps5::set_status(game.title + " uses the global settings again");
					feedback.play(audio::Cue::toggle);
					dialog_open_ = false;
				} else {
					feedback.play(audio::Cue::modal_close);
					dialog_open_ = false;
				}
				break;
			}
		}
		if (input.is_pressed(Action::back)) {
			if (dialog_mode_ == Dialog::details) {
				dialog_mode_ = Dialog::prompt;
				dialog_button_ = 1;
			} else {
				if (dialog_mode_ == Dialog::prompt)
					rpcs3ps5::mark_offered(game);
				dialog_open_ = false;
			}
			feedback.play(audio::Cue::modal_close);
		}
	}

	// A game the library lists that was never offered its recommended settings: offered now, one at a time
	void offer_new_games()
	{
		if (dialog_open_ || page_open_ || options_open_ || age_ < 1.0f || rpcs3ps5::tiles_busy())
			return;
		if (game_count() != offered_scan_count_) {
			offered_scan_count_ = game_count();
			pending_offers_ = rpcs3ps5::unoffered_games(games());
		}
		if (pending_offers_.empty())
			return;
		const std::size_t game = pending_offers_.front();
		pending_offers_.erase(pending_offers_.begin());
		if (game < games().size())
			open_dialog(static_cast<int>(game), Dialog::prompt);
	}

	// ---- input ----

	void refuse(app::Feedback &feedback, float direction)
	{
		feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
		nudge_direction_ = direction;
		nudge_.trigger();
	}

	void update_bar(const InputFrame &input, app::Feedback &feedback)
	{
		if (input.nav == Direction::left || input.nav == Direction::right ||
		    input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next)) {
			const bool right = input.nav == Direction::right || input.is_pressed(Action::page_next);
			const int next = category_ + (right ? 1 : -1);
			if (next >= 0 && next < kCategoryCount) {
				// Leaving a game's own settings goes back to the global ones
				if (category_ == kSettings && rpcs3ps5::settings_for_game())
					rpcs3ps5::settings_open("", "");
				category_ = next;
				feedback.play(audio::Cue::tab);
			} else if (!input.nav_repeat) {
				refuse(feedback, right ? 1.0f : -1.0f);
			}
		}
		if (input.nav == Direction::up || input.nav == Direction::down) {
			const int next = item_[category_] + (input.nav == Direction::down ? 1 : -1);
			if (next >= 0 && next < item_count(category_)) {
				item_[category_] = next;
				feedback.play(audio::Cue::focus);
			} else if (!input.nav_repeat) {
				feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.4f);
			}
		}
		if (input.is_pressed(Action::west)) {
			rpcs3ps5::request(rpcs3ps5::Request::rescan);
			feedback.play(audio::Cue::select);
		}
		if (input.is_pressed(Action::back) && category_ == kSettings && rpcs3ps5::settings_for_game()) {
			// A game's settings: Circle goes back to the game
			rpcs3ps5::settings_open("", "");
			category_ = kGame;
			feedback.play(audio::Cue::back);
		}
		if (input.is_pressed(Action::north) && category_ == kGame && game_count()) {
			options_open_ = true;
			option_ = 0;
			option_pos_.snap(0.0f);
			feedback.play(audio::Cue::modal_open);
		}
		if (input.is_pressed(Action::confirm)) {
			const int index = item_[category_];
			switch (category_) {
			case kGame:
				if (game_count()) {
					feedback.play(audio::Cue::launch);
					feedback.rumble(0.5f, 0.15f);
					rpcs3ps5::request(rpcs3ps5::Request::boot, index);
				} else {
					refuse(feedback, 0.0f);
				}
				break;
			case kInstall: {
				static constexpr rpcs3ps5::Request kInstall[] = {rpcs3ps5::Request::install_firmware,
				                                                 rpcs3ps5::Request::install_packages,
				                                                 rpcs3ps5::Request::install_licenses};
				feedback.play(audio::Cue::select);
				rpcs3ps5::request(kInstall[index]);
				break;
			}
			case kSettings:
				if (index < kTabCount) {
					open_page(index);
					feedback.play(audio::Cue::modal_open);
				} else if (!rpcs3ps5::settings_for_game()) {
					rpcs3ps5::set_vibration_level((rpcs3ps5::vibration_level() + 1) % 4);
					rpcs3ps5::set_status(std::string("Vibration: ") + kVibration[rpcs3ps5::vibration_level() & 3]);
					feedback.play(audio::Cue::toggle);
				} else if (index == kTabCount) {
					open_dialog(scope_game_index(), Dialog::manage);
					feedback.play(audio::Cue::modal_open);
				} else {
					rpcs3ps5::core_settings_clear(rpcs3ps5::settings_game_id());
					invalidate_settings();
					rpcs3ps5::set_status(rpcs3ps5::settings_game_title() + " uses the global settings");
					feedback.play(audio::Cue::toggle);
				}
				break;
			default:
				break;
			}
		}
	}

	// The options panel's rows: a game's options, or the delete confirmation
	enum Option
	{
		kStart,
		kGameSettings,
		kRecommended,
		kTile,
		kDelete,
		kClose,
		kDeleteKeepSaves,
		kDeleteWithSaves,
		kCancel,
	};

	std::vector<Option> option_rows() const
	{
		if (confirm_delete_)
			return {kDeleteKeepSaves, kDeleteWithSaves, kCancel};
		return {kStart, kGameSettings, kRecommended, kTile, kDelete, kClose};
	}

	const char *option_label(Option option) const
	{
		switch (option) {
		case kStart: return "Start";
		case kGameSettings: return "Game Settings";
		case kRecommended: return "Recommended Settings";
		case kTile: return focused_tile_present() ? "Remove from PS5 Home Screen" : "Add to PS5 Home Screen";
		case kDelete: return "Delete Game";
		case kDeleteKeepSaves: return "Delete, keep save data";
		case kDeleteWithSaves: return "Delete, with save data";
		case kCancel: return "Cancel";
		default: return "Close";
		}
	}

	bool focused_tile_present() const
	{
		return game_count() && rpcs3ps5::tile_present(games()[static_cast<std::size_t>(item_[kGame])]);
	}

	void update_options(const InputFrame &input, app::Feedback &feedback)
	{
		const std::vector<Option> rows = option_rows();
		const int count = static_cast<int>(rows.size());
		if (input.nav == Direction::up || input.nav == Direction::down) {
			const int next = std::clamp(option_ + (input.nav == Direction::down ? 1 : -1), 0, count - 1);
			if (next != option_) {
				option_ = next;
				feedback.play(audio::Cue::focus, 1.0f, 0.35f);
			}
		}
		if (input.is_pressed(Action::confirm)) {
			const int game = item_[kGame];
			const rpcs3ps5::Game &g = games()[static_cast<std::size_t>(game)];
			switch (rows[static_cast<std::size_t>(std::clamp(option_, 0, count - 1))]) {
			case kStart:
				feedback.play(audio::Cue::launch);
				rpcs3ps5::request(rpcs3ps5::Request::boot, game);
				break;
			case kGameSettings:
				// This game's own settings, in the Settings column
				feedback.play(audio::Cue::select);
				rpcs3ps5::settings_open(g.title_id, g.title);
				category_ = kSettings;
				item_[kSettings] = 0;
				break;
			case kRecommended:
				feedback.play(audio::Cue::modal_open);
				close_options();
				open_dialog(game, Dialog::manage);
				return;
			case kTile:
				feedback.play(audio::Cue::select);
				rpcs3ps5::request(focused_tile_present() ? rpcs3ps5::Request::tile_remove : rpcs3ps5::Request::tile_add, game);
				break;
			case kDelete:
				// Asks first, in the same panel
				feedback.play(audio::Cue::modal_open);
				confirm_delete_ = true;
				option_ = 2; // Cancel
				option_pos_.snap(2.0f);
				return;
			case kDeleteKeepSaves:
			case kDeleteWithSaves:
				feedback.play(audio::Cue::select);
				rpcs3ps5::request(rows[static_cast<std::size_t>(option_)] == kDeleteWithSaves ? rpcs3ps5::Request::delete_game_and_saves
				                                                                              : rpcs3ps5::Request::delete_game,
				                  game);
				break;
			default:
				feedback.play(audio::Cue::modal_close);
				break;
			}
			close_options();
		}
		if (input.is_pressed(Action::back) || input.is_pressed(Action::north)) {
			feedback.play(audio::Cue::modal_close);
			close_options();
		}
	}

	void close_options()
	{
		options_open_ = false;
		confirm_delete_ = false;
	}

	// ---- drawing ----

	// Translucent ribbons drifting across the middle of the screen, as on the PS3
	void draw_waves(gfx::DrawList &list) const
	{
		const float t = context_.settings.reduced_motion ? 0.0f : clock_;
		constexpr int kSteps = 96;
		const float w = static_cast<float>(gfx::kVirtualWidth);
		struct Ribbon
		{
			float y, amp, freq, speed, phase, thick, alpha;
		};
		static const Ribbon kRibbons[] = {
		    {600, 70, 1.6f, 0.11f, 0.0f, 120, 0.07f},
		    {620, 55, 2.1f, -0.08f, 1.9f, 70, 0.09f},
		    {585, 45, 2.7f, 0.14f, 3.4f, 34, 0.12f},
		};
		float xy[(kSteps + 1) * 4];
		for (const Ribbon &r : kRibbons) {
			const auto curve = [&](float x, float offset) {
				const float u = x / w;
				return r.y + offset + r.amp * std::sin(u * 6.2832f * r.freq * 0.5f + t * r.speed + r.phase) +
				       r.amp * 0.35f * std::sin(u * 6.2832f * r.freq + t * r.speed * 1.7f + r.phase * 0.5f);
			};
			// Thickness swells along the ribbon, so it twists like the XMB wave
			for (int i = 0; i <= kSteps; ++i) {
				const float x = w * static_cast<float>(i) / kSteps;
				const float swell = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(x / w * 5.0f + t * r.speed * 2.3f + r.phase));
				xy[i * 2] = x;
				xy[i * 2 + 1] = curve(x, -r.thick * 0.5f * swell);
				const int j = (kSteps + 1) * 2 - 1 - i;
				xy[j * 2] = x;
				xy[j * 2 + 1] = curve(x, r.thick * 0.5f * swell);
			}
			list.polygon(xy, (kSteps + 1) * 2, kWhite.with_alpha(r.alpha));
			// A bright edge along the top of each ribbon
			for (int i = 0; i < kSteps; ++i)
				list.line(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], 1.5f, kWhite.with_alpha(r.alpha * 2.2f));
		}
	}

	void draw_clock(gfx::DrawList &list) const
	{
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_r(&now, &local);
		char text[32];
		std::snprintf(text, sizeof(text), "%d/%d  %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_hour, local.tm_min);
		ui::text(list, context_.fonts.regular, text, gfx::kVirtualWidth - 110, 104, 30, kWhite.with_alpha(0.92f), gfx::Align::right);
		list.line(gfx::kVirtualWidth - 420, 122, gfx::kVirtualWidth - 110, 122, 1.5f, kWhite.with_alpha(0.35f));
	}

	// The category icons, drawn from simple shapes
	static void draw_category_icon(gfx::DrawList &list, int category, float cx, float cy, float s, Color ink)
	{
		switch (category) {
		case kSettings: { // a toolbox: lid, box, handle
			list.bordered_rect({cx - s * 0.5f, cy - s * 0.15f, s, s * 0.55f}, s * 0.06f, ink.with_alpha(0.0f), s * 0.07f, ink);
			list.rounded_rect({cx - s * 0.5f, cy - s * 0.15f, s, s * 0.12f}, s * 0.04f, ink);
			list.bordered_rect({cx - s * 0.18f, cy - s * 0.38f, s * 0.36f, s * 0.26f}, s * 0.06f, ink.with_alpha(0.0f), s * 0.07f, ink);
			list.rounded_rect({cx - s * 0.06f, cy + s * 0.02f, s * 0.12f, s * 0.12f}, s * 0.02f, ink);
			break;
		}
		case kGame: { // a pad: body, grips, d-pad, buttons
			list.rounded_rect({cx - s * 0.5f, cy - s * 0.22f, s, s * 0.42f}, s * 0.2f, ink);
			list.circle(cx - s * 0.36f, cy + s * 0.16f, s * 0.17f, ink);
			list.circle(cx + s * 0.36f, cy + s * 0.16f, s * 0.17f, ink);
			const Color hole = Color::rgb(0x000000, 0.55f);
			list.rounded_rect({cx - s * 0.33f, cy - s * 0.04f, s * 0.2f, s * 0.06f}, 1, hole);
			list.rounded_rect({cx - s * 0.26f, cy - s * 0.11f, s * 0.06f, s * 0.2f}, 1, hole);
			list.circle(cx + s * 0.22f, cy - s * 0.06f, s * 0.04f, hole);
			list.circle(cx + s * 0.3f, cy + s * 0.02f, s * 0.04f, hole);
			break;
		}
		case kInstall: { // an arrow into a tray
			list.line(cx, cy - s * 0.42f, cx, cy + s * 0.08f, s * 0.1f, ink);
			const float tri[] = {cx - s * 0.2f, cy, cx + s * 0.2f, cy, cx, cy + s * 0.22f};
			list.polygon(tri, 3, ink);
			list.line(cx - s * 0.45f, cy + s * 0.08f, cx - s * 0.45f, cy + s * 0.36f, s * 0.08f, ink);
			list.line(cx + s * 0.45f, cy + s * 0.08f, cx + s * 0.45f, cy + s * 0.36f, s * 0.08f, ink);
			list.line(cx - s * 0.45f, cy + s * 0.36f, cx + s * 0.45f, cy + s * 0.36f, s * 0.08f, ink);
			break;
		}
		default: { // a console: a slim box with a disc slot
			list.bordered_rect({cx - s * 0.5f, cy - s * 0.2f, s, s * 0.4f}, s * 0.08f, ink.with_alpha(0.0f), s * 0.07f, ink);
			list.line(cx - s * 0.3f, cy, cx + s * 0.1f, cy, s * 0.05f, ink);
			list.circle(cx + s * 0.32f, cy, s * 0.04f, ink);
			break;
		}
		}
	}

	float category_x(int category) const
	{
		float x = kBarX + (static_cast<float>(category) - category_pos_.value) * kCategoryGap;
		// The focused category keeps clear space to its right for its label
		if (static_cast<float>(category) > category_pos_.value)
			x += 60.0f * std::min(1.0f, static_cast<float>(category) - category_pos_.value);
		return x;
	}

	void draw_bar(gfx::DrawList &list) const
	{
		const ui::Fonts &fonts = context_.fonts;
		const float shake = ui::shake(nudge_.value, clock_, 14.0f, 6.0f) * nudge_direction_;
		for (int c = 0; c < kCategoryCount; ++c) {
			const float x = category_x(c) + shake;
			const float focus = std::max(0.0f, 1.0f - std::fabs(static_cast<float>(c) - category_pos_.value));
			const float size = 70.0f + 26.0f * focus;
			if (focus > 0.2f)
				list.glow({x - 50, kBarY - 50, 100, 100}, 50, 40, kWhite.with_alpha(0.18f * focus));
			draw_category_icon(list, c, x, kBarY, size, kWhite.with_alpha(0.5f + 0.5f * focus));
			if (focus > 0.05f)
				ui::text(list, fonts.regular, kCategoryNames[c], x, kBarY + 84, 26, kWhite.with_alpha(focus),
				         gfx::Align::center);
		}
	}

	void draw_item_icon(gfx::DrawList &list, int category, int index, const Rect &box, float focus) const
	{
		if (category == kGame && game_count()) {
			const rpcs3ps5::Game &game = games()[static_cast<std::size_t>(index)];
			if (focus > 0.3f)
				list.glow(box.inset(6), 8, 26, Color::rgb(game.accent, 0.35f * focus));
			if (game.icon)
				list.image(game.icon, box, gfx::kFullUv, kWhite, 6);
			else
				list.rounded_rect(box, 6, Color::rgb(game.mid));
			return;
		}
		// Other items: a small round mark in the category's style
		const float cx = box.cx(), cy = box.cy();
		list.ring(cx, cy, 20 + 6 * focus, 3, kWhite.with_alpha(0.55f + 0.45f * focus));
		list.circle(cx, cy, 7 + 3 * focus, kWhite.with_alpha(0.55f + 0.45f * focus));
	}

	void draw_items(gfx::DrawList &list) const
	{
		const ui::Fonts &fonts = context_.fonts;
		// The focused category's column, and the neighbours' fading out as the bar slides
		for (int c = 0; c < kCategoryCount; ++c) {
			const float focus_c = std::max(0.0f, 1.0f - std::fabs(static_cast<float>(c) - category_pos_.value) * 1.6f);
			if (focus_c <= 0.01f)
				continue;
			const bool games_column = c == kGame && game_count();
			const float pitch = games_column ? kGameItemPitch : kItemPitch;
			const float x = category_x(c);
			const int count = item_count(c);
			list.push_opacity(focus_c);
			if (c == kSettings && rpcs3ps5::settings_for_game()) {
				const std::string heading = "Settings for " + rpcs3ps5::settings_game_title() + "  \xC2\xB7  Circle: back";
				ui::text(list, fonts.semibold, heading.c_str(), x - 40, kBarY + 130, 22, kWhite.with_alpha(0.8f));
			}
			for (int i = 0; i < count; ++i) {
				const float rel = static_cast<float>(i) - item_pos_[c].value;
				const float y = item_y(rel, pitch);
				if (y < -100 || y > gfx::kVirtualHeight + 100)
					continue;
				const float focus = std::max(0.0f, 1.0f - std::fabs(rel));
				// Brightness follows the scroll too: full at the focus, dimmer below, dimmest above the bar
				const float fade = rel < 0.0f ? 1.0f + (0.45f - 1.0f) * std::min(1.0f, -rel)
				                              : 1.0f + (0.62f - 1.0f) * std::min(1.0f, rel);
				list.push_opacity(fade);
				const Item it = item(c, i);
				if (games_column) {
					const float w = 192.0f + 64.0f * focus, h = w * 176.0f / 320.0f;
					draw_item_icon(list, c, i, {x - w * 0.5f, y - h * 0.5f, w, h}, focus);
					ui::text(list, fonts.semibold, it.label.c_str(), x + 160 + 30 * focus, y + 4, 30 + 4 * focus, kWhite);
					if (focus > 0.5f)
						ui::text(list, fonts.regular, it.detail.c_str(), x + 190, y + 44, 21, kWhite.with_alpha(0.7f * focus));
				} else {
					draw_item_icon(list, c, i, {x - 40, y - 40, 80, 80}, focus);
					ui::text(list, fonts.semibold, it.label.c_str(), x + 80, y + 2, 28 + 2 * focus, kWhite);
					ui::text(list, fonts.regular, it.detail.c_str(), x + 80, y + 36, 21, kWhite.with_alpha(0.55f + 0.25f * focus));
				}
				list.pop_opacity();
			}
			list.pop_opacity();
		}
	}

	// Where an item sits, `rel` items from the scroll position (fractional while it moves). Items after the
	// focus go down the column; the one before it rises above the category bar, as on the PS3. The way up is
	// one smooth curve, so an item glides past the bar instead of jumping to its row above (2026-10-07).
	static float item_y(float rel, float pitch)
	{
		constexpr float kAboveY = kBarY - 150.0f; // the nearest item above the bar
		if (rel >= 0.0f)
			return kFirstItemY + rel * pitch;
		if (rel >= -1.0f) {
			// cubic Hermite from (rel -1, kAboveY) to (rel 0, kFirstItemY), its slopes those of the straight
			// parts on either side: no jump and no change of speed as an item crosses over
			const float t = rel + 1.0f, t2 = t * t, t3 = t2 * t;
			const float m0 = pitch * 0.8f, m1 = pitch;
			return (2 * t3 - 3 * t2 + 1) * kAboveY + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * kFirstItemY + (t3 - t2) * m1;
		}
		return kAboveY + (rel + 1.0f) * pitch * 0.8f;
	}

	void draw_options(gfx::DrawList &list) const
	{
		// The PS3's options panel: a dark strip sliding in from the right
		const ui::Fonts &fonts = context_.fonts;
		const float t = options_.value;
		const float w = 580.0f;
		const float x = gfx::kVirtualWidth - w * tween::quint_out(t);
		list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0, Color::rgb(0x000000, 0.25f * t));
		list.gradient_rect_h({x, 0, w, gfx::kVirtualHeight}, 0, Color::rgb(0x000000, 0.55f * t), Color::rgb(0x000000, 0.85f * t));
		list.line(x, 0, x, gfx::kVirtualHeight, 1.5f, kWhite.with_alpha(0.35f * t));
		const std::vector<Option> rows = option_rows();
		const float y0 = confirm_delete_ ? 520.0f : 380.0f;
		list.push_opacity(t);
		if (confirm_delete_ && game_count()) {
			const std::string question = "Delete " + games()[static_cast<std::size_t>(item_[kGame])].title + "?";
			ui::text(list, fonts.semibold, question.c_str(), x + 40, 300, 30, kWhite);
			static const char *kNote[] = {"Removes the game, its installed data,", "caches and settings, and its PS5",
			                              "home screen tile. This can't be undone."};
			for (int i = 0; i < 3; ++i)
				ui::text(list, fonts.regular, kNote[i], x + 40, 350 + 32.0f * i, 22, kWhite.with_alpha(0.7f));
		}
		list.rounded_rect({x + 30, y0 - 42 + option_pos_.value * 80, w - 60, 62}, 10, kWhite.with_alpha(0.16f));
		for (std::size_t i = 0; i < rows.size(); ++i)
			ui::text(list, fonts.regular, option_label(rows[i]), x + 64, y0 + static_cast<float>(i) * 80, 28,
			         kWhite.with_alpha(static_cast<int>(i) == option_ ? 1.0f : 0.7f));
		list.pop_opacity();
	}

	// A settings tab: every setting of its sections, the value on the right, the selected one's details below
	void draw_page(gfx::DrawList &list) const
	{
		const ui::Fonts &fonts = context_.fonts;
		const float W = gfx::kVirtualWidth, H = gfx::kVirtualHeight;
		list.rounded_rect({0, 0, W, H}, 0, Color::rgb(0x05060a, 0.88f));
		const std::string scope = rpcs3ps5::settings_for_game() ? rpcs3ps5::settings_game_title() : std::string("all games");
		ui::text(list, fonts.semibold, (std::string(kTabs[page_tab_].title) + " settings").c_str(), 140, 120, 44, kWhite);
		ui::text(list, fonts.regular, ("For " + scope + "  \xC2\xB7  " + std::to_string(page_rows_.size()) + " settings").c_str(), 140, 168, 24,
		         kWhite.with_alpha(0.65f));
		list.line(140, 196, W - 140, 196, 1.5f, kWhite.with_alpha(0.3f));
		const auto &all = settings();
		constexpr float kTop = 250.0f, kPitch = 56.0f, kBottom = 860.0f;
		const float scroll = std::max(0.0f, page_pos_.value - 5.0f);
		for (std::size_t r = 0; r < page_rows_.size(); ++r) {
			const float y = kTop + (static_cast<float>(r) - scroll) * kPitch;
			if (y < kTop - kPitch * 0.5f || y > kBottom)
				continue;
			const rpcs3ps5::Setting &setting = all[page_rows_[r]];
			const bool selected = static_cast<int>(r) == page_sel_;
			if (selected)
				list.rounded_rect({120, y - 36, W - 240, 52}, 10, kWhite.with_alpha(0.14f));
			const float alpha = setting.locked ? 0.4f : (selected ? 1.0f : 0.78f);
			ui::text(list, fonts.regular, setting.label.c_str(), 150, y, 26, kWhite.with_alpha(alpha));
			std::string value = setting.kind == 'b' ? (setting.value == "true" ? "On" : "Off") : setting.value;
			if (setting.locked)
				value += "  (fixed on PS5)";
			if (selected && !setting.locked && setting.kind != 's')
				value = "\xE2\x97\x80  " + value + "  \xE2\x96\xB6";
			ui::text(list, fonts.semibold, value.c_str(), W - 150, y, 26, kWhite.with_alpha(alpha), gfx::Align::right);
		}
		if (!page_rows_.empty()) {
			const rpcs3ps5::Setting &setting = all[page_rows_[static_cast<std::size_t>(page_sel_)]];
			list.line(140, 890, W - 140, 890, 1.5f, kWhite.with_alpha(0.3f));
			std::string info = "Default: " + (setting.kind == 'b' ? std::string(setting.def == "true" ? "On" : "Off") : setting.def);
			if (setting.kind == 'i' || setting.kind == 'f') {
				if (setting.choices.size() == 2)
					info += "  \xC2\xB7  range " + setting.choices[0] + " to " + setting.choices[1];
			} else if (setting.kind == 'e') {
				info += "  \xC2\xB7  " + std::to_string(setting.choices.size()) + " choices";
			}
			ui::text(list, fonts.regular, info.c_str(), 150, 935, 24, kWhite.with_alpha(0.7f));
		}
	}

	// The recommended settings dialog: a new game's offer, its details, or Triangle > Recommended Settings
	void draw_dialog(gfx::DrawList &list) const
	{
		const ui::Fonts &fonts = context_.fonts;
		const float W = gfx::kVirtualWidth, H = gfx::kVirtualHeight;
		list.rounded_rect({0, 0, W, H}, 0, Color::rgb(0x000000, 0.6f));
		const Rect box{260, 130, W - 520, H - 260};
		list.rounded_rect(box, 22, Color::rgb(0x12161f, 0.97f));
		list.line(box.x + 40, box.y + 150, box.x + box.w - 40, box.y + 150, 1.5f, kWhite.with_alpha(0.2f));
		const rpcs3ps5::Game &game = games()[static_cast<std::size_t>(dialog_game_)];
		const rpcs3ps5::Recommendation &rec = recommendation_;
		const float x = box.x + 60;
		std::string title, subtitle;
		if (dialog_mode_ == Dialog::prompt) {
			title = game.title + " added";
			subtitle = "Use recommended settings for it?";
		} else {
			title = "Recommended settings: " + game.title;
			subtitle = rec.in_database ? "From RPCS3's settings for this game, plus the PS5's own" : "RPCS3 has no special settings for this game: the PS5's own only";
		}
		ui::text(list, fonts.semibold, title.c_str(), x, box.y + 80, 40, kWhite);
		ui::text(list, fonts.regular, subtitle.c_str(), x, box.y + 126, 26, kWhite.with_alpha(0.75f));

		float y = box.y + 210;
		if (dialog_mode_ == Dialog::prompt) {
			std::vector<std::string> lines;
			if (rec.changes.empty()) {
				lines.push_back("It already runs with the recommended settings: nothing to change.");
			} else {
				lines.push_back(std::to_string(rec.changes.size()) + " setting(s) change for this game only" +
				                (rec.in_database ? ", as RPCS3 recommends for it on PC," : "") + " tuned for the best performance on the PS5.");
				lines.push_back("Your other games keep their settings. You can switch back at any time: Triangle > Recommended Settings.");
				lines.push_back("More details lists every change and what it does.");
			}
			for (const std::string &text : lines)
				for (const std::string &line : wrap(text, 78)) {
					ui::text(list, fonts.regular, line.c_str(), x, y, 28, kWhite.with_alpha(0.9f));
					y += 42;
				}
		} else {
			if (rec.changes.empty())
				ui::text(list, fonts.regular, "Nothing to change: the game already uses these settings.", x, y, 28, kWhite.with_alpha(0.9f));
			const float bottom = box.y + box.h - 140;
			for (std::size_t i = static_cast<std::size_t>(dialog_scroll_); i < rec.changes.size() && y < bottom; ++i) {
				const rpcs3ps5::SettingChange &change = rec.changes[i];
				const std::string what = change.label + ":  " + (change.from.empty() ? "" : change.from + "  \xE2\x86\x92  ") + change.to +
				                         (change.fixed ? "  (kept as the PS5 needs)" : "");
				ui::text(list, fonts.semibold, what.c_str(), x, y, 26, kWhite.with_alpha(change.fixed ? 0.5f : 1.0f));
				y += 36;
				for (const std::string &line : wrap(change.why, 90)) {
					ui::text(list, fonts.regular, line.c_str(), x + 24, y, 22, kWhite.with_alpha(0.65f));
					y += 30;
				}
				y += 14;
			}
			if (rec.changes.size() > 3)
				ui::text(list, fonts.regular, "Up / Down: more", box.x + box.w - 60, box.y + 126, 22, kWhite.with_alpha(0.5f), gfx::Align::right);
		}

		const std::vector<const char *> buttons = dialog_buttons();
		float bx = x;
		const float by = box.y + box.h - 90;
		for (std::size_t i = 0; i < buttons.size(); ++i) {
			const float w = 30.0f + 15.0f * static_cast<float>(std::strlen(buttons[i]));
			const bool focused = static_cast<int>(i) == dialog_button_;
			list.rounded_rect({bx, by - 40, w, 60}, 30, focused ? kWhite.with_alpha(0.9f) : kWhite.with_alpha(0.12f));
			ui::text(list, fonts.semibold, buttons[i], bx + w * 0.5f, by, 26, focused ? Color::rgb(0x10131a) : kWhite, gfx::Align::center);
			bx += w + 24;
		}
	}

	void draw_status(gfx::DrawList &list) const
	{
		const std::string status = rpcs3ps5::status_text();
		if (status.empty())
			return;
		ui::text(list, context_.fonts.regular, status.c_str(), 110, 1010, 22, kWhite.with_alpha(0.75f));
	}

	void draw_hints(gfx::DrawList &list) const
	{
		const ui::Fonts &fonts = context_.fonts;
		const ui::GlyphStyle style = ui::GlyphStyle::dark();
		if (dialog_open_) {
			const ui::Hint hints[] = {{ui::Button::cross, "Choose"}, {ui::Button::circle, "Back"}};
			ui::draw_hints(list, fonts, style, hints, 2, 1824, true);
			return;
		}
		if (page_open_) {
			const ui::Hint hints[] = {{ui::Button::cross, "Change"}, {ui::Button::square, "Default"}, {ui::Button::circle, "Back"}};
			ui::draw_hints(list, fonts, style, hints, 3, 1824, true);
			return;
		}
		if (options_open_) {
			const ui::Hint hints[] = {{ui::Button::cross, "Choose"}, {ui::Button::circle, "Back"}};
			ui::draw_hints(list, fonts, style, hints, 2, 1824, true);
			return;
		}
		if (category_ == kGame && game_count()) {
			const ui::Hint hints[] = {{ui::Button::cross, "Start"}, {ui::Button::triangle, "Options"}, {ui::Button::square, "Rescan"}};
			ui::draw_hints(list, fonts, style, hints, 3, 1824, true);
		} else {
			const ui::Hint hints[] = {{ui::Button::cross, "Enter"}, {ui::Button::square, "Rescan"}};
			ui::draw_hints(list, fonts, style, hints, 2, 1824, true);
		}
	}

	app::Context &context_;
	Color theme_;
	int category_ = kGame;
	int item_[kCategoryCount] = {};
	float age_ = 0.0f;
	float clock_ = 0.0f;
	tween::Spring category_pos_;
	tween::Spring item_pos_[kCategoryCount];
	ui::Pulse nudge_;
	float nudge_direction_ = 0.0f;
	bool options_open_ = false;
	bool confirm_delete_ = false;
	mutable std::vector<rpcs3ps5::Setting> settings_;
	mutable std::string settings_scope_;
	mutable bool settings_valid_ = false;
	bool page_open_ = false;
	int page_tab_ = 0;
	std::vector<std::size_t> page_rows_;
	int page_sel_ = 0;
	tween::Spring page_pos_;
	bool dialog_open_ = false;
	Dialog dialog_mode_ = Dialog::prompt;
	int dialog_game_ = 0;
	int dialog_button_ = 0;
	int dialog_scroll_ = 0;
	rpcs3ps5::Recommendation recommendation_;
	int offered_scan_count_ = -1;
	std::vector<std::size_t> pending_offers_;
	tween::Spring options_;
	int option_ = 0;
	tween::Spring option_pos_;
};

} // namespace

} // namespace hui::screen

// The title's screen, for its program (ps5/ui/kit.hpp declares it)
namespace ps5ui
{
std::unique_ptr<hui::app::Concept> make_screen(hui::app::Context &context)
{
	return std::make_unique<hui::screen::Home>(context);
}
} // namespace ps5ui
