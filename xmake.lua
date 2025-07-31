add_requires("conan::spdlog/1.15.1#92e99f07f134481bce4b70c1a41060e7", { alias = "spdlog" ,configs = {options={"spdlog/*:header_only = True"}}}) 
add_requires("conan::boost/1.85.0#3add30047e4245e29efe86ba14014e76", { alias = "boost", debug = is_mode("debug"), configs = { settings = "compiler.cppstd=20", options = {"boost/*:without_test=True", "boost/*:without_cobalt=False"} } })
add_requires("conan::benchmark/1.9.4#ce4403f7a24d3e1f907cd9da4b678be4", { alias = "benchmark" , debug = is_mode("debug"), configs = { settings = "compiler.cppstd=20"}})
add_requires("conan::gtest/1.14.0#25e2a474b4d1aecf5ff4f0555dcdf72c", { alias = "gtest" , debug = is_mode("debug"), configs = { settings = "compiler.cppstd=20"}})

add_rules("mode.debug", "mode.release")
set_languages("c++20")
add_cxxflags("cl::/bigobj", "cl::/Zc:__cplusplus", "cl::/Oy-", "cl::/permissive-", "cl::/FS", "cl::/Zi")
-- todo: recover this falg to generate cod
-- add_cxxflags("cl::/FAcs")
local buildir = get_config("buildir") or "build"
local plat    = get_config("plat") or "windows"
local arch    = get_config("arch") or "x86"
local mode    = get_config("mode") or "release"
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
-- =========================================
-- 主业务可执行文件
target("stl-test")
    set_kind("binary")
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
    add_defines("MI_STAT=1")
    -- add_defines("_WIN32", "_WINDOWS", "WIN32_LEAN_AND_MEAN", "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE", "_USE_32BIT_TIME_T")
    add_files("src/*.cpp")
    add_packages("spdlog", "boost")
    add_links("advapi32")
    -- add_includedirs("src")
    set_version("1.16.0")
    -- === 修复LNK1104，给不同target分配独立map，并在构建前自动建目录 ===
    on_load(function(target)
        local map_path = path.join(buildir, plat, arch, mode, target:name() .. ".map")
        target:data_set("my_map_path", map_path)
        target:add("ldflags", "/DEBUG", 
                               "/MAP:" .. map_path, 
                               "/MAPINFO:EXPORTS")
    end)
    before_build(function(target)
        local map_path = target:data("my_map_path")
        if map_path then
            local map_dir = path.directory(map_path)
            if not os.isdir(map_dir) then
                os.mkdir(map_dir)
            end
        end
    end)
    after_build(function(target)
        local codir = path.join(target:targetdir(), "cod")
        os.mkdir(codir)
        local cod_files = os.files("$(projectdir)/*.cod")
        for _, file in ipairs(cod_files) do
            os.mv(file, codir)
        end
    end)
    on_install(function(target)
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
        -- 主程序目标的 map 文件名已变为 stl-test.map
        local map_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/stl-test.map")
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
    on_config(function(target) 
        import("core.project.task")
        task.run("check") 
    end)
-- =========================================
-- Benchmark 可执行文件
target("benchmark-stl-test")
    set_kind("binary")
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
    add_files("bench/*.cpp")   -- 假如你的benchmark代码在此
    --add_files("src/adapterQueue.h") -- 因为没有cpp文件
    add_includedirs("src")            -- 让benchmark_queue.cpp可 #include "adapterQueue.h"
    add_packages("benchmark", "spdlog", "boost")
    add_links("advapi32")
    --add_includedirs("src") -- 假如AutoShrinkBlockingQueue.h在src下，提供给benchmark用
    set_version("1.16.0")
    -- === 修复LNK1104，map单独命名并自动建目录 ===
    on_load(function(target)
        local map_path = path.join(buildir, plat, arch, mode, target:name() .. ".map")
        target:data_set("my_map_path", map_path)
        target:add("ldflags", "/DEBUG", 
                               "/MAP:" .. map_path, 
                               "/MAPINFO:EXPORTS")
    end)
    before_build(function(target)
        local map_path = target:data("my_map_path")
        if map_path then
            local map_dir = path.directory(map_path)
            if not os.isdir(map_dir) then
                os.mkdir(map_dir)
            end
        end
    end)
    after_build(function(target)
        local codir = path.join(target:targetdir(), "cod")
        os.mkdir(codir)
        local cod_files = os.files("$(projectdir)/*.cod")
        for _, file in ipairs(cod_files) do
            os.mv(file, codir)
        end
    end)
    on_install(function(target)
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
        -- benchmark目标的map文件名已变为 benchmark-stl-test.map
        local map_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/benchmark-stl-test.map")
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
    on_config(function(target) 
        import("core.project.task")
        task.run("check") 
    end)

-- =========================================
--1)开启单元测试编译选项（只需做一次即可）
--xmake f --test=y
--这是在现有配置的基础上新增或修改一个配置开关选项，即 test=y
--xmake f -p windows -a x86 -m release --toolchain=msvc -vyD --test=y
--2)编译单元测试目标(这会自动编译你的主程序、benchmark 和 unit_tests（测试用例）)
--xmake
--3)运行全部单元测试
--xmake run unit_tests

-- 单元测试 target
if has_config("test") then
    target("unit_tests")
        set_kind("binary")
        add_files("tests/*.cpp")        -- 你的全部测试代码
        add_includedirs("src")
        add_packages("gtest", "spdlog", "boost")
        add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
        set_languages("c++20")
        set_version("1.16.0")
        -- === 修复LNK1104，map单独命名并自动建目录 ===
        on_load(function(target)
            local map_path = path.join(buildir, plat, arch, mode, target:name() .. ".map")
            target:data_set("my_map_path", map_path)
            target:add("ldflags", "/DEBUG", 
                                   "/MAP:" .. map_path, 
                                   "/MAPINFO:EXPORTS")
        end)
        before_build(function(target)
            local map_path = target:data("my_map_path")
            if map_path then
                local map_dir = path.directory(map_path)
                if not os.isdir(map_dir) then
                    os.mkdir(map_dir)
                end
            end
        end)
        after_build(function(target)
            local codir = path.join(target:targetdir(), "cod")
            os.mkdir(codir)
            local cod_files = os.files("$(projectdir)/*.cod")
            for _, file in ipairs(cod_files) do
                os.mv(file, codir)
            end
        end)
        on_install(function(target)
            local installdir = path.join("$(projectdir)", "install")
            os.mkdir(installdir)
            -- cod files
            os.cp(path.join(target:targetdir(), "cod"), installdir)
            -- dll files
            local dlldir = path.join(installdir, "dll")
            os.mkdir(dlldir)
            local dll_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/unit_tests.dll")
            for _, file in ipairs(dll_files) do
                os.cp(file, dlldir)
            end
            -- pdb and map files
            local pdbdir = path.join(installdir, "debug")
            os.mkdir(pdbdir)
            local pdb_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/unit_tests.pdb")
            for _, file in ipairs(pdb_files) do
                os.cp(file, pdbdir)
            end
            local map_files = os.files("$(buildir)/$(plat)/$(arch)/$(mode)/unit_tests.map")
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
        on_config(function(target) 
            import("core.project.task")
            task.run("check") 
        end)
end