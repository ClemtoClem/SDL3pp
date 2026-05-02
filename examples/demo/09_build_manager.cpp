/**
 * @file 09_build_manager.cpp
 * @brief Build Manager — select C/C++ source files from a directory tree,
 *        configure compiler options, compile with g++ or clang++, and view
 *        the output with error/warning counting and filtering.
 *
 * Compilation configurations are saved to data/build_manager.json using
 * SDL3pp DataScripts.
 *
 * Controls:
 *   - Navigate the directory tree on the left panel.
 *   - Click a C/C++ file to add it to the compilation list.
 *   - Configure compiler, C++ version, libraries and include paths in the
 *     right settings panel.
 *   - Click [Compile] to build; output appears in the bottom text area.
 *   - Use the filter buttons to show all lines, errors only, or warnings.
 *   - [Save Config] / [Load Config] persist settings to data/build_manager.json.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_filesystem.h>
#include <SDL3pp/SDL3pp_dataScripts.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
    constexpr SDL::Color BG      = { 14,  16,  22, 255};
    constexpr SDL::Color PANEL   = { 20,  22,  32, 255};
    constexpr SDL::Color CARD    = { 26,  28,  42, 255};
    constexpr SDL::Color HDR     = { 18,  20,  30, 255};
    constexpr SDL::Color ACCENT  = { 55, 130, 220, 255};
    constexpr SDL::Color ACCENT2 = { 40, 160, 100, 255};
    constexpr SDL::Color WARN    = {200, 160,  30, 255};
    constexpr SDL::Color ERR     = {210,  50,  50, 255};
    constexpr SDL::Color WHITE   = {215, 218, 228, 255};
    constexpr SDL::Color GREY    = {130, 138, 155, 255};
    constexpr SDL::Color BORDER  = { 45,  50,  72, 255};
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string FileExtension(std::string_view path) {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return {};
    return std::string(path.substr(dot));
}

static std::string FileName(std::string_view path) {
    auto slash = path.rfind('/');
    if (slash == std::string_view::npos) slash = path.rfind('\\');
    if (slash == std::string_view::npos) return std::string(path);
    return std::string(path.substr(slash + 1));
}

static bool IsCppFile(std::string_view ext) {
    return ext == ".c" || ext == ".cpp" || ext == ".cxx" || ext == ".cc"
        || ext == ".C" || ext == ".CPP";
}

static bool IsHeaderFile(std::string_view ext) {
    return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh";
}

// Run a shell command and capture its combined stdout+stderr output.
static std::string RunProcess(const std::string& cmd) {
    std::string result;
    char buf[512];
    FILE* fp = popen((cmd + " 2>&1").c_str(), "r");
    if (!fp) return "Failed to start process.";
    while (fgets(buf, sizeof(buf), fp))
        result += buf;
    pclose(fp);
    return result;
}

// Returns true if `pkg-config --exists <name>` succeeds.
static bool PkgConfigExists(const std::string& name) {
    FILE* fp = popen(("pkg-config --exists " + name + " 2>/dev/null && echo ok").c_str(), "r");
    if (!fp) return false;
    char buf[8] = {};
    fgets(buf, sizeof(buf), fp);
    pclose(fp);
    return std::string_view(buf).find("ok") != std::string_view::npos;
}

// Returns true if `lib<name>.so` (or `.a`) is found in common library search paths.
static bool SharedLibExists(const std::string& name) {
    static const char* kPaths[] = {
        "/usr/lib/", "/usr/local/lib/",
        "/usr/lib/x86_64-linux-gnu/", "/usr/lib/aarch64-linux-gnu/",
        "/usr/lib64/", nullptr
    };
    for (int i = 0; kPaths[i]; ++i) {
        std::string so  = std::string(kPaths[i]) + "lib" + name + ".so";
        std::string a   = std::string(kPaths[i]) + "lib" + name + ".a";
        try { if (SDL::GetPathInfo(so)) return true; } catch (...) {}
        try { if (SDL::GetPathInfo(a))  return true; } catch (...) {}
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compilation output parsing
// ─────────────────────────────────────────────────────────────────────────────

enum class FilterMode { All, ErrorsOnly, WarningsOnly };

struct ParsedOutput {
    std::string fullLog;
    int         errors   = 0;
    int         warnings = 0;

    void parse(const std::string& raw) {
        fullLog  = raw;
        errors   = 0;
        warnings = 0;
        std::istringstream ss(raw);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.find(": error:")   != std::string::npos) ++errors;
            if (line.find(": warning:") != std::string::npos) ++warnings;
            if (line.find(": fatal error:") != std::string::npos) ++errors;
        }
    }

    std::string filtered(FilterMode mode) const {
        if (mode == FilterMode::All) return fullLog;
        std::string out;
        std::istringstream ss(fullLog);
        std::string line;
        while (std::getline(ss, line)) {
            bool isErr  = line.find(": error:")       != std::string::npos
                       || line.find(": fatal error:") != std::string::npos;
            bool isWarn = line.find(": warning:")     != std::string::npos;
            if (mode == FilterMode::ErrorsOnly   && isErr)  out += line + '\n';
            if (mode == FilterMode::WarningsOnly && isWarn) out += line + '\n';
        }
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Main application class
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
    // ── SDL / UI setup ────────────────────────────────────────────────────────

    SDL::Window      window  { SDL::CreateWindowAndRenderer(
                                 "SDL3pp - Build Manager", {1400, 800},
                                 SDL_WINDOW_RESIZABLE, nullptr) };
    SDL::RendererRef renderer{ window.GetRenderer() };

    SDL::ResourceManager     rm;
    SDL::ResourcePool&       pool{ *rm.CreatePool("ui") };
    SDL::ECS::Context        ecs;
    SDL::UI::System          ui{ ecs, renderer, {}, pool };
    SDL::FrameTimer          timer{ 60.f };

    // ── UI entity IDs ─────────────────────────────────────────────────────────

    SDL::ECS::EntityId id_tree         = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_fileList     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_rootInput    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_compilerBox  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cppVerBox    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_targetInput  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_libInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_libList      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_incInput     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_incList      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_output       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_errLabel     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_warnLabel    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_compileBtn   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_filterAll    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_filterErr    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_filterWarn   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_statusLabel  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_checkBtn     = SDL::ECS::NullEntity;

    // ── Application state ─────────────────────────────────────────────────────

    std::vector<std::string>     m_selectedFiles;
    std::vector<std::string>     m_extraLibs;
    std::vector<std::string>     m_includeDirs;
    std::unordered_set<std::string> m_loadedDirs;

    std::string m_treeRoot;
    std::string m_configPath;

    // Current settings
    int m_compilerIdx = 0;   // 0=g++ 1=clang++ 2=Android
    int m_cppVerIdx   = 4;   // 0=11 1=14 2=17 3=20 4=23
    static constexpr const char* kCompilers[]   = { "g++", "clang++", "Android NDK" };
    static constexpr const char* kCppVersions[] = { "11", "14", "17", "20", "23" };

    // Compilation thread
    std::atomic<bool> m_compiling{false};
    std::thread       m_compileThread;
    std::mutex        m_outputMtx;
    ParsedOutput      m_pendingOutput;
    bool              m_hasPendingOutput = false;
    ParsedOutput      m_output;
    FilterMode        m_filterMode = FilterMode::All;

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if      (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug")   priority = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info")    priority = SDL::LOG_PRIORITY_INFO;
		}
		SDL::SetLogPriorities(priority);
		SDL::SetAppMetadata("SDL3pp Build manager", "1.0", "com.example.build_manager");
		SDL::Init(SDL::INIT_VIDEO);
		SDL::TTF::Init();
		*out = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::TTF::Quit();
		SDL::Quit();
	}

    // ── Constructor ───────────────────────────────────────────────────────────

    Main() {
        window.StartTextInput();

        // Load fonts
        std::string assetsPath = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("font", assetsPath + "fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 13.f);

        // Default tree root = home directory
        const char* home = SDL::GetUserFolder(SDL::FOLDER_HOME);
        m_treeRoot = home ? home : SDL::GetBasePath();

        // Config file in data/
        std::string dataPath = std::string(SDL::GetBasePath()) + "../../../data/";
        m_configPath = dataPath + "build_manager.json";
        SDL::EnsureDirectoryExists(dataPath);

        _BuildUI();
        _InitTree(m_treeRoot);
        _TryLoadConfig();
        timer.Begin();
    }

    ~Main() {
        if (m_compileThread.joinable()) m_compileThread.join();
        pool.Release();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Tree management
    // ─────────────────────────────────────────────────────────────────────────

    void _InitTree(const std::string& rootPath) {
        m_loadedDirs.clear();
        ui.ClearTreeNodes(id_tree);

        // Insert a virtual root node representing the root path
        SDL::UI::TreeNodeData root;
        root.label       = rootPath;
        root.iconKey     = rootPath;
        root.level       = 0;
        root.hasChildren = true;
        root.expanded    = false;
        ui.AddTreeNode(id_tree, root);
        ui.MarkLayoutDirty();
    }

    void _LoadTreeChildren(int parentIdx, const std::string& dirPath, int childLevel) {
        auto* td = ui.GetTreeData(id_tree);
        if (!td) return;

        std::vector<SDL::UI::TreeNodeData> dirs, files;

        try {
            SDL::EnumerateDirectory(dirPath, [&](const char* dirname, const char* fname) {
                if (fname[0] == '.') return SDL::ENUM_CONTINUE;
                std::string fullPath = SDL::JoinPath(dirname, fname);
                try {
                    SDL::PathInfo info = SDL::GetPathInfo(fullPath);
                    SDL::UI::TreeNodeData child;
                    child.label       = fname;
                    child.iconKey     = fullPath;
                    child.level       = childLevel;
                    child.hasChildren = (info.type == SDL::PATHTYPE_DIRECTORY);
                    child.expanded    = false;
                    if (child.hasChildren)
                        dirs.push_back(std::move(child));
                    else
                        files.push_back(std::move(child));
                } catch (...) {}
                return SDL::ENUM_CONTINUE;
            });
        } catch (...) {}

        auto byName = [](const SDL::UI::TreeNodeData& a, const SDL::UI::TreeNodeData& b) {
            return a.label < b.label;
        };
        std::sort(dirs.begin(),  dirs.end(),  byName);
        std::sort(files.begin(), files.end(), byName);

        auto pos = (int)parentIdx + 1;
        td->nodes.insert(td->nodes.begin() + pos, dirs.begin(), dirs.end());
        pos += (int)dirs.size();
        td->nodes.insert(td->nodes.begin() + pos, files.begin(), files.end());
    }

    void _OnTreeNodeSelected(int nodeIdx, bool hasChildren) {
        auto* td = ui.GetTreeData(id_tree);
        if (!td || nodeIdx < 0 || nodeIdx >= (int)td->nodes.size()) return;

        auto& node = td->nodes[nodeIdx];
        const std::string& path = node.iconKey;

        if (hasChildren) {
            // Lazy load: insert children the first time this directory is expanded
            if (m_loadedDirs.find(path) == m_loadedDirs.end()) {
                m_loadedDirs.insert(path);
                _LoadTreeChildren(nodeIdx, path, node.level + 1);
                node.expanded = true;
            }
        } else {
            // File node: add to list if it is a C/C++ source file
            std::string ext = FileExtension(path);
            if (IsCppFile(ext) || IsHeaderFile(ext)) {
                _AddFileToList(path);
            }
        }
        ui.MarkLayoutDirty();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // File list management
    // ─────────────────────────────────────────────────────────────────────────

    void _AddFileToList(const std::string& path) {
        if (std::find(m_selectedFiles.begin(), m_selectedFiles.end(), path)
            != m_selectedFiles.end()) return;  // already in list
        m_selectedFiles.push_back(path);
        _RefreshFileList();
    }

    void _RemoveSelectedFile() {
        int sel = ui.GetListBoxSelection(id_fileList);
        if (sel < 0 || sel >= (int)m_selectedFiles.size()) return;
        m_selectedFiles.erase(m_selectedFiles.begin() + sel);
        _RefreshFileList();
    }

    void _ClearFileList() {
        m_selectedFiles.clear();
        _RefreshFileList();
    }

    void _RefreshFileList() {
        std::vector<std::string> display;
        display.reserve(m_selectedFiles.size());
        for (auto& p : m_selectedFiles)
            display.push_back(FileName(p));
        ui.SetListBoxItems(id_fileList, std::move(display));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Library / Include dir management
    // ─────────────────────────────────────────────────────────────────────────

    void _AddLib() {
        std::string lib = ui.GetText(id_libInput);
        if (lib.empty()) return;
        m_extraLibs.push_back(lib);
        ui.SetText(id_libInput, "");
        ui.SetListBoxItems(id_libList, m_extraLibs);
    }

    void _RemoveLib() {
        int sel = ui.GetListBoxSelection(id_libList);
        if (sel < 0 || sel >= (int)m_extraLibs.size()) return;
        m_extraLibs.erase(m_extraLibs.begin() + sel);
        ui.SetListBoxItems(id_libList, m_extraLibs);
    }

    void _AddInclude() {
        std::string inc = ui.GetText(id_incInput);
        if (inc.empty()) return;
        m_includeDirs.push_back(inc);
        ui.SetText(id_incInput, "");
        ui.SetListBoxItems(id_incList, m_includeDirs);
    }

    void _RemoveInclude() {
        int sel = ui.GetListBoxSelection(id_incList);
        if (sel < 0 || sel >= (int)m_includeDirs.size()) return;
        m_includeDirs.erase(m_includeDirs.begin() + sel);
        ui.SetListBoxItems(id_incList, m_includeDirs);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Compilation
    // ─────────────────────────────────────────────────────────────────────────

    std::string _BuildCommand() const {
        // Compiler name
        const char* compiler = kCompilers[m_compilerIdx];
        if (m_compilerIdx == 2) return "echo 'Android NDK compilation not yet implemented.'";

        // Target name
        std::string target = ui.GetText(id_targetInput);
        if (target.empty()) target = "output";

        std::string cmd;
        cmd.reserve(512);
        cmd += compiler;
        cmd += " -std=c++";
        cmd += kCppVersions[m_cppVerIdx];
        cmd += " -Wall -Wextra";

        // Source files
        for (auto& f : m_selectedFiles)
            cmd += " \"" + f + "\"";

        // Include directories
        for (auto& inc : m_includeDirs)
            cmd += " -I\"" + inc + "\"";

        // Default SDL3pp libraries (mirrors the Makefile)
        cmd += " $(pkg-config --cflags --libs sdl3 sdl3-image sdl3-mixer sdl3-ttf 2>/dev/null)";
        cmd += " $(pkg-config --cflags --libs vulkan 2>/dev/null)";

        // User-specified extra libraries
        for (auto& lib : m_extraLibs)
            cmd += " -l" + lib;

        cmd += " -o \"" + target + "\"";
        return cmd;
    }

    void _Compile() {
        if (m_compiling.load()) return;
        if (m_selectedFiles.empty()) {
            ui.SetTextAreaContent(id_output, "No files selected for compilation.\n");
            return;
        }

        std::string cmd = _BuildCommand();
        ui.SetText(id_statusLabel, "Compiling…");
        ui.SetEnabled(id_compileBtn, false);
        ui.SetTextAreaContent(id_output, "$ " + cmd + "\n");
        m_compiling.store(true);

        if (m_compileThread.joinable()) m_compileThread.join();
        m_compileThread = std::thread([this, cmd]() {
            std::string raw = RunProcess(cmd);
            std::lock_guard lock(m_outputMtx);
            m_pendingOutput.parse(raw);
            m_hasPendingOutput = true;
            m_compiling.store(false);
        });
    }

    void _ApplyOutput() {
        ui.SetEnabled(id_compileBtn, true);
        const std::string text = "$ " + _BuildCommand() + "\n\n" + m_output.filtered(m_filterMode);
        ui.SetTextAreaContent(id_output, text);
        ui.SetText(id_errLabel,  std::format("Errors: {}", m_output.errors));
        ui.SetText(id_warnLabel, std::format("Warnings: {}", m_output.warnings));
        const char* status = m_output.errors > 0 ? "Build failed" :
                             m_output.warnings > 0 ? "Build succeeded (with warnings)" :
                             m_output.fullLog.empty() ? "Nothing to show" : "Build succeeded";
        ui.SetText(id_statusLabel, status);
    }

    void _SetFilter(FilterMode mode) {
        m_filterMode = mode;
        _ApplyOutput();
        SDL::Color activeCol  = pal::ACCENT;
        SDL::Color defaultCol = pal::CARD;
        using namespace SDL::UI;
        auto setBtn = [&](SDL::ECS::EntityId e, bool active) {
            if (auto* s = ecs.Get<Style>(e))
                s->bgColor = active ? activeCol : defaultCol;
        };
        setBtn(id_filterAll,  mode == FilterMode::All);
        setBtn(id_filterErr,  mode == FilterMode::ErrorsOnly);
        setBtn(id_filterWarn, mode == FilterMode::WarningsOnly);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Libs & includes validation
    // ─────────────────────────────────────────────────────────────────────────

    void _CheckLibsAndIncludes() {
        ui.SetEnabled(id_checkBtn, false);
        ui.SetText(id_statusLabel, "Checking…");

        int okCount   = 0;
        int failCount = 0;
        std::string report;

        // ── Include directories ───────────────────────────────────────────────
        if (m_includeDirs.empty()) {
            report += "No include directories configured.\n";
        }
        for (auto& inc : m_includeDirs) {
            if (SDL::DirectoryExists(inc)) {
                report += "[OK]   Include: " + inc + "\n";
                ++okCount;
            } else {
                report += "[MISS] Include: " + inc + "\n";
                ++failCount;
            }
        }

        // ── Libraries ─────────────────────────────────────────────────────────
        if (m_extraLibs.empty()) {
            report += "No extra libraries configured.\n";
        }
        for (auto& lib : m_extraLibs) {
            bool found = PkgConfigExists(lib) || SharedLibExists(lib);
            if (found) {
                report += "[OK]   Library: " + lib + "\n";
                ++okCount;
            } else {
                report += "[MISS] Library: " + lib + "\n";
                ++failCount;
            }
        }

        // ── Summary ───────────────────────────────────────────────────────────
        report += "\n";
        if (failCount == 0 && okCount > 0)
            report += std::format("All {} item(s) found.", okCount);
        else if (failCount > 0)
            report += std::format("{} found, {} missing.", okCount, failCount);
        else
            report += "Nothing to check.";

        ui.SetTextAreaContent(id_output, report);
        ui.SetText(id_statusLabel,
            failCount > 0 ? std::format("{} missing", failCount) : "All OK");
        ui.SetEnabled(id_checkBtn, true);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration persistence (DataScripts JSON)
    // ─────────────────────────────────────────────────────────────────────────

    void _SaveConfig() {
        auto doc  = std::make_shared<SDL::JSONDataDocument>();
        auto root = SDL::ObjectDataNode::Make();

        // Helper: string → StringDataNode
        auto Str = [](const std::string& v) {
            return SDL::StringDataNode::Make(v);
        };
        // Helper: vector<string> → ArrayDataNode
        auto StrArr = [&](const std::vector<std::string>& v) {
            auto arr = SDL::ArrayDataNode::Make();
            for (auto& s : v) arr->add(Str(s));
            return arr;
        };

        root->set("treeRoot",    Str(m_treeRoot));
        root->set("compiler",    SDL::S32DataNode::Make(m_compilerIdx));
        root->set("cppVersion",  SDL::S32DataNode::Make(m_cppVerIdx));
        root->set("target",      Str(ui.GetText(id_targetInput)));
        root->set("files",       StrArr(m_selectedFiles));
        root->set("extraLibs",   StrArr(m_extraLibs));
        root->set("includeDirs", StrArr(m_includeDirs));
        doc->setRoot(root);

        try {
            std::string json = doc->encode();
            FILE* fp = std::fopen(m_configPath.c_str(), "w");
            if (!fp) throw std::runtime_error("cannot open file");
            std::fwrite(json.data(), 1, json.size(), fp);
            std::fclose(fp);
            ui.SetText(id_statusLabel, "Config saved.");
        } catch (...) {
            ui.SetText(id_statusLabel, "Failed to save config.");
        }
    }

    void _TryLoadConfig() {
        try {
            auto io = SDL::IOFromFile(m_configPath.c_str(), "r");
            auto doc = std::make_shared<SDL::JSONDataDocument>();
            if (doc->decode(io)) return; // parse error → skip

            auto root = doc->getRoot();
            if (!root) return;

            // Helper: read string from node
            auto getStr = [](std::shared_ptr<SDL::DataNode> n) -> std::string {
                if (!n) return {};
                auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(n);
                return s ? s->getValue() : SDL::NodeSerializer::toScalarString(n);
            };
            // Helper: read int from node
            auto getInt = [](std::shared_ptr<SDL::DataNode> n) -> int {
                if (!n) return 0;
                auto i = std::dynamic_pointer_cast<SDL::S32DataNode>(n);
                return i ? (int)i->getValue() : 0;
            };
            // Helper: array of strings
            auto getStrArr = [&](const std::string& key) -> std::vector<std::string> {
                std::vector<std::string> out;
                if (!root->has(key)) return out;
                auto arr = std::dynamic_pointer_cast<SDL::ArrayDataNode>(root->get(key));
                if (!arr) return out;
                for (size_t i = 0; i < arr->getSize(); ++i)
                    out.push_back(getStr(arr->get(i)));
                return out;
            };

            if (root->has("treeRoot")) {
                m_treeRoot = getStr(root->get("treeRoot"));
                ui.SetText(id_rootInput, m_treeRoot);
                _InitTree(m_treeRoot);
            }
            if (root->has("compiler")) {
                m_compilerIdx = SDL::Clamp(getInt(root->get("compiler")), 0, 2);
                ui.SetValue(id_compilerBox, (float)m_compilerIdx);
            }
            if (root->has("cppVersion")) {
                m_cppVerIdx = SDL::Clamp(getInt(root->get("cppVersion")), 0, 4);
                ui.SetValue(id_cppVerBox, (float)m_cppVerIdx);
            }
            if (root->has("target"))
                ui.SetText(id_targetInput, getStr(root->get("target")));

            m_selectedFiles = getStrArr("files");
            m_extraLibs     = getStrArr("extraLibs");
            m_includeDirs   = getStrArr("includeDirs");

            _RefreshFileList();
            ui.SetListBoxItems(id_libList, m_extraLibs);
            ui.SetListBoxItems(id_incList, m_includeDirs);
        } catch (...) {
            // Config file does not exist yet — ignore silently.
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI construction
    // ─────────────────────────────────────────────────────────────────────────

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f))
            .Children(
                _BuildHeader(),
                _BuildBody(),
                _BuildOutputBar()
            )
            .AsRoot();
    }

    // ── Header ────────────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildHeader() {
        auto hdr = ui.Row("header", 10.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(50.f)
            .PaddingH(12.f).PaddingV(7.f)
            .BgColor(pal::HDR)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BdColor(pal::BORDER);

        hdr.Child(ui.Label("title", "Build Manager")
            .TextColor(pal::ACCENT).Font("font", 16.f));

        hdr.Child(ui.Separator("hdr_sep").W(1.f).H(SDL::UI::Value::Grow(100.f)));

        hdr.Child(ui.Label("root_lbl", "Root:").TextColor(pal::GREY));

        id_rootInput = ui.Input("root_input", m_treeRoot)
            .W(SDL::UI::Value::Grow(100.f)).H(32.f);
        hdr.Child(id_rootInput);

        hdr.Child(ui.Button("root_btn", "Browse")
            .W(74.f).H(32.f)
            .BgColor(pal::CARD)
            .OnClick([this]{
                m_treeRoot = ui.GetText(id_rootInput);
                m_loadedDirs.clear();
                _InitTree(m_treeRoot);
            }));

        id_statusLabel = ui.Label("status_lbl", "").TextColor(pal::GREY);
        hdr.Child(id_statusLabel);

        return hdr;
    }

    // ── Body ──────────────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildBody() {
        auto body = ui.Row("body", 6.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f))
            .H(SDL::UI::Value::Wh(55.f))
            .PaddingH(6.f).PaddingV(6.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f));

        body.Child(_BuildTreePanel());
        body.Child(_BuildFilePanel());
        body.Child(_BuildSettingsPanel());
        return body;
    }

    // ── Directory tree panel ──────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildTreePanel() {
        auto panel = ui.Column("tree_panel", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(28.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .PaddingH(4.f).PaddingV(6.f);

        panel.Child(ui.Label("tree_title", "Directory Browser")
            .TextColor(pal::GREY).Font("font", 12.f));

        id_tree = ui.Tree("dir_tree")
            .W(SDL::UI::Value::Pw(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(0.f))
            .TreeItemHeight(22.f)
            .TreeIndent(16.f)
            .OnTreeSelect([this](int idx, bool hasChildren){
                _OnTreeNodeSelected(idx, hasChildren);
            });
        panel.Child(id_tree);

        return panel;
    }

    // ── File list panel ───────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildFilePanel() {
        auto panel = ui.Column("file_panel", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(32.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .PaddingH(8.f).PaddingV(8.f);

        panel.Child(ui.Label("files_title", "Files to compile")
            .TextColor(pal::GREY).Font("font", 12.f));

        id_fileList = ui.ListBoxWidget("file_list", {})
            .W(SDL::UI::Value::Pw(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::CARD)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER)
            .Radius(SDL::FCorners(4.f));
        panel.Child(id_fileList);

        // Action buttons
        auto btns = ui.Row("file_btns", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(30.f)
            .BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));

        btns.Child(ui.Button("btn_remove", "Remove")
            .W(SDL::UI::Value::Grow(50.f)).H(30.f)
            .BgColor(pal::CARD)
            .OnClick([this]{ _RemoveSelectedFile(); }));

        btns.Child(ui.Button("btn_clear", "Clear All")
            .W(SDL::UI::Value::Grow(50.f)).H(30.f)
            .BgColor(pal::CARD)
            .OnClick([this]{ _ClearFileList(); }));

        panel.Child(btns);
        return panel;
    }

    // ── Settings panel ────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildSettingsPanel() {
        auto panel = ui.Column("settings_panel", 8.f, 0.f)
            .W(SDL::UI::Value::Grow(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER)
            .Radius(SDL::FCorners(6.f))
            .PaddingH(10.f).PaddingV(8.f);

        panel.Child(ui.Label("settings_title", "Compiler Settings")
            .TextColor(pal::GREY).Font("font", 12.f));
        panel.Child(ui.Separator("sep1").W(SDL::UI::Value::Pw(100.f)).H(1.f));

        // Compiler
        panel.Child(ui.Label("lbl_compiler", "Compiler").TextColor(pal::WHITE));
        id_compilerBox = ui.ComboBox("compiler_box",
            {"g++", "clang++", "Android NDK"}, m_compilerIdx)
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .BgColor(pal::CARD)
            .OnChange<float>([this](float v){ m_compilerIdx = (int)v; });
        panel.Child(id_compilerBox);

        // C++ version
        panel.Child(ui.Label("lbl_cpp", "C++ Version").TextColor(pal::WHITE));
        id_cppVerBox = ui.ComboBox("cpp_ver_box",
            {"C++11", "C++14", "C++17", "C++20", "C++23"}, m_cppVerIdx)
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .BgColor(pal::CARD)
            .OnChange<float>([this](float v){ m_cppVerIdx = (int)v; });
        panel.Child(id_cppVerBox);

        // Output target name
        panel.Child(ui.Label("lbl_target", "Output target").TextColor(pal::WHITE));
        id_targetInput = ui.Input("target_input", "output")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .BgColor(pal::CARD);
        panel.Child(id_targetInput);

        panel.Child(ui.Separator("sep2").W(SDL::UI::Value::Pw(100.f)).H(1.f));

        // Extra libraries
        panel.Child(ui.Label("lbl_libs", "Extra Libraries (-l…)").TextColor(pal::WHITE));
        id_libList = ui.ListBoxWidget("lib_list", {})
            .W(SDL::UI::Value::Pw(100.f)).H(80.f)
            .BgColor(pal::CARD)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER);
        panel.Child(id_libList);

        auto libRow = ui.Row("lib_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(26.f)
            .BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));
        id_libInput = ui.Input("lib_input", "e.g. GL")
            .W(SDL::UI::Value::Grow(100.f)).H(26.f).BgColor(pal::CARD);
        libRow.Child(id_libInput);
        libRow.Child(ui.Button("lib_add", "+").W(24.f).H(26.f)
            .BgColor(pal::ACCENT2).TextColor(pal::WHITE)
            .OnClick([this]{ _AddLib(); }));
        libRow.Child(ui.Button("lib_rem", "-").W(24.f).H(26.f)
            .BgColor(pal::ERR).TextColor(pal::WHITE)
            .OnClick([this]{ _RemoveLib(); }));
        panel.Child(libRow);

        panel.Child(ui.Separator("sep3").W(SDL::UI::Value::Pw(100.f)).H(1.f));

        // Include directories
        panel.Child(ui.Label("lbl_incs", "Include Directories (-I…)").TextColor(pal::WHITE));
        id_incList = ui.ListBoxWidget("inc_list", {})
            .W(SDL::UI::Value::Pw(100.f)).H(80.f)
            .BgColor(pal::CARD)
            .Borders(SDL::FBox(1.f)).BdColor(pal::BORDER);
        panel.Child(id_incList);

        auto incRow = ui.Row("inc_row", 4.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(26.f)
            .BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));
        id_incInput = ui.Input("inc_input", "/path/to/include")
            .W(SDL::UI::Value::Grow(100.f)).H(26.f).BgColor(pal::CARD);
        incRow.Child(id_incInput);
        incRow.Child(ui.Button("inc_add", "+").W(24.f).H(26.f)
            .BgColor(pal::ACCENT2).TextColor(pal::WHITE)
            .OnClick([this]{ _AddInclude(); }));
        incRow.Child(ui.Button("inc_rem", "-").W(24.f).H(26.f)
            .BgColor(pal::ERR).TextColor(pal::WHITE)
            .OnClick([this]{ _RemoveInclude(); }));
        panel.Child(incRow);

        // Check libs & includes
        panel.Child(ui.Separator("sep4").W(SDL::UI::Value::Pw(100.f)).H(1.f));
        id_checkBtn = ui.Button("check_btn", "Check Libs & Includes")
            .W(SDL::UI::Value::Pw(100.f)).H(30.f)
            .BgColor({60, 80, 50, 255})
            .TextColor(pal::WHITE)
            .Tooltip("Verify that each include directory exists and each library\n"
                     "is findable via pkg-config or in the standard lib paths.")
            .OnClick([this]{ _CheckLibsAndIncludes(); });
        panel.Child(id_checkBtn);

        // Save / Load
        panel.Child(ui.Separator("sep5").W(SDL::UI::Value::Pw(100.f)).H(1.f));
        auto cfgRow = ui.Row("cfg_row", 6.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(30.f)
            .BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));
        cfgRow.Child(ui.Button("save_cfg", "Save Config")
            .W(SDL::UI::Value::Grow(50.f)).H(30.f)
            .BgColor(pal::ACCENT)
            .OnClick([this]{ _SaveConfig(); }));
        cfgRow.Child(ui.Button("load_cfg", "Load Config")
            .W(SDL::UI::Value::Grow(50.f)).H(30.f)
            .BgColor(pal::CARD)
            .OnClick([this]{ _TryLoadConfig(); }));
        panel.Child(cfgRow);

        return panel;
    }

    // ── Output bar ────────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildOutputBar() {
        auto col = ui.Column("output_col", 0.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f, 1.f, 0.f, 0.f)).BdColor(pal::BORDER);

        // Toolbar
        auto bar = ui.Row("output_bar", 8.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(38.f)
            .PaddingH(10.f).PaddingV(4.f)
            .BgColor(pal::HDR)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BdColor(pal::BORDER);

        id_compileBtn = ui.Button("compile_btn", "Compile")
            .W(90.f).H(28.f)
            .BgColor(pal::ACCENT2).TextColor(pal::WHITE)
            .OnClick([this]{ _Compile(); });
        bar.Child(id_compileBtn);

        bar.Child(ui.Separator("out_sep").W(1.f).H(SDL::UI::Value::Grow(100.f)));
        bar.Child(ui.Label("filter_lbl", "Filter:").TextColor(pal::GREY));

        id_filterAll = ui.Button("f_all",  "All")
            .W(50.f).H(28.f).BgColor(pal::ACCENT)
            .OnClick([this]{ _SetFilter(FilterMode::All); });
        bar.Child(id_filterAll);

        id_filterErr = ui.Button("f_err",  "Errors")
            .W(60.f).H(28.f).BgColor(pal::CARD)
            .OnClick([this]{ _SetFilter(FilterMode::ErrorsOnly); });
        bar.Child(id_filterErr);

        id_filterWarn = ui.Button("f_warn", "Warnings")
            .W(72.f).H(28.f).BgColor(pal::CARD)
            .OnClick([this]{ _SetFilter(FilterMode::WarningsOnly); });
        bar.Child(id_filterWarn);

        bar.Child(ui.Separator("out_sep2").W(1.f).H(SDL::UI::Value::Grow(100.f)));

        id_errLabel = ui.Label("err_lbl", "Errors: 0")
            .TextColor(pal::ERR);
        bar.Child(id_errLabel);

        id_warnLabel = ui.Label("warn_lbl", "Warnings: 0")
            .TextColor(pal::WARN);
        bar.Child(id_warnLabel);

        col.Child(bar);

        // Output text area (read-only)
        id_output = ui.TextArea("output_ta", "Ready.")
            .W(SDL::UI::Value::Pw(100.f))
            .H(SDL::UI::Value::Grow(100.f))
            .BgColor(pal::CARD)
            .Borders(SDL::FBox(0.f))
            .TextAreaTabSize(4)
            .ReadOnly(true);
        col.Child(id_output);

        return col;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Frame callbacks
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        timer.Begin();
        const float dt = timer.GetDelta();

        // Poll compilation result from background thread
        {
            std::lock_guard lock(m_outputMtx);
            if (m_hasPendingOutput) {
                m_output           = m_pendingOutput;
                m_hasPendingOutput = false;
                _ApplyOutput();
            }
        }

        pool.Update();
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        timer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            if (ev.key.key == SDL::KEYCODE_ESCAPE) return SDL::APP_SUCCESS;
            if (ev.key.key == SDL::KEYCODE_F5) _Compile();
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
