
add_requires("conan::spdlog/1.15.1#92e99f07f134481bce4b70c1a41060e7", { alias = "spdlog" ,configs = {options={"spdlog/*:header_only = True"}}}) 

add_rules("mode.debug", "mode.release")


set_languages("c++20")
add_cxxflags("cl::/bigobj", "cl::/Zc:__cplusplus", "cl::/Oy-", "cl::/permissive-", "cl::/FS", "cl::/Zi")

-- todo: recover this falg to generate cod
-- add_cxxflags("cl::/FAcs")
local buildir = get_config("buildir") or "build"
local plat    = get_config("plat") or "windows"
local arch    = get_config("arch") or "x86"
local mode    = get_config("mode") or "release"
local map_path = path.join(buildir, plat, arch, mode, "mini-test.map")
add_ldflags("/DEBUG", 
        "/MAP:" .. map_path,
        "/MAPINFO:EXPORTS", 
        "/MAPINFO:FNAMES")
set_symbols("debug")
if is_mode("debug") and has_config("vld") then 
    add_defines("USE_VLD")
    local vld_root = os.getenv("VLD_ROOT") or "C:/Program Files (x86)/Visual Leak Detector"
    add_linkdirs(path.join(vld_root, "lib", "$(mode)"))
    add_includedirs(path.join(vld_root, "include"))
    add_links("vld")
end

set_configvar("PROJECT_NAME", "MT4_SG")

set_runtimes("MT")

option("vld")
    set_showmenu(true)
    set_default(false)
    set_description("Enable Visual Leak Detector (only valid in debug mode)")

option("test")
    set_showmenu(true)
    set_default(false)
    set_description("Enable build unit tests")



target("stl-test")
set_kind("binary")

add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
add_defines("MI_STAT=1")
-- add_defines("_WIN32", "_WINDOWS", "WIN32_LEAN_AND_MEAN", "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE", "_USE_32BIT_TIME_T")
add_files("src/*.cpp")
add_packages("spdlog")
add_links("advapi32") -- 添加这行，解决 LNK2019
-- add_includedirs("src")
set_version("1.16.0")

after_build(function (target)
    local codir = path.join(target:targetdir(), "cod")
    os.mkdir(codir)
    local cod_files = os.files("$(projectdir)/*.cod")
    for _, file in ipairs(cod_files) do
        os.mv(file, codir)
    end
end)

on_install(function (target)
    local installdir = path.join("$(projectdir)", "install")
    os.mkdir(installdir)

    -- cod files
    os.cp(path.join(target:targetdir(), "cod"), installdir)

    -- dll files
    local dlldir = path.join(installdir, "dll")
    os.mkdir(dlldir)
    local dll_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/mini-test.dll")
    for _, file in ipairs(dll_files) do
        os.cp(file, dlldir)
    end

    -- pdb and map files
    local pdbdir = path.join(installdir, "debug")
    os.mkdir(pdbdir)
    local pdb_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/mini-test.pdb")
    for _, file in ipairs(pdb_files) do
        os.cp(file, pdbdir)
    end
    local map_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/mini-test.map")
    for _, file in ipairs(map_files) do
        os.cp(file, pdbdir)
    end

    -- version file
    local git_commit = try { function() return os.iorun("git rev-parse --short HEAD") end }
    git_commit = (git_commit or "unknown"):trim()
    local version_info = string.format("Project Version: %s\nGit Commit: %s\n", target:version(), git_commit)
    local version_file = path.join(installdir, "version.txt")
    io.writefile(version_file, version_info)

    -- vld
    if is_mode("debug") and has_config("vld") then
        local vld_root = os.getenv("VLD_ROOT") or "C:/Program Files (x86)/Visual Leak Detector"
        os.cp(path.join(vld_root, "bin", "$(mode)"), dlldir)
        os.cp(path.join(vld_root, "vld.ini"), dlldir)
    end
end)

on_config(function (target) 
    import("core.project.task")
    task.run("check") 
end)


