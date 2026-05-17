/**
 * @file 07_audio_patchbay.cpp
 * @brief Audio Patchbay — node-graph for routing audio using ECS components and Multithreading.
 */

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_audio.h>
#include <SDL3pp/SDL3pp_audioProcessing.h>
#include <SDL3pp/SDL3pp_mixer.h>
#include <SDL3pp/SDL3pp_filesystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace SDL::UI;

namespace pal {
    constexpr SDL::Color BG       = {  10,  12,  20, 255 };
    constexpr SDL::Color HEADER   = {  16,  18,  30, 255 };
    constexpr SDL::Color CANVAS   = {  12,  14,  22, 255 };
    constexpr SDL::Color GRID     = {  22,  25,  38, 255 };
    constexpr SDL::Color ACCENT   = {  60, 140, 220, 255 };
    constexpr SDL::Color GREEN    = {  45, 195, 110, 255 };
    constexpr SDL::Color ORANGE   = { 230, 145,  30, 255 };
    constexpr SDL::Color RED      = { 200,  60,  50, 255 };
    constexpr SDL::Color PURPLE   = { 155,  75, 220, 255 };
    constexpr SDL::Color TEAL     = {  45, 180, 180, 255 };
    constexpr SDL::Color WHITE    = { 220, 220, 228, 255 };
    constexpr SDL::Color PORT_IN  = {  60, 140, 220, 255 };
    constexpr SDL::Color PORT_OUT = {  45, 195, 110, 255 };
    constexpr SDL::Color WIRE     = { 210, 185,  60, 220 };
    constexpr SDL::Color WIRE_TMP = { 255, 220,  60, 180 };
}

enum class BlockType   { AudioIn, AudioOut, Filter, Amp, Mixer, Scope, Spectrum, Osc, Delay, Playlist };
enum class FilterMode  { LowPass, HighPass, BandStop };
enum class MixMode     { Add, Multiply, Average };
enum class PortDir     { In, Out };
enum class OscShape    { Sine = 0, Square, Triangle, Sawtooth, Noise };
enum class TriggerMode { Free = 0, Rising, Falling };

static constexpr const char* kOscShapeLabel[]  = {"Sin","Sqr","Tri","Saw","Nse"};
static constexpr const char* kTrigModeLabel[]  = {"Free","Rise\xe2\x86\x91","Fall\xe2\x86\x93"};

static constexpr int kSampleRates[] = {8000, 16000, 22050, 44100, 48000};
static constexpr int kBufferSizes[] = {256, 512, 1024, 2048, 4096};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool IsMusicFile(std::string_view path) {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return false;
    std::string ext = std::string(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    return ext == ".wav" || ext == ".ogg" || ext == ".flac" || ext == ".mp3";
}

static std::string FileName(std::string_view path) {
    auto slash = path.rfind('/');
    if (slash == std::string_view::npos) slash = path.rfind('\\');
    if (slash == std::string_view::npos) return std::string(path);
    return std::string(path.substr(slash + 1));
}

using MBI = SDL::UI::MenuBarItem;

static MBI MI(std::string label, std::string shortcut = {}, const char *icon = nullptr,
              std::function<void()> action = nullptr, bool enabled = true) {
    MBI item;
    item.label        = std::move(label);
    item.shortcutText = std::move(shortcut);
    item.iconKey      = icon ? std::string(icon) : std::string{};
    item.action       = std::move(action);
    item.enabled      = enabled;
    return item;
}
static MBI MSep() { return MBI::Sep(); }

static bool LoadAudioToFloatMono(const std::string& path, int targetSampleRate, std::vector<float>& outSamples) {
    outSamples.clear();
    // Primary: SDL_mixer AudioDecoder handles WAV, OGG, FLAC, MP3.
    try {
        SDL::AudioSpec dstSpec{};
        dstSpec.format   = SDL::AUDIO_F32;
        dstSpec.channels = 1;
        dstSpec.freq     = targetSampleRate;
        SDL::AudioDecoder decoder(path);
        constexpr size_t CHUNK       = 8192;
        constexpr size_t kMaxSamples = 44100 * 600; // 10-minute cap
        std::vector<float> tmp(CHUNK);
        while (outSamples.size() < kMaxSamples) {
            int got = decoder.DecodeAudio(std::span<float>{tmp.data(), CHUNK}, dstSpec);
            if (got <= 0) break;
            size_t n = (size_t)(got / (int)sizeof(float));
            outSamples.insert(outSamples.end(), tmp.data(), tmp.data() + n);
        }
        if (!outSamples.empty()) return true;
    } catch (...) {}
    // Fallback: SDL_LoadWAV for plain WAV.
    SDL_AudioSpec spec{};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) return false;
    SDL_AudioSpec targetSpec = { SDL_AUDIO_F32, 1, targetSampleRate };
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &targetSpec);
    if (stream) {
        SDL_PutAudioStreamData(stream, buf, len);
        SDL_FlushAudioStream(stream);
        int available = SDL_GetAudioStreamAvailable(stream);
        outSamples.resize(available / sizeof(float));
        SDL_GetAudioStreamData(stream, outSamples.data(), available);
        SDL_DestroyAudioStream(stream);
    }
    SDL_free(buf);
    return !outSamples.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Audio Bus & ECS Components
// ─────────────────────────────────────────────────────────────────────────────
struct AudioBus {
    std::vector<float> samples;
    bool valid = false;
    AudioBus(int size = 1024) : samples(size, 0.f) {}
    void Clear() { std::fill(samples.begin(), samples.end(), 0.f); valid = false; }
    void Resize(int sz) { if (samples.size() != static_cast<size_t>(sz)) samples.resize(sz, 0.f); }
};

struct Port {
    PortDir dir;
    std::shared_ptr<AudioBus> bus;
};

struct DSPNode {
    int id;
    BlockType type;
    int sampleRate = 44100;
    int bufferSize = 1024;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    
    std::vector<SDL::ECS::EntityId> uiInPorts;
    std::vector<SDL::ECS::EntityId> uiOutPorts;
};

// Composants d'états spécifiques
struct AudioInState {
    SDL::AudioStream stream;
    int selDeviceIdx = 0;
    bool enabled = true;
    SDL::ECS::EntityId cbDev;
};

struct AudioOutState {
    SDL::AudioStream stream;
    int selDeviceIdx = 0;
    bool enabled = true;
    SDL::ECS::EntityId cbDev;
};

struct FilterState {
    FilterMode mode = FilterMode::LowPass;
    float cutoffHz = 1000.f;
    float q = 0.7071f;
    SDL::detail::BiQuadState bq{};
    SDL::ECS::EntityId lblCutoff;
};

struct AmpState {
    float gain = 1.0f;
    SDL::ECS::EntityId lblGain;
};

struct MixerState {
    MixMode mode = MixMode::Add;
};

struct DelayState {
    float delayMs = 100.f;
    std::vector<float> buffer;
    int writeIdx = 0;
    SDL::ECS::EntityId lblMs;
};

struct SpectrumState {
    int winIdx = 1;
    float minF = 20.f;
    float maxF = 20000.f;
    std::vector<float> displayData;
    SDL::ECS::EntityId graph;
};

struct ScopeState {
    // Display frame
    std::vector<float> displayData;

    // Trigger config (written from main thread, read from audio thread)
    TriggerMode trigMode  = TriggerMode::Free;
    float       trigLevel = 0.f;
    float       vScale    = 1.f;   // vertical gain (1 = ±1.0 fills screen)

    // Internal trigger state (audio thread only)
    bool  trigArmed    = true;
    float prevSample   = 0.f;
    bool  collecting   = false;
    int   collectCount = 0;
    std::vector<float> collectBuf;

    // UI entities
    SDL::ECS::EntityId lblTrigLevel;
    SDL::ECS::EntityId lblVScale;
};

struct OscState {
    OscShape  shape = OscShape::Sine;
    float     freq  = 440.f;
    float     amp   = 0.5f;
    uint64_t  playbackNs = 0; // nanoseconds of audio committed to AudioOut
    SDL::ECS::EntityId lblFreq, lblAmp;
};

struct PlaylistState {
    SDL::ECS::EntityId uiList;
    SDL::ECS::EntityId uiExplorerPopup = SDL::ECS::NullEntity;
    SDL::ECS::EntityId uiSlider;
    SDL::ECS::EntityId uiTimeLbl;

    std::vector<std::string> tracks;
    int currentTrack = -1;
    
    int pendingTrackToLoad = -1;
    std::vector<float> currentAudioData;
    size_t playbackPos = 0;
    float durationSec = 0.f;

