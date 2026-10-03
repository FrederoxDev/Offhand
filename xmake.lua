-- Mod Options
local mod_name = "Offhand"
local config_options = {
    extra_include_dirs = {
        "packages/spyglass",
    },
    extra_files = {
        "packages/spyglass/spyglass/input/Processor.cpp",
        "packages/spyglass/spyglass/input/Trigger.cpp",
        "packages/spyglass/spyglass/input/Binding.cpp",
        "packages/spyglass/spyglass/input/Dispatcher.cpp",
    },
}

-- Anything below here should not need to be changed
-- To update your build script if its outdated, replace everything below these comments
-- The latest version can be found here: https://github.com/AmethystAPI/Amethyst-Template/blob/main/xmake.lua
local MOD_BUILD_SCRIPT_VERSION = 2

option("automated_build")
    set_default(false)
    set_showmenu(true)
    set_description("Flag to indicate this is an automated build")
option_end()

option("platform")
    set_default("win-client")
    set_showmenu(true)
    set_values("win-client", "win-server")
    set_description("The platform to target building too")
option_end()

option("build_data")
    set_default(true)
    set_showmenu(true)
    set_description("Run Regolith after building to compile/export BP/RP data")
option_end()

local automated = is_config("automated_build", true)
local platform = get_config("platform")
local build_data = is_config("build_data", true)

-- Pinned to the nlohmann copy Amethyst bundles (AmethystAPI/include/Json.hpp, v3.11.3), so both
-- resolve to one identical type. Spyglass persists keymaps through it.
add_requires("nlohmann_json 3.11.3")

local build_script_path
local vendored_build_script = path.join("Amethyst", "AmethystAPI", "mod_build.lua")
if automated and os.isfile(vendored_build_script) then
    build_script_path = vendored_build_script
else
    local amethyst_src = os.getenv("AMETHYST_SRC")
    if amethyst_src then
        build_script_path = path.join(amethyst_src, "AmethystAPI", "mod_build.lua")
    end
end

local mc_headers_path = os.getenv("MC_HEADERS")

if build_script_path and os.isfile(build_script_path) then
    includes(build_script_path)

    local build_config = {
        MOD_BUILD_SCRIPT_VERSION = MOD_BUILD_SCRIPT_VERSION,
        platform = platform,
        mc_headers_path = mc_headers_path,
        post_build_commands = {},
    }

    if build_data then
        table.insert(build_config.post_build_commands, {
            program = "rgl",
            args = { "run", automated and "package" or "default" },
            curdir = "data",
            description = "running Regolith data export...",
        })
    end

    for k, v in pairs(config_options) do
        build_config[k] = v
    end

    build_mod(mod_name, automated, build_config)

    target(mod_name)
        add_packages("nlohmann_json")
    target_end()
else
    print("Amethyst build skipped: set AMETHYST_SRC or place Amethyst/AmethystAPI/mod_build.lua at repo root.")
end
