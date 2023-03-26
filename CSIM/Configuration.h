#pragma once

class Configuration
{
public:
	static bool dropdash_enabled;
	static bool dropdash_on_x;
	static bool peelout_enabled;
	static float peelout_speed;
	static float peelout_display_speed;
	static bool roll_momentum;
	//static bool OG_spindash;
	static std::string OG_spindash; // Changed to have more than 1 option.
	static bool OG_crush_death;
	//static bool jumpball_flash;
	static std::string cs_model;
	//static std::string little_tweaks;
	static std::string sound_style;
	static std::string physics_tweaks;

    static bool load(const std::string& filePath);
};