    bool isPlaying = false;
    bool isAutoplay = true;
    bool isLooping = false;
};

struct Connection {
    SDL::ECS::EntityId fromNode; int fromPort;
    SDL::ECS::EntityId toNode;   int toPort;
};

// ─────────────────────────────────────────────────────────────────────────────
// Application Principale
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
    SDL::Window      window { SDL::CreateWindowAndRenderer("SDL3pp -  Audio Patchbay ECS (Multithreaded)", {1400, 800}, SDL_WINDOW_RESIZABLE, nullptr) };
    SDL::RendererRef renderer{ window.GetRenderer() };

    SDL::ResourceManager rm;
    SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };
    SDL::ECS::Context    ecs;
    SDL::UI::System      ui{ ecs, renderer, SDL::MixerRef{nullptr}, pool };
    SDL::FrameTimer      timer{ 60.f };

    std::vector<Connection> connections;
    std::vector<SDL::ECS::EntityId> blocks;

    SDL::OwnArray<SDL::AudioDeviceRef> m_recDevices;
    SDL::OwnArray<SDL::AudioDeviceRef> m_playDevices;
    std::vector<std::string>           m_recNames;
    std::vector<std::string>           m_playNames;

    SDL::ECS::EntityId m_rootId = SDL::ECS::NullEntity;
    SDL::ECS::EntityId m_connectId = SDL::ECS::NullEntity;
    SDL::FPoint        mousePos = {};
    int                s_nextId = 1;

    // Multithreading pour l'audio
    std::atomic<bool> m_audioRunning{true};
    std::thread m_audioThread;
    std::mutex m_audioMutex;
    std::condition_variable m_audioCv;

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
		for (auto arg : args) {
			if (arg == "--verbose") SDL::SetLogPriorities(SDL::LOG_PRIORITY_VERBOSE);
			if (arg == "--debug")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
		}
		SDL::SetAppMetadata("SDL3pp audio patchbay", "1.0",
							"com.example.audio_patchbay");
        SDL::Init(SDL::INIT_VIDEO | SDL::INIT_AUDIO);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }
    static void Quit(Main* m, SDL::AppResult) {
        m->m_audioRunning = false;
        m->m_audioCv.notify_all();
        if (m->m_audioThread.joinable()) m->m_audioThread.join();
        delete m;
        SDL::MIX::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }

    Main() {
		window.StartTextInput();
        timer.Begin();
        ui.LoadFont("font", "assets/fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 12.f);
        _RescanDevices();
        _BuildUI();
        
        // Démarrage du thread audio
        m_audioThread = std::thread([this]() {
            while (m_audioRunning) {
                std::unique_lock<std::mutex> lock(m_audioMutex);
                // Le thread attend 5ms pour boucler, tout en conservant le verrou pour traiter le graphe
                m_audioCv.wait_for(lock, std::chrono::milliseconds(5), [this] { return !m_audioRunning; });
                if (!m_audioRunning) break;
                _ProcessGraph();
            }
        });
    }
    ~Main() { pool.Release(); }

    void _RescanDevices() {
        try { m_recDevices  = SDL::GetAudioRecordingDevices(); } catch (...) { m_recDevices  = {}; }
        try { m_playDevices = SDL::GetAudioPlaybackDevices();  } catch (...) { m_playDevices = {}; }

        m_recNames.clear();
        m_recNames.push_back("Default System / Virtual");
        for (size_t i = 0; i < m_recDevices.size(); ++i)
            try   { m_recNames.push_back(SDL::GetAudioDeviceName(m_recDevices[i])); }
            catch (...) { m_recNames.push_back(std::format("Rec {}", i)); }

        m_playNames.clear();
        m_playNames.push_back("Default System / Virtual");
        for (size_t i = 0; i < m_playDevices.size(); ++i)
            try   { m_playNames.push_back(SDL::GetAudioDeviceName(m_playDevices[i])); }
            catch (...) { m_playNames.push_back(std::format("Play {}", i)); }
            
        std::lock_guard<std::mutex> lock(m_audioMutex);
        ecs.Each<AudioInState>([&](SDL::ECS::EntityId /*e*/, AudioInState& st) {
            if (auto* d = ecs.Get<SDL::UI::ComboBoxData>(st.cbDev)) d->items = m_recNames;
        });
        ecs.Each<AudioOutState>([&](SDL::ECS::EntityId /*e*/, AudioOutState& st) {
            if (auto* d = ecs.Get<SDL::UI::ComboBoxData>(st.cbDev)) d->items = m_playNames;
        });
    }

    void _OpenAudioIn(DSPNode& node, AudioInState& st) {
        if (st.stream) { st.stream.PauseDevice(); st.stream = {}; }
        SDL::AudioSpec sp{ SDL::AUDIO_F32, 1, node.sampleRate };
        try {
            if (st.selDeviceIdx == 0)
                st.stream = SDL::AudioDeviceRef{SDL::AUDIO_DEVICE_DEFAULT_RECORDING}.OpenStream(sp, nullptr, nullptr);
            else if (st.selDeviceIdx - 1 < (int)m_recDevices.size())
                st.stream = m_recDevices[st.selDeviceIdx - 1].OpenStream(sp, nullptr, nullptr);
            if (st.stream) st.stream.ResumeDevice();
        } catch (...) {}
    }

    void _OpenAudioOut(DSPNode& node, AudioOutState& st) {
        if (st.stream) { st.stream.PauseDevice(); st.stream = {}; }
        SDL::AudioSpec sp{ SDL::AUDIO_F32, 1, node.sampleRate };
        try {
            if (st.selDeviceIdx == 0)
                st.stream = SDL::AudioDeviceRef{SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK}.OpenStream(sp, nullptr, nullptr);
            else if (st.selDeviceIdx - 1 < (int)m_playDevices.size())
                st.stream = m_playDevices[st.selDeviceIdx - 1].OpenStream(sp, nullptr, nullptr);
            if (st.stream) st.stream.ResumeDevice();
        } catch (...) {}
    }

    SDL::ECS::EntityId _CreateFileExplorer(SDL::ECS::EntityId playlistId) {
        std::string bid = std::to_string(playlistId);
        auto popup = ui.Popup("fileExp_" + bid, "Select Audio Files", true, true, true)
            .PopupOpen(false)
            .Fixed(SDL::UI::Value::Px(150.f), SDL::UI::Value::Px(150.f))
            .W(SDL::UI::Value::Px(450.f)).H(SDL::UI::Value::Px(400.f))
            .Padding(4.f).AttachTo(m_rootId);

        SDL::ECS::EntityId popId = popup.Id();

        // Default tree root = home directory
        const char* home = SDL::GetUserFolder(SDL::FOLDER_HOME);
        const char* treeRoot = home ? home : SDL::GetBasePath();
        
        auto pathInput = ui.Input("path_in_" + bid, treeRoot).GrowW(100.f);
        auto tree = ui.Tree("dir_tree_" + bid).GrowW(100.f).GrowH(100.f);

        SDL::ECS::EntityId treeId = tree.Id();
        SDL::ECS::EntityId pathId = pathInput.Id();

        auto goBtn = ui.Button("go_btn_" + bid, "Go").OnClick([this, treeId, pathId]() {
            if (auto* c = ecs.Get<SDL::UI::EditableContent>(pathId)) {
                std::string path = c->text;
                if (auto* td = ui.GetTreeData(treeId)) {
                    td->nodes.clear();
                    td->nodes.push_back({path, path, 0, true, false});
                    ui.MarkLayoutDirty();
                }
            }
        });

        auto topRow = ui.Row("topR_" + bid, 4.f).GrowW(100.f).Children(pathInput, goBtn);

        ui.AddTreeNode(treeId, {treeRoot, treeRoot, 0, true, false});

        ui.OnTreeSelect(treeId, [this, playlistId, pathId, treeId](int idx, bool hasKids) {
            auto* td = ui.GetTreeData(treeId);
            if (!td) return;
            auto& n = td->nodes[idx];

            if (hasKids) {
                if (auto* c = ecs.Get<SDL::UI::EditableContent>(pathId)) {
                    c->text = n.iconKey;
                    c->cursor = (int)c->text.size();
                    ui.MarkLayoutDirty();
                }

                bool alreadyLoaded = (idx + 1 < (int)td->nodes.size() && td->nodes[idx+1].level > n.level);
                bool wasExpanded = n.expanded;

                if (!alreadyLoaded && !wasExpanded) {
                    try {
                        std::vector<SDL::UI::TreeNodeData> kids;
                        SDL::EnumerateDirectory(n.iconKey, [&](const char* dir, const char* f) {
                            if (f[0] == '.') return SDL::ENUM_CONTINUE; 
                            std::string path = SDL::JoinPath(dir, f);
                            bool isDir = false;
                            try { isDir = (SDL::GetPathInfo(path).type == SDL::PATHTYPE_DIRECTORY); } catch(...) {}
                            
                            if (isDir || IsMusicFile(f)) {
                                kids.push_back({f, path, n.level + 1, isDir, false});
                            }
                            return SDL::ENUM_CONTINUE;
                        });
                        std::sort(kids.begin(), kids.end(), [](const auto& a, const auto& b) {
                            if (a.hasChildren != b.hasChildren) return a.hasChildren > b.hasChildren;
                            return a.label < b.label;
                        });
                        td->nodes.insert(td->nodes.begin() + idx + 1, kids.begin(), kids.end());
                        ui.MarkLayoutDirty();
                    } catch(...) {}
                }
                td->nodes[idx].expanded = !wasExpanded;
            } else {
                std::lock_guard<std::mutex> lk(m_audioMutex);
                if (auto* ps = ecs.Get<PlaylistState>(playlistId)) {
                    ps->tracks.push_back(n.iconKey);
                    if (auto* ld = ui.GetListBoxData(ps->uiList)) {
                        ld->items.push_back(FileName(n.iconKey));
                        ui.MarkLayoutDirty();
                    }
                }
            }
        });

        auto layout = ui.Column("layout_" + bid, 4.f, 4.f).GrowW(100.f).GrowH(100.f).Children(topRow, tree);
        ui.GetBuilder(popId).Child(layout);
        
        return popId;
    }

    void AddBlock(BlockType type) {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        int id = s_nextId++;
        std::string bid = std::to_string(id);
        
        std::string title;
        SDL::FPoint sz;
        SDL::Color color;
        int numIn = 0, numOut = 0;

        switch(type) {
            case BlockType::AudioIn:  title="Audio In"; sz={260, 220}; color=pal::ACCENT; numIn=0; numOut=1; break;
            case BlockType::AudioOut: title="Audio Out"; sz={260, 140}; color=pal::PURPLE; numIn=1; numOut=0; break;
            case BlockType::Filter:   title="Filter"; sz={210, 220}; color=pal::ORANGE; numIn=1; numOut=1; break;
            case BlockType::Amp:      title="Amplifier"; sz={175, 95}; color=pal::GREEN; numIn=1; numOut=1; break;
            case BlockType::Mixer:    title="Mixer"; sz={180, 160}; color=pal::TEAL; numIn=3; numOut=1; break;
            case BlockType::Scope:    title="Scope"; sz={260, 290}; color=pal::GREEN; numIn=1; numOut=0; break;
            case BlockType::Spectrum: title="Spectrum"; sz={320, 300}; color=pal::ACCENT; numIn=1; numOut=0; break;
            case BlockType::Osc:      title="Oscillator"; sz={220, 270}; color=pal::RED; numIn=0; numOut=1; break;
            case BlockType::Delay:    title="Delay"; sz={200, 120}; color=pal::PURPLE; numIn=1; numOut=1; break;
            case BlockType::Playlist: title="Playlist"; sz={320, 420}; color=pal::ORANGE; numIn=0; numOut=1; break;
        }

        float px = 60.f + (float)((id-1) % 4) * 260.f;
        float py = 60.f + (float)(((id-1) / 4) % 3) * 280.f;

        auto inCol = ui.Column("inc_" + bid, 6.f, 2.f).W(18.f).GrowH(100.f).AlignChildrenH(Align::Center);
        auto outCol = ui.Column("outc_" + bid, 6.f, 2.f).W(18.f).GrowH(100.f).AlignChildrenH(Align::Center);
        auto content = ui.Column("cnt_" + bid, 4.f, 4.f).GrowW(100.f).GrowH(100.f);

        auto row = ui.Row("row_" + bid).GrowW(100.f).GrowH(100.f).Children(inCol, content, outCol);
        auto popup = ui.Popup("blk_" + bid, title)
            .Fixed(SDL::UI::Value::Px(px), SDL::UI::Value::Px(py))
            .W(SDL::UI::Value::Px(sz.x)).H(SDL::UI::Value::Px(sz.y))
            .PopupResizable(true).Padding(4.f)
            .WithStyle([color](SDL::UI::Style& s) { s.bdColor = color; s.bgPressedColor = color; })
            .Child(row).AttachTo(m_rootId);

        SDL::ECS::EntityId eId = popup.Id();
        blocks.push_back(eId);

        DSPNode node;
        node.id = id;
        node.type = type;
        for (int i=0; i<numIn; ++i) {
            node.inputs.push_back({PortDir::In, nullptr});
            auto c = ui.Connector("ci_" + std::to_string(i) + "_" + bid, ConnectorDir::In).W(14.f).H(14.f);
            if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(c.Id())) { cd->blockId = id; cd->portIdx = i; cd->color = pal::PORT_IN; }
            node.uiInPorts.push_back(c.Id()); inCol.Child(c);
        }
        for (int i=0; i<numOut; ++i) {
            node.outputs.push_back({PortDir::Out, std::make_shared<AudioBus>(node.bufferSize)});
            auto c = ui.Connector("co_" + std::to_string(i) + "_" + bid, ConnectorDir::Out).W(14.f).H(14.f);
            if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(c.Id())) { cd->blockId = id; cd->portIdx = i; cd->color = pal::PORT_OUT; }
            node.uiOutPorts.push_back(c.Id()); outCol.Child(c);
        }
        ecs.Add<DSPNode>(eId, std::move(node));

        auto b = ui.GetBuilder(content.Id());
        
        switch(type) {
            case BlockType::AudioIn: {
                AudioInState st;
                
                auto togIn = ui.Toggle("togIn_" + bid, "On").OnToggle([this, eId](bool v) {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* s = ecs.Get<AudioInState>(eId)) s->enabled = v;
                });
                ui.SetChecked(togIn.Id(), true);

                st.cbDev = ui.ComboBox("dev_" + bid, m_recNames, st.selDeviceIdx).OnChange<float>([this, eId](float v) {
                    if (v < 0) return;
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* s = ecs.Get<AudioInState>(eId)) { s->selDeviceIdx = (int)v; _OpenAudioIn(*ecs.Get<DSPNode>(eId), *s); }
                }).GrowW(100.f).Id();
                
                std::vector<std::string> srs; for(auto s : kSampleRates) srs.push_back(std::to_string(s) + " Hz");
                auto cbSR = ui.ComboBox("sr_" + bid, srs, 3).OnChange<float>([this, eId](float v) {
                    if (v < 0) return;
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* n = ecs.Get<DSPNode>(eId)) { n->sampleRate = kSampleRates[(int)v]; _OpenAudioIn(*n, *ecs.Get<AudioInState>(eId)); }
                }).GrowW(100.f).Id();
                
                std::vector<std::string> szs; for(auto s : kBufferSizes) szs.push_back(std::to_string(s));
                auto cbSz = ui.ComboBox("sz_" + bid, szs, 2).OnChange<float>([this, eId](float v) {
                    if (v < 0) return;
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* n = ecs.Get<DSPNode>(eId)) { n->bufferSize = kBufferSizes[(int)v]; _OpenAudioIn(*n, *ecs.Get<AudioInState>(eId)); }
                }).GrowW(100.f).Id();

                auto rowT = ui.Row("rt_" + bid, 8.f).AlignChildrenV(Align::Center).Children(ui.Label("lbT_" + bid, "Device:").FontSize(10.f), togIn);

                b.Children(rowT, ui.GetBuilder(st.cbDev),
                           ui.Label("srl_" + bid, "Sample Rate:").FontSize(10.f), ui.GetBuilder(cbSR), 
                           ui.Label("szl_" + bid, "Buffer Size:").FontSize(10.f), ui.GetBuilder(cbSz));
                
                _OpenAudioIn(*ecs.Get<DSPNode>(eId), st);
                ecs.Add<AudioInState>(eId, std::move(st));
                break;
            }
            case BlockType::AudioOut: {
                AudioOutState st;
                
                auto togOut = ui.Toggle("togOut_" + bid, "On").OnToggle([this, eId](bool v) {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* s = ecs.Get<AudioOutState>(eId)) s->enabled = v;
                });
                ui.SetChecked(togOut.Id(), true);

                st.cbDev = ui.ComboBox("dev_" + bid, m_playNames, st.selDeviceIdx).OnChange<float>([this, eId](float v) {
                    if (v < 0) return;
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* s = ecs.Get<AudioOutState>(eId)) { s->selDeviceIdx = (int)v; _OpenAudioOut(*ecs.Get<DSPNode>(eId), *s); }
                }).GrowW(100.f).Id();

                auto rowT = ui.Row("rt_" + bid, 8.f).AlignChildrenV(Align::Center).Children(ui.Label("lbT_" + bid, "Device:").FontSize(10.f), togOut);

                b.Children(rowT, ui.GetBuilder(st.cbDev));
                
                _OpenAudioOut(*ecs.Get<DSPNode>(eId), st);
                ecs.Add<AudioOutState>(eId, std::move(st));
                break;
            }
            case BlockType::Filter: {
                FilterState st;
                auto lpr = ui.Radio("lp_" + bid, "fm_" + bid, "Low-Pass").OnToggle([this, eId](bool v){ if(v) ecs.Get<FilterState>(eId)->mode=FilterMode::LowPass; });
                auto hpr = ui.Radio("hp_" + bid, "fm_" + bid, "High-Pass").OnToggle([this, eId](bool v){ if(v) ecs.Get<FilterState>(eId)->mode=FilterMode::HighPass; });
                ui.SetChecked(lpr.Id(), true);
                st.lblCutoff = ui.Label("cl_" + bid, "Cutoff: 1000 Hz").FontSize(9.f).Id();
                auto cut = ui.Slider("sl_" + bid, 0.f, 1.f, 0.5f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v){ ecs.Get<FilterState>(eId)->cutoffHz = 20.f * std::pow(1000.f, v); });
                b.Children(lpr, hpr, ui.GetBuilder(st.lblCutoff), cut);
                ecs.Add<FilterState>(eId, std::move(st));
                break;
            }
            case BlockType::Delay: {
                DelayState st;
                st.lblMs = ui.Label("msl_" + bid, "Delay: 100 ms").FontSize(10.f).Id();
                auto sl = ui.Slider("mss_" + bid, 0.f, 2000.f, 100.f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v){ ecs.Get<DelayState>(eId)->delayMs = v; });
                b.Children(ui.GetBuilder(st.lblMs), sl);
                ecs.Add<DelayState>(eId, std::move(st));
                break;
            }
            case BlockType::Spectrum: {
                SpectrumState st;
                auto winC = ui.ComboBox("win_" + bid, {"Rectangular", "Hann", "Hamming", "Blackman"}, st.winIdx)
                    .OnChange<float>([this, eId](float v){ if(v>=0) ecs.Get<SpectrumState>(eId)->winIdx = (int)v; });
                auto minF = ui.InputValue<float>("minF_" + bid, st.minF, 0.f, 20000.f)
                    .OnChange<float>([this, eId](float v){ std::lock_guard<std::mutex> lk(m_audioMutex); ecs.Get<SpectrumState>(eId)->minF = v; });
                auto maxF = ui.InputValue<float>("maxF_" + bid, st.maxF, 0.f, 20000.f)
                    .OnChange<float>([this, eId](float v){ std::lock_guard<std::mutex> lk(m_audioMutex); ecs.Get<SpectrumState>(eId)->maxF = v; });
                
                auto graph = ui.GradedGraph("grph_" + bid).GrowW(100.f).GrowH(100.f);
                st.graph = graph.Id();
                b.Children(ui.Row("rt1_" + bid).Children(ui.Label("WL_" + bid,"Window:").FontSize(10.f), winC),
                           ui.Row("rt2_" + bid).Children(ui.Label("MinF_" + bid,"MinHz:").FontSize(10.f), minF, ui.Label("MaxF_" + bid,"MaxHz:").FontSize(10.f), maxF),
                           graph);
                ecs.Add<SpectrumState>(eId, std::move(st));
                break;
            }
            case BlockType::Scope: {
                ScopeState st;

                // ── Canvas ──────────────────────────────────────────────────
                auto cv = ui.Canvas("cvs_" + bid, nullptr, nullptr,
                [this, eId](SDL::RendererRef r, SDL::FRect rect) {
                    constexpr int kCols = 10, kRows = 8;
                    const float cx = rect.x, cy = rect.y, cw = rect.w, ch = rect.h;
                    const float midY = cy + ch * 0.5f, midX = cx + cw * 0.5f;

                    // Background
                    r.SetDrawColor({10, 13, 22, 255}); r.RenderFillRect(rect);

                    // Grid
                    for (int i = 0; i <= kCols; ++i) {
                        float x = cx + cw * (float)i / kCols;
                        bool center = (i == kCols / 2);
                        r.SetDrawColor(center ? SDL::Color{40,50,75,255} : SDL::Color{20,24,38,255});
                        r.RenderLine({x, cy}, {x, cy + ch});
                        // Sub-ticks on center horizontal
                        r.SetDrawColor({30, 36, 55, 255});
                        for (int j = 1; j < 5; ++j) {
                            float sx = cx + cw * ((float)(i * 5 + j) / (kCols * 5));
                            r.RenderLine({sx, midY - 2.f}, {sx, midY + 2.f});
                        }
                    }
                    for (int i = 0; i <= kRows; ++i) {
                        float y = cy + ch * (float)i / kRows;
                        bool center = (i == kRows / 2);
                        r.SetDrawColor(center ? SDL::Color{40,50,75,255} : SDL::Color{20,24,38,255});
                        r.RenderLine({cx, y}, {cx + cw, y});
                        // Sub-ticks on center vertical
                        r.SetDrawColor({30, 36, 55, 255});
                        for (int j = 1; j < 5; ++j) {
                            float sy = cy + ch * ((float)(i * 5 + j) / (kRows * 5));
                            r.RenderLine({midX - 2.f, sy}, {midX + 2.f, sy});
                        }
                    }

                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    auto* st = ecs.Get<ScopeState>(eId);
                    if (!st) return;

                    // Trigger level line (dashed orange)
                    if (st->trigMode != TriggerMode::Free) {
                        float ty = SDL::Clamp(midY - st->trigLevel * st->vScale * ch * 0.5f, cy, cy + ch);
                        r.SetDrawColor({230, 145, 30, 200});
                        for (float x = cx; x < cx + cw - 1.f; x += 8.f)
                            r.RenderLine({x, ty}, {SDL::Min(x + 5.f, cx + cw - 1.f), ty});
                        // Edge marker (arrow-like)
                        r.SetDrawColor({230, 145, 30, 255});
                        float dir = (st->trigMode == TriggerMode::Rising) ? -4.f : 4.f;
                        r.RenderLine({cx,      ty},       {cx + 6.f, ty});
                        r.RenderLine({cx + 6.f, ty},      {cx + 6.f, ty + dir});
                        r.RenderLine({cx + 6.f, ty + dir},{cx + 12.f, ty + dir});
                    }

                    // Waveform
                    if (!st->displayData.empty()) {
                        r.SetDrawColor(pal::GREEN);
                        int n = (int)st->displayData.size();
                        float half = ch * 0.48f;
                        float px = cx;
                        float py = SDL::Clamp(midY - st->displayData[0] * st->vScale * half, cy, cy + ch);
                        for (int i = 1; i < n; ++i) {
                            float nx = cx + cw * ((float)i / (float)(n - 1));
                            float ny = SDL::Clamp(midY - st->displayData[i] * st->vScale * half, cy, cy + ch);
                            r.RenderLine({px, py}, {nx, ny});
                            px = nx; py = ny;
                        }
                    }

                    // Border
                    r.SetDrawColor({40, 50, 75, 200}); r.RenderRect(rect);
                }).GrowW(100.f).GrowH(100.f);

                // ── Trigger mode ─────────────────────────────────────────────
                auto trigRow = ui.Row("trow_" + bid, 4.f).GrowW(100.f);
                trigRow.Child(ui.Label("trlbl_" + bid, "Trig:").FontSize(9.f));
                for (int i = 0; i < 3; ++i) {
                    auto rb = ui.Radio("trg" + std::to_string(i) + "_" + bid, "tgrp_" + bid, kTrigModeLabel[i])
                        .FontSize(9.f)
                        .OnToggle([this, eId, i](bool v) {
                            if (!v) return;
                            std::lock_guard<std::mutex> lk(m_audioMutex);
                            if (auto* s = ecs.Get<ScopeState>(eId)) {
                                s->trigMode  = (TriggerMode)i;
                                s->trigArmed = true;
                                s->collecting = false;
                                s->collectCount = 0;
                            }
                        });
                    if (i == 0) ui.SetChecked(rb.Id(), true);
                    trigRow.Child(rb);
                }

                // ── Trigger level ─────────────────────────────────────────────
                st.lblTrigLevel = ui.Label("tll_" + bid, "Level: 0.00").FontSize(9.f).Id();
                auto trigSlider = ui.Slider("tls_" + bid, -1.f, 1.f, 0.f, 0.005f).GrowW(100.f)
                    .OnChange<float>([this, eId](float v) {
                        {
                            std::lock_guard<std::mutex> lk(m_audioMutex);
                            if (auto* s = ecs.Get<ScopeState>(eId)) s->trigLevel = v;
                        }
                        if (auto* s = ecs.Get<ScopeState>(eId))
                            ui.SetText(s->lblTrigLevel, std::format("Level: {:+.2f}", v));
                    });

                // ── V-Scale ───────────────────────────────────────────────────
                st.lblVScale = ui.Label("vsl_" + bid, "V-Scale: ×1.00").FontSize(9.f).Id();
                auto vScaleSlider = ui.Slider("vss_" + bid, 0.1f, 4.f, 1.f, 0.05f).GrowW(100.f)
                    .OnChange<float>([this, eId](float v) {
                        {
                            std::lock_guard<std::mutex> lk(m_audioMutex);
                            if (auto* s = ecs.Get<ScopeState>(eId)) s->vScale = v;
                        }
                        if (auto* s = ecs.Get<ScopeState>(eId))
                            ui.SetText(s->lblVScale, std::format("V-Scale: ×{:.2f}", v));
                    });

                b.Children(cv, trigRow,
                           ui.GetBuilder(st.lblTrigLevel), trigSlider,
                           ui.GetBuilder(st.lblVScale),    vScaleSlider);
                ecs.Add<ScopeState>(eId, std::move(st));
                break;
            }
            case BlockType::Amp: {
                AmpState st;
                st.lblGain = ui.Label("gl_" + bid, "Gain: 1.00x").FontSize(9.f).Id();
                auto sl = ui.Slider("sl_" + bid, 0.f, 5.f, 1.f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v){ ecs.Get<AmpState>(eId)->gain = v; });
                b.Children(ui.GetBuilder(st.lblGain), sl);
                ecs.Add<AmpState>(eId, std::move(st));
                break;
            }
            case BlockType::Mixer: {
                MixerState st;
                auto addr = ui.Radio("add_" + bid, "mx_" + bid, "Add").OnToggle([this, eId](bool v){ if(v) ecs.Get<MixerState>(eId)->mode=MixMode::Add; });
                auto mulr = ui.Radio("mul_" + bid, "mx_" + bid, "Multiply").OnToggle([this, eId](bool v){ if(v) ecs.Get<MixerState>(eId)->mode=MixMode::Multiply; });
                auto avgr = ui.Radio("avg_" + bid, "mx_" + bid, "Average").OnToggle([this, eId](bool v){ if(v) ecs.Get<MixerState>(eId)->mode=MixMode::Average; });
                ui.SetChecked(addr.Id(), true);
                b.Children(addr, mulr, avgr);
                ecs.Add<MixerState>(eId, std::move(st));
                break;
            }
            case BlockType::Osc: {
                OscState st;
                auto row = ui.Row("r_" + bid).GrowW(100.f);
                for(int i=0; i<5; ++i) {
                    auto r = ui.Radio("os" + std::to_string(i) + "_" + bid, "grp_" + bid, kOscShapeLabel[i]).OnToggle([this, eId, i](bool v){ if(v) ecs.Get<OscState>(eId)->shape=(OscShape)i; });
                    if(i==0) ui.SetChecked(r.Id(), true);
                    row.Child(r);
                }
                st.lblFreq = ui.Label("fl_" + bid, "Freq: 440 Hz").FontSize(9.f).Id();
                auto slF = ui.Slider("sf_" + bid, 0.f, 1.f, 0.5f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v){ ecs.Get<OscState>(eId)->freq = 20.f * std::pow(1000.f, v); });
                st.lblAmp = ui.Label("al_" + bid, "Amp: 0.50").FontSize(9.f).Id();
                auto slA = ui.Slider("sa_" + bid, 0.f, 1.f, 0.5f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v){ ecs.Get<OscState>(eId)->amp = v; });
                b.Children(row, ui.GetBuilder(st.lblFreq), slF, ui.GetBuilder(st.lblAmp), slA);
                ecs.Add<OscState>(eId, std::move(st));
                break;
            }
            case BlockType::Playlist: {
                PlaylistState st;
                st.uiExplorerPopup = _CreateFileExplorer(eId);
                
                auto btnAdd = ui.Button("addBtn_" + bid, "Add...").GrowW(1.f).OnClick([this, eId]() {
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        ui.SetPopupOpen(ps->uiExplorerPopup, true);
                    }
                });

                auto btnClear = ui.Button("clrBtn_" + bid, "Clear").GrowW(1.f).OnClick([this, eId]() {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        ps->tracks.clear();
                        ps->isPlaying = false;
                        ps->currentAudioData.clear();
                        if (auto* ld = ui.GetListBoxData(ps->uiList)) {
                            ld->items.clear();
                            ld->selectedIndex = -1;
                        }
                    }
                });

                auto btnRem = ui.Button("remBtn_" + bid, "Remove").GrowW(1.f).OnClick([this, eId]() {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        if (auto* ld = ui.GetListBoxData(ps->uiList)) {
                            int sel = ld->selectedIndex;
                            if (sel >= 0 && sel < (int)ps->tracks.size()) {
                                ps->tracks.erase(ps->tracks.begin() + sel);
                                ld->items.erase(ld->items.begin() + sel);
                                if (ps->currentTrack == sel) {
                                    ps->isPlaying = false;
                                    ps->currentAudioData.clear();
                                    ps->currentTrack = -1;
                                } else if (ps->currentTrack > sel) {
                                    ps->currentTrack--;
                                }
                                ld->selectedIndex = -1;
                                ui.MarkLayoutDirty();
                            }
                        }
                    }
                });

                auto rowTop = ui.Row("rTop_" + bid, 2.f).GrowW(100.f).Children(btnAdd, btnClear, btnRem);
                
                auto lst = ui.ListBoxWidget("plist_" + bid).GrowW(100.f).GrowH(100.f);
                st.uiList = lst.Id();

                auto btnPlay = ui.Button("play_" + bid, "▶").W(30.f).OnClick([this, eId]() {
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        int sel = -1;
                        if (auto* ld = ui.GetListBoxData(ps->uiList)) sel = ld->selectedIndex;
                        if (sel == -1 && !ps->tracks.empty()) sel = 0;
                        if (sel != -1) ps->pendingTrackToLoad = sel;
                    }
                });

                auto btnStop = ui.Button("stop_" + bid, "■").W(30.f).OnClick([this, eId]() {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        ps->isPlaying = false;
                        ps->playbackPos = 0;
                    }
                });

                auto togAuto = ui.Toggle("auto_" + bid, "Autoplay").OnToggle([this, eId](bool v) {
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) ps->isAutoplay = v;
                });
                ui.SetChecked(togAuto.Id(), true);

                auto togLoop = ui.Toggle("loop_" + bid, "Loop").OnToggle([this, eId](bool v) {
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) ps->isLooping = v;
                });

                auto rowCtrl = ui.Row("rCtrl_" + bid, 2.f).GrowW(100.f).AlignChildrenV(Align::Center).Children(btnPlay, btnStop, togAuto, togLoop);

                auto slProg = ui.Slider("prog_" + bid, 0.f, 1.f, 0.f, 0.01f).GrowW(100.f).OnChange<float>([this, eId](float v) {
                    std::lock_guard<std::mutex> lk(m_audioMutex);
                    if (auto* ps = ecs.Get<PlaylistState>(eId)) {
                        ps->playbackPos = (size_t)(v * ps->currentAudioData.size());
                    }
                });
                st.uiSlider = slProg.Id();

                auto lblTime = ui.Label("time_" + bid, "00:00 / 00:00").FontSize(10.f);
                st.uiTimeLbl = lblTime.Id();

                auto rowProg = ui.Row("rProg_" + bid, 6.f).GrowW(100.f).AlignChildrenV(Align::Center).Children(slProg, lblTime);

                b.Children(rowTop, lst, rowCtrl, rowProg);
                ecs.Add<PlaylistState>(eId, std::move(st));
                break;
            }
        }
    }

    DSPNode* FindNodeById(int id, SDL::ECS::EntityId* outEId = nullptr) {
        for (auto e : blocks) {
            if (auto* node = ecs.Get<DSPNode>(e)) {
                if (node->id == id) {
                    if (outEId) *outEId = e;
                    return node;
                }
            }
        }
        return nullptr;
    }

    void Connect(SDL::ECS::EntityId fromE, int fromP, SDL::ECS::EntityId toE, int toP) {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        if (fromE == toE) return;
        for (auto& c : connections) if (c.toNode == toE && c.toPort == toP) return;
        connections.push_back({fromE, fromP, toE, toP});
        _RebuildBuses();
    }

    void Disconnect(SDL::ECS::EntityId toE, int toP) {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        connections.erase(std::remove_if(connections.begin(), connections.end(),
            [toE, toP](const Connection& c){ return c.toNode == toE && c.toPort == toP; }), connections.end());
        _RebuildBuses();
    }

    void _RebuildBuses() {
        for (auto e : blocks)
            if (auto* n = ecs.Get<DSPNode>(e))
                for (auto& p : n->inputs) p.bus = nullptr;
            
        for (auto& c : connections) {
            auto* from = ecs.Get<DSPNode>(c.fromNode);
            auto* to   = ecs.Get<DSPNode>(c.toNode);
            if (from && to && c.fromPort < (int)from->outputs.size() && c.toPort < (int)to->inputs.size()) {
                to->inputs[c.toPort].bus = from->outputs[c.fromPort].bus;
            }
        }
        
        for (auto e : blocks) {
            if (auto* n = ecs.Get<DSPNode>(e)) {
                for (auto pid : n->uiInPorts) if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(pid)) cd->connected = false;
                for (auto pid : n->uiOutPorts) if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(pid)) cd->connected = false;
            }
        }
        for (auto& c : connections) {
            auto* from = ecs.Get<DSPNode>(c.fromNode);
            auto* to   = ecs.Get<DSPNode>(c.toNode);
            if (from && to) {
                if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(from->uiOutPorts[c.fromPort])) cd->connected = true;
                if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(to->uiInPorts[c.toPort])) cd->connected = true;
            }
        }
    }

    void _ProcessGraph() {
        // Remarque : cette fonction est toujours appelée avec m_audioMutex verrouillé par la lambda du thread

        std::unordered_map<SDL::ECS::EntityId, std::vector<SDL::ECS::EntityId>> adj;
        for (auto e : blocks) adj[e] = {};
        for (auto& c : connections) adj[c.fromNode].push_back(c.toNode);

        std::unordered_set<SDL::ECS::EntityId> visited;
        std::vector<SDL::ECS::EntityId> sorted;
        std::function<void(SDL::ECS::EntityId)> dfs = [&](SDL::ECS::EntityId e) {
            if (visited.count(e)) return;
            visited.insert(e);
            for (auto next : adj[e]) dfs(next);
            sorted.push_back(e);
        };
        for (auto e : blocks) dfs(e);
        std::reverse(sorted.begin(), sorted.end());

        for (auto e : sorted) {
            auto* node = ecs.Get<DSPNode>(e);
            if (!node) continue;
            for (auto& out : node->outputs) out.bus->Resize(node->bufferSize);

            switch (node->type) {
                case BlockType::AudioIn: {
                    auto* st = ecs.Get<AudioInState>(e);
                    auto& bus = *node->outputs[0].bus;
                    if (!st || !st->enabled || !st->stream) { bus.Clear(); break; }
                    if (st->stream.GetAvailable() >= node->bufferSize * (int)sizeof(float)) {
                        st->stream.GetData(std::span<float>{bus.samples.data(), bus.samples.size()});
                        bus.valid = true;
                    } else bus.Clear();
                    break;
                }
                case BlockType::AudioOut: {
                    auto* st = ecs.Get<AudioOutState>(e);
                    auto* inBus = node->inputs[0].bus.get();
                    if (st && st->enabled && st->stream && inBus && inBus->valid) {
                        if (st->stream.GetQueued() < node->sampleRate * 0.1f * sizeof(float)) {
                            st->stream.PutData(std::span<const float>{inBus->samples.data(), inBus->samples.size()});
                            inBus->valid = false; // marque comme consommé pour synchroniser les sources
                        }
                    }
                    break;
                }
                case BlockType::Filter: {
                    auto* st = ecs.Get<FilterState>(e);
                    auto& out = *node->outputs[0].bus;
                    auto* inBus = node->inputs[0].bus.get();
                    if (!st || !inBus || !inBus->valid) { out.Clear(); break; }
                    const float fc = SDL::Clamp(st->cutoffHz, 20.f, node->sampleRate * 0.49f);
                    const float w0 = 2.f * SDL::PI_F * fc / node->sampleRate;
                    const float cosW = SDL::Cos(w0), sinW = SDL::Sin(w0);
                    const float alpha = sinW / (2.f * st->q);
                    const float a0inv = 1.f / (1.f + alpha);
                    float b0, b1, b2, a1, a2;
                    if (st->mode == FilterMode::LowPass) {
                        b0 = (1.f - cosW) * 0.5f * a0inv; b1 = (1.f - cosW) * a0inv; b2 = b0; a1 = -2.f * cosW * a0inv; a2 = (1.f - alpha) * a0inv;
                    } else if (st->mode == FilterMode::HighPass) {
                        b0 = (1.f + cosW) * 0.5f * a0inv; b1 = -(1.f + cosW) * a0inv; b2 = b0; a1 = -2.f * cosW * a0inv; a2 = (1.f - alpha) * a0inv;
                    } else {
                        b0 = a0inv; b1 = -2.f * cosW * a0inv; b2 = b0; a1 = b1; a2 = (1.f - alpha) * a0inv;
                    }
                    for (int i = 0; i < node->bufferSize; ++i)
                        out.samples[i] = SDL::detail::BiQuadStep(inBus->samples[i], b0,b1,b2,a1,a2, st->bq);
                    out.valid = true;
                    break;
                }
                case BlockType::Delay: {
                    auto* st = ecs.Get<DelayState>(e);
                    auto& out = *node->outputs[0].bus;
                    auto* inBus = node->inputs[0].bus.get();
                    if (!st || !inBus || !inBus->valid) { out.Clear(); break; }
                    int dSz = (int)(st->delayMs / 1000.f * node->sampleRate);
                    if (dSz <= 0) { out.samples = inBus->samples; out.valid = true; break; }
                    if ((int)st->buffer.size() != dSz) { st->buffer.assign(dSz, 0.f); st->writeIdx = 0; }
                    for (int i = 0; i < node->bufferSize; ++i) {
                        float s = inBus->samples[i];
                        out.samples[i] = st->buffer[st->writeIdx] * 0.6f + s;
                        st->buffer[st->writeIdx] = s + out.samples[i] * 0.3f;
                        st->writeIdx = (st->writeIdx + 1) % dSz;
                    }
                    out.valid = true;
                    break;
                }
                case BlockType::Amp: {
                    auto* st = ecs.Get<AmpState>(e);
                    auto& out = *node->outputs[0].bus;
                    auto* inBus = node->inputs[0].bus.get();
                    if (!st || !inBus || !inBus->valid) { out.Clear(); break; }
                    for (int i = 0; i < node->bufferSize; ++i) out.samples[i] = inBus->samples[i] * st->gain;
                    out.valid = true;
                    break;
                }
                case BlockType::Mixer: {
                    auto* st = ecs.Get<MixerState>(e);
                    auto& out = *node->outputs[0].bus;
                    if (!st) break;
                    std::fill(out.samples.begin(), out.samples.end(), st->mode == MixMode::Multiply ? 1.f : 0.f);
                    int active = 0;
                    for (auto& p : node->inputs) {
                        if (!p.bus || !p.bus->valid) continue;
                        ++active;
                        for (int i = 0; i < node->bufferSize; ++i) {
                            if (st->mode == MixMode::Add) out.samples[i] += p.bus->samples[i];
                            else if (st->mode == MixMode::Multiply) out.samples[i] *= p.bus->samples[i];
                            else out.samples[i] += p.bus->samples[i];
                        }
                    }
                    if (st->mode == MixMode::Average && active > 0)
                        for (float& s : out.samples) s /= active;
                    out.valid = active > 0;
                    break;
                }
                case BlockType::Scope: {
                    auto* st = ecs.Get<ScopeState>(e);
                    auto* inBus = node->inputs[0].bus.get();
                    if (!st || !inBus || !inBus->valid) break;

                    const auto& src = inBus->samples;
                    int dispSz = node->bufferSize;

                    if (st->trigMode == TriggerMode::Free) {
                        st->displayData.assign(src.begin(), src.end());
                        break;
                    }

                    if ((int)st->collectBuf.size() != dispSz)
                        st->collectBuf.assign(dispSz, 0.f);

                    for (float s : src) {
                        if (st->collecting) {
                            st->collectBuf[st->collectCount++] = s;
                            if (st->collectCount >= dispSz) {
                                st->displayData = st->collectBuf;
                                st->collecting  = false;
                                st->trigArmed   = true;
                                st->collectCount = 0;
                            }
                        } else if (st->trigArmed) {
                            bool trig = (st->trigMode == TriggerMode::Rising)
                                ? (st->prevSample < st->trigLevel && s >= st->trigLevel)
                                : (st->prevSample > st->trigLevel && s <= st->trigLevel);
                            if (trig) {
                                st->collectBuf[0] = s;
                                st->collectCount  = 1;
                                st->collecting    = true;
                                st->trigArmed     = false;
                            }
                        }
                        st->prevSample = s;
                    }
                    break;
                }
                case BlockType::Spectrum: {
                    auto* st = ecs.Get<SpectrumState>(e);
                    auto* inBus = node->inputs[0].bus.get();
                    if (!st || !inBus || !inBus->valid) break;
                    std::vector<float> buf(inBus->samples.begin(), inBus->samples.end());
                    std::vector<float> win;
                    if (st->winIdx == 1) win = SDL::detail::WindowHanning((int)buf.size());
                    else if (st->winIdx == 2) win = SDL::detail::WindowHamming((int)buf.size());
                    else if (st->winIdx == 3) win = SDL::detail::WindowBlackman((int)buf.size());
                    else win = SDL::detail::WindowRectangular((int)buf.size());
                    
                    for (int i = 0; i < (int)buf.size(); ++i) buf[i] *= win[i];
                    SDL::Signal sig{std::move(buf), (float)node->sampleRate};
                    auto fft = SDL::FFT(sig);
                    auto db  = SDL::FFTMagnitudeDB(fft);
                    
                    st->displayData.clear();
                    int outSz = (int)db.size() / 2;
                    float freqStep = (float)node->sampleRate / (outSz * 2.f);
                    for (int k = 0; k < outSz; ++k) {
                        float f = k * freqStep;
                        if (f >= st->minF && f <= st->maxF) st->displayData.push_back(SDL::Max(-80.f, db[k]));
                    }
                    break;
                }
                case BlockType::Osc: {
                    auto* st = ecs.Get<OscState>(e);
                    auto& out = *node->outputs[0].bus;
                    if (!st) break;
                    if (out.valid) break; // buffer précédent pas encore consommé par AudioOut

                    // Temps de lecture en nanosecondes (base de temps SDL exacte)
                    // Chaque sample avance de 1 000 000 000 / sampleRate ns
                    const uint64_t nsPerBuffer = (uint64_t)node->bufferSize * 1'000'000'000ULL
                                                  / (uint64_t)node->sampleRate;
                    const double twoPi  = 2.0 * M_PI;
                    const double nsToSec = 1e-9;

                    for (int i = 0; i < node->bufferSize; ++i) {
                        uint64_t sampleNs = st->playbackNs
                                          + (uint64_t)i * 1'000'000'000ULL / (uint64_t)node->sampleRate;
                        double t  = (double)sampleNs * nsToSec;
                        double ph = std::fmod((double)st->freq * t, 1.0) * twoPi;
                        float s = 0.f;
                        if (st->shape == OscShape::Sine)          s = (float)std::sin(ph);
                        else if (st->shape == OscShape::Square)   s = ph < M_PI ? 1.f : -1.f;
                        else if (st->shape == OscShape::Triangle) s = (float)(1.0 - 4.0 * std::abs(ph / twoPi - 0.5));
                        else if (st->shape == OscShape::Sawtooth) s = (float)(ph / M_PI - 1.0);
                        else s = (float)std::rand() / (float)RAND_MAX * 2.f - 1.f;
                        out.samples[i] = st->amp * s;
                    }
                    st->playbackNs += nsPerBuffer;
                    out.valid = true;
                    break;
                }
                case BlockType::Playlist: {
                    auto* st = ecs.Get<PlaylistState>(e);
                    auto& out = *node->outputs[0].bus;

                    if (!st || !st->isPlaying) { out.Clear(); break; }
                    if (out.valid) break; // buffer précédent pas encore consommé par AudioOut

                    if (!st->currentAudioData.empty()) {
                        int toCopy = node->bufferSize;
                        int available = (int)st->currentAudioData.size() - (int)st->playbackPos;
                        
                        if (available < toCopy) {
                            if (available > 0) {
                                std::copy(st->currentAudioData.begin() + st->playbackPos, 
                                          st->currentAudioData.end(), out.samples.begin());
                            }
                            st->playbackPos = st->currentAudioData.size();
                            
                            st->isPlaying = false;
                            if (st->isAutoplay) {
                                int next = st->currentTrack + 1;
                                if (next >= (int)st->tracks.size()) {
                                    if (st->isLooping) next = 0;
                                    else next = -1;
                                }
                                if (next != -1) st->pendingTrackToLoad = next;
                            } else if (st->isLooping) {
                                st->pendingTrackToLoad = st->currentTrack;
                            }
                        } else {
                            std::copy(st->currentAudioData.begin() + st->playbackPos, 
                                      st->currentAudioData.begin() + st->playbackPos + toCopy, 
                                      out.samples.begin());
                            st->playbackPos += toCopy;
                        }
                        out.valid = true;
                    }
                    break;
                }
            }
        }
    }

    void _BuildUI() {
        m_rootId = ui.Container("root").W(SDL::UI::Value::Ww(100.f)).H(SDL::UI::Value::Wh(100.f)).Padding(0.f).AsRoot().Id();

        ui.Canvas("wires", nullptr, nullptr, [this](SDL::RendererRef r, SDL::FRect rect){ 
            r.SetDrawColor(pal::CANVAS); r.RenderFillRect(rect);
            r.SetDrawColor(pal::GRID);
            for (float y = std::fmod(rect.y, 32.f); y < rect.y + rect.h; y += 32.f)
                for (float x = std::fmod(rect.x, 32.f); x < rect.x + rect.w; x += 32.f)
                    r.RenderPoint({x, y});

            std::lock_guard<std::mutex> lock(m_audioMutex);

            for (auto& c : connections) {
                auto* from = ecs.Get<DSPNode>(c.fromNode); auto* to = ecs.Get<DSPNode>(c.toNode);
                if (from && to && c.fromPort < (int)from->uiOutPorts.size() && c.toPort < (int)to->uiInPorts.size()) {
                    auto* cr0 = ecs.Get<SDL::UI::ComputedRect>(from->uiOutPorts[c.fromPort]);
                    auto* cr1 = ecs.Get<SDL::UI::ComputedRect>(to->uiInPorts[c.toPort]);
                    if (cr0 && cr1) {
                        SDL::FPoint p0 = {cr0->screen.x + cr0->screen.w * 0.5f, cr0->screen.y + cr0->screen.h * 0.5f};
                        SDL::FPoint p3 = {cr1->screen.x + cr1->screen.w * 0.5f, cr1->screen.y + cr1->screen.h * 0.5f};
                        float bend = SDL::Clamp(std::abs(p3.x - p0.x) * 0.5f, 40.f, 200.f);
                        r.SetDrawColor(pal::WIRE);
                        
                        std::array<SDL::FPoint, 4> pts = {p0, SDL::FPoint{p0.x + bend, p0.y}, SDL::FPoint{p3.x - bend, p3.y}, p3};
                        r.RenderBezierCurve(pts, 0.01f); // 0.01 = 100 segments
                    }
                }
            }
            if (m_connectId != SDL::ECS::NullEntity) {
                if (auto* cr = ecs.Get<SDL::UI::ComputedRect>(m_connectId)) {
                    SDL::FPoint p0 = {cr->screen.x + cr->screen.w * 0.5f, cr->screen.y + cr->screen.h * 0.5f};
                    float bend = SDL::Clamp(std::abs(mousePos.x - p0.x) * 0.5f, 40.f, 200.f);
                    r.SetDrawColor(pal::WIRE_TMP);
                    
                    std::array<SDL::FPoint, 4> pts = {p0, SDL::FPoint{p0.x + bend, p0.y}, SDL::FPoint{mousePos.x - bend, mousePos.y}, mousePos};
                    r.RenderBezierCurve(pts, 0.01f);
                }
            }
        }).Fixed(SDL::UI::Value::Px(0.f), SDL::UI::Value::Px(0.f)).W(SDL::UI::Value::Ww(100.f)).H(SDL::UI::Value::Wh(100.f)).AttachTo(m_rootId);

        ui.MenuBar("mb")
            .W(SDL::UI::Value::Pw(100.f)).H(28.f)
            .WithStyle([](SDL::UI::Style &s){ s.bgColor = pal::HEADER; s.textColor = pal::WHITE; s.radius = {}; })
            .AddMenu("Add Block", {
                MI("Audio In", "", nullptr, [this]{ AddBlock(BlockType::AudioIn); }),
                MI("Audio Out", "", nullptr, [this]{ AddBlock(BlockType::AudioOut); }),
                MSep(),
                MI("Filter", "", nullptr, [this]{ AddBlock(BlockType::Filter); }),
                MI("Amplifier", "", nullptr, [this]{ AddBlock(BlockType::Amp); }),
                MI("Mixer", "", nullptr, [this]{ AddBlock(BlockType::Mixer); }),
                MI("Delay", "", nullptr, [this]{ AddBlock(BlockType::Delay); }),
                MSep(),
                MI("Oscillator", "", nullptr, [this]{ AddBlock(BlockType::Osc); }),
                MI("Playlist", "", nullptr, [this]{ AddBlock(BlockType::Playlist); }),
                MSep(),
                MI("Scope", "", nullptr, [this]{ AddBlock(BlockType::Scope); }),
                MI("Spectrum", "", nullptr, [this]{ AddBlock(BlockType::Spectrum); }),
            })
            .AddMenu("System", {
                MI("Rescan Devices", "", nullptr, [this]{ _RescanDevices(); }),
            })
            .AttachTo(m_rootId);
    }

    SDL::ECS::EntityId _HitConnector(SDL::FPoint pos) {
        SDL::ECS::EntityId found = SDL::ECS::NullEntity;
        ecs.Each<SDL::UI::ConnectorData, SDL::UI::ComputedRect>(
            [&](SDL::ECS::EntityId e, SDL::UI::ConnectorData&, SDL::UI::ComputedRect& cr) {
                if (found != SDL::ECS::NullEntity) return;
                float cx = cr.screen.x + cr.screen.w * 0.5f;
                float cy = cr.screen.y + cr.screen.h * 0.5f;
                float dx = pos.x - cx, dy = pos.y - cy;
                if (dx*dx + dy*dy < 144.f) found = e;
            });
        return found;
    }

    SDL::AppResult Iterate() {
        timer.Begin();
        rm.UpdateAll();

        // 1. Processus de chargement audio pour Playlist (en dehors du mutex audio principal pour ne pas ralentir le process)
        ecs.Each<PlaylistState>([&](SDL::ECS::EntityId eId, PlaylistState& ps) {
            if (ps.pendingTrackToLoad != -1) {
                int next = ps.pendingTrackToLoad;
                ps.pendingTrackToLoad = -1;
                
                auto* node = ecs.Get<DSPNode>(eId);
                if (node && next >= 0 && next < (int)ps.tracks.size()) {
                    std::vector<float> samples;
                    if (LoadAudioToFloatMono(ps.tracks[next], node->sampleRate, samples)) {
                        std::lock_guard<std::mutex> lk(m_audioMutex);
                        ps.currentTrack = next;
                        ps.currentAudioData = std::move(samples);
                        ps.playbackPos = 0;
                        ps.durationSec = (float)ps.currentAudioData.size() / node->sampleRate;
                        ps.isPlaying = true;
                        
                        if (auto* ld = ui.GetListBoxData(ps.uiList)) {
                            ld->selectedIndex = next;
                            ui.MarkLayoutDirty();
                        }
                    }
                }
            }
        });

        // 2. Mises à jour UI avec lock sécurisé
        {
            std::lock_guard<std::mutex> lock(m_audioMutex);
            
            ecs.Each<FilterState>([&](SDL::ECS::EntityId, FilterState& st) { ui.SetText(st.lblCutoff, std::format("Cutoff: {:.0f} Hz", st.cutoffHz)); });
            ecs.Each<AmpState>([&](SDL::ECS::EntityId, AmpState& st) { ui.SetText(st.lblGain, std::format("Gain: {:.2f}x", st.gain)); });
            ecs.Each<DelayState>([&](SDL::ECS::EntityId, DelayState& st) { ui.SetText(st.lblMs, std::format("Delay: {:.0f} ms", st.delayMs)); });
            ecs.Each<OscState>([&](SDL::ECS::EntityId, OscState& st) { ui.SetText(st.lblFreq, std::format("Freq: {:.0f} Hz", st.freq)); ui.SetText(st.lblAmp, std::format("Amp: {:.2f}", st.amp)); });
            
            ecs.Each<SpectrumState>([&](SDL::ECS::EntityId, SpectrumState& st) {
                if (st.graph != SDL::ECS::NullEntity && !st.displayData.empty()) {
                    ui.SetGraphData(st.graph, st.displayData);
                    ui.SetGraphRange(st.graph, -80.f, 0.f);
                }
            });

            // Update Playlist Slider & Time
            ecs.Each<PlaylistState>([&](SDL::ECS::EntityId eId, PlaylistState& ps) {
                float currentSec = 0.f;
                auto* node = ecs.Get<DSPNode>(eId);
                if (node && !ps.currentAudioData.empty()) {
                    currentSec = (float)ps.playbackPos / node->sampleRate;
                }
                
                if (ps.durationSec > 0.f) {
                    ui.SetValue(ps.uiSlider, currentSec / ps.durationSec);
                    int cm = (int)currentSec / 60;
                    int cs = (int)currentSec % 60;
                    int dm = (int)ps.durationSec / 60;
                    int ds = (int)ps.durationSec % 60;
                    ui.SetText(ps.uiTimeLbl, std::format("{:02d}:{:02d} / {:02d}:{:02d}", cm, cs, dm, ds));
                } else {
                    ui.SetValue(ps.uiSlider, 0.f);
                    ui.SetText(ps.uiTimeLbl, "00:00 / 00:00");
                }
            });

            std::vector<SDL::ECS::EntityId> toRemove;
            for (auto e : blocks) {
                if (auto* pd = ecs.Get<SDL::UI::PopupData>(e))
                    if (!pd->open) toRemove.push_back(e);
            }
            for (auto e : toRemove) {
                if (auto* ps = ecs.Get<PlaylistState>(e)) {
                    if (ps->uiExplorerPopup != SDL::ECS::NullEntity) {
                        ui.RemoveChild(m_rootId, ps->uiExplorerPopup);
                        ecs.DestroyEntity(ps->uiExplorerPopup);
                    }
                }

                ui.RemoveChild(m_rootId, e);
                ecs.DestroyEntity(e);
                blocks.erase(std::remove(blocks.begin(), blocks.end(), e), blocks.end());
                connections.erase(std::remove_if(connections.begin(), connections.end(),
                    [e](const Connection& c){ return c.fromNode == e || c.toNode == e; }), connections.end());
                _RebuildBuses();
            }
        }

        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(timer.GetDelta());
        renderer.Present();
        timer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_MOUSE_MOTION) mousePos = {ev.motion.x, ev.motion.y};

        if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
            SDL::ECS::EntityId ce = _HitConnector({ev.button.x, ev.button.y});
            if (ce != SDL::ECS::NullEntity) {
                if (auto* cd = ecs.Get<SDL::UI::ConnectorData>(ce)) {
                    if (cd->dir == SDL::UI::ConnectorDir::Out) m_connectId = ce;
                    else {
                        SDL::ECS::EntityId nodeEId;
                        if (FindNodeById(cd->blockId, &nodeEId)) Disconnect(nodeEId, cd->portIdx);
                    }
                }
            }
        }

        if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT) {
            if (m_connectId != SDL::ECS::NullEntity) {
                SDL::ECS::EntityId ce = _HitConnector({ev.button.x, ev.button.y});
                if (ce != SDL::ECS::NullEntity && ce != m_connectId) {
                    auto* cd1 = ecs.Get<SDL::UI::ConnectorData>(m_connectId);
                    auto* cd2 = ecs.Get<SDL::UI::ConnectorData>(ce);
                    if (cd1 && cd2 && cd2->dir == SDL::UI::ConnectorDir::In) {
                        SDL::ECS::EntityId fromE, toE;
                        if (FindNodeById(cd1->blockId, &fromE) && FindNodeById(cd2->blockId, &toE))
                            Connect(fromE, cd1->portIdx, toE, cd2->portIdx);
                    }
                }
                m_connectId = SDL::ECS::NullEntity;
            }
        }

        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)