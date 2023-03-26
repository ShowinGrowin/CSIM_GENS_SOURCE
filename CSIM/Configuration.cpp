#include "Configuration.h"
//#include <fstream>

bool Configuration::dropdash_enabled = true;
bool Configuration::dropdash_on_x = false;
bool Configuration::peelout_enabled = true;
float Configuration::peelout_speed = 43.0f;
float Configuration::peelout_display_speed = 40.0f;
bool Configuration::roll_momentum = true;
//bool Configuration::OG_spindash = false;
std::string Configuration::OG_spindash = "spindash_csim";
bool Configuration::OG_crush_death = false;
//bool Configuration::jumpball_flash = false;
//int Configuration::cs_model = 0;
std::string Configuration::cs_model = "graphics_null";
//std::string Configuration::little_tweaks = "graphics_null";
std::string Configuration::sound_style = "sound_original";
std::string Configuration::physics_tweaks = "physics_tweaks";

bool Configuration::load(const std::string& filePath)
{
    const INIReader reader(filePath);
    if (reader.ParseError() != 0)
        return false;

    physics_tweaks = reader.Get("Main", "IncludeDir4", "physics_tweaks");

    // Gameplay Options
    dropdash_enabled = reader.GetBoolean("Gameplay", "dropdash_enabled", true);
    dropdash_on_x = reader.GetBoolean("Gameplay", "dropdash_on_x", false);
    peelout_enabled = reader.GetBoolean("Gameplay", "peelout_enabled", true);
    peelout_speed = reader.GetFloat("Gameplay", "peelout_speed", 43.0f);
    peelout_display_speed = reader.GetFloat("Gameplay", "peelout_speed", 40.0f);
    //roll_momentum = reader.GetBoolean("Gameplay", "roll_momentum", true);
	//OG_spindash = reader.GetBoolean("Gameplay", "OG_spindash", false);
    //OG_spindash = reader.Get("Gameplay", "OG_spindash", "spindash_csim");
    OG_crush_death = reader.GetBoolean("Gameplay", "OG_crush_death", false);

    // Graphical Options
    //jumpball_flash = reader.GetBoolean("Main", "jumpball_flash", false);
    cs_model = reader.Get("Main", "IncludeDir1", "graphics_marza");
   // little_tweaks = reader.Get("Main", "IncludeDir2", "graphics_little_touches");
    //sound_style = reader.Get("Main", "IncludeDir3", "sound_classic");
    sound_style = reader.Get("Main", "IncludeDir0", "sound_classic");
    

    // If physics tweaks are off then force these settings.
    if (physics_tweaks == "physics_tweaks_none")
    {
        roll_momentum = false;
        OG_spindash = "spindash_original";
        
        // Change sound settings since those include physics files. Only real conveient way to do this for now. The game will have to be restarted for this fix to technically do the trick.
        // IF ANYTHING IS CHANED ABOUT THE FILE ABOVE THIS SETTING IT WILL MAKE THIS SOLUTION BROKEN!
        // ITS NOT EVEN A GOOD FIX ANYWAYS SO BETTER FIND SOMETHING ELSE!
        // BUT FOR NOW I JUST MADE SOUND STUFF USE IncludeDir0 INSTEAD OF CORE WHICH TECHNICALLY FIXES THE ISSUE SINCE STUFF ABOVE WOULD HAVE TO BE CHANGED MANUALLY!
        std::fstream change(filePath);
        change.seekp(422, std::ios_base::beg); // MUST BE POSITION OF S IN "sound_classic" / "sound_original) Can check in notepad++!
        change.write("z", 1); // Change to "zound_classic" or "zound_original)
        change.close();
    }
    else
    {
        roll_momentum = reader.GetBoolean("Gameplay", "roll_momentum", true);
        OG_spindash = reader.Get("Gameplay", "OG_spindash", "spindash_csim");
    }

    return true;
}