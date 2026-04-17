/**
 * @file 07_weather.cpp
 * @brief 7-day weather forecast viewer using SDL3pp_ui and SDL3pp_net.
 *
 * Queries the Open-Meteo geocoding and forecast APIs over plain HTTP/1.0.
 * A background thread handles all networking so the UI stays responsive.
 *
 * Controls:
 *   - Type a city name in the search bar and press Enter / click Search.
 *   - If multiple cities match, select one from the suggestions list.
 *   - The main panel shows the current conditions and a 7-day forecast row.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL3PP_ENABLE_NET
#include <SDL3pp/SDL3pp.h>
#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_ttf.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <atomic>
#include <cmath>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
  constexpr SDL::Color BG      = {15,  20,  35, 255};
  constexpr SDL::Color PANEL   = {22,  30,  50, 255};
  constexpr SDL::Color CARD    = {28,  38,  62, 255};
  constexpr SDL::Color HEADER  = {18,  24,  42, 255};
  constexpr SDL::Color ACCENT  = {55, 130, 220, 255};
  constexpr SDL::Color WHITE   = {220,225, 235, 255};
  constexpr SDL::Color GREY    = {130,138, 158, 255};
  constexpr SDL::Color BORDER  = {45,  55,  82, 255};
  constexpr SDL::Color WARM    = {230,150,  50, 255};
  constexpr SDL::Color COLD    = {100,180, 240, 255};
}

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight HTTP/1.0 GET over plain TCP
// ─────────────────────────────────────────────────────────────────────────────
static std::string HttpGet(const std::string& host, const std::string& path)
{
    // Resolve
    SDL::Address addr{host.c_str()};
    if (addr.WaitUntilResolved(8000) != SDL::NET_SUCCESS_STATUS)
        return "";

    // Connect
    SDL::StreamSocket sock{addr, 80};
    if (sock.WaitUntilConnected(8000) != SDL::NET_SUCCESS_STATUS)
        return "";

    // Request
    std::string req =
        "GET " + path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n\r\n";
    if (!sock.Write(req.data(), static_cast<int>(req.size())))
        return "";

    // Read response
    std::string resp;
    char buf[4096];
    while (true) {
        void* vsock = sock.Get();
        int ready = SDL::WaitUntilInputAvailable(&vsock, 1, 5000);
        if (ready <= 0) break;
        int n = sock.Read(buf, static_cast<int>(sizeof buf) - 1);
        if (n <= 0) break;
        resp.append(buf, n);
    }

    // Strip HTTP headers — find "\r\n\r\n"
    auto pos = resp.find("\r\n\r\n");
    if (pos != std::string::npos)
        return resp.substr(pos + 4);
    return resp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tiny JSON helpers (no external library)
// ─────────────────────────────────────────────────────────────────────────────
static std::string JsonStr(const std::string& json,
                           const std::string& key,
                           const std::string& def = "")
{
    // Find "key":"value" or "key":value
    auto search = "\"" + key + "\"";
    auto kpos = json.find(search);
    if (kpos == std::string::npos) return def;
    auto colon = json.find(':', kpos + search.size());
    if (colon == std::string::npos) return def;
    auto start = json.find_first_not_of(" \t\r\n", colon + 1);
    if (start == std::string::npos) return def;
    if (json[start] == '"') {
        auto end = json.find('"', start + 1);
        if (end == std::string::npos) return def;
        return json.substr(start + 1, end - start - 1);
    }
    // Number / true / false
    auto end = json.find_first_of(",}\r\n]", start);
    return json.substr(start, end - start);
}

static double JsonNum(const std::string& json, const std::string& key, double def = 0.0)
{
    auto s = JsonStr(json, key);
    if (s.empty()) return def;
    try { return std::stod(s); } catch (...) { return def; }
}

// Extracts a flat JSON array of numbers/strings after "key":
static std::vector<std::string> JsonArray(const std::string& json,
                                          const std::string& key)
{
    std::vector<std::string> out;
    auto search = "\"" + key + "\"";
    auto kpos = json.find(search);
    if (kpos == std::string::npos) return out;
    auto lb = json.find('[', kpos);
    if (lb == std::string::npos) return out;
    auto rb = json.find(']', lb);
    if (rb == std::string::npos) return out;
    std::string arr = json.substr(lb + 1, rb - lb - 1);
    // Tokenise
    size_t i = 0;
    while (i < arr.size()) {
        i = arr.find_first_not_of(" \t\r\n,", i);
        if (i == std::string::npos) break;
        if (arr[i] == '"') {
            auto e = arr.find('"', i + 1);
            if (e == std::string::npos) break;
            out.push_back(arr.substr(i + 1, e - i - 1));
            i = e + 1;
        } else {
            auto e = arr.find_first_of(",]", i);
            if (e == std::string::npos) e = arr.size();
            out.push_back(arr.substr(i, e - i));
            i = e;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Domain types
// ─────────────────────────────────────────────────────────────────────────────
struct CityResult {
    std::string name;
    std::string country;
    double      lat = 0.0;
    double      lon = 0.0;
    std::string displayName() const { return name + ", " + country; }
};

struct DayForecast {
    std::string date;         ///< "YYYY-MM-DD"
    std::string weekday;      ///< "Mon", "Tue" …
    int         weatherCode = 0;
    double      tempMax = 0.0;
    double      tempMin = 0.0;
    double      precipSum = 0.0;
};

struct WeatherState {
    std::string            cityName;
    double                 currentTemp  = 0.0;
    double                 feelsLike    = 0.0;
    double                 humidity     = 0.0;
    double                 windSpeed    = 0.0;
    int                    weatherCode  = 0;
    std::vector<DayForecast> forecast;  ///< 7 days
};

// ─────────────────────────────────────────────────────────────────────────────
// Weather code → icon key + description
// ─────────────────────────────────────────────────────────────────────────────
static const char* WeatherIcon(int code)
{
    if (code == 0)            return "weather_sun";
    if (code <= 2)            return "weather_partly_cloudy";
    if (code == 3)            return "weather_cloud";
    if (code <= 49)           return "weather_cloud";   // fog/haze
    if (code <= 57)           return "weather_rain";    // drizzle
    if (code <= 67)           return "weather_rain";    // rain
    if (code <= 77)           return "weather_snow";    // snow / grains
    if (code <= 82)           return "weather_rain";    // showers
    if (code <= 86)           return "weather_snow";    // snow showers
    if (code <= 99)           return "weather_storm";   // thunderstorm
    return "weather_cloud";
}

static const char* WeatherDesc(int code)
{
    if (code == 0)  return "Clear sky";
    if (code == 1)  return "Mainly clear";
    if (code == 2)  return "Partly cloudy";
    if (code == 3)  return "Overcast";
    if (code <= 49) return "Foggy";
    if (code <= 57) return "Drizzle";
    if (code <= 67) return "Rain";
    if (code <= 77) return "Snow";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow showers";
    if (code <= 99) return "Thunderstorm";
    return "Unknown";
}

static const char* kWeekdays[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// Parse "YYYY-MM-DD" → weekday name
static std::string DateToWeekday(const std::string& date)
{
    if (date.size() < 10) return "?";
    // Tomohiko Sakamoto's algorithm
    int y = std::stoi(date.substr(0, 4));
    int m = std::stoi(date.substr(5, 2));
    int d = std::stoi(date.substr(8, 2));
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return kWeekdays[dow];
}

// ─────────────────────────────────────────────────────────────────────────────
// Network calls (run on background thread)
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<CityResult> GeocodeCities(const std::string& name)
{
    std::string path =
        "/v1/search?name=" + name + "&count=8&language=en&format=json";
    std::string body = HttpGet("geocoding-api.open-meteo.com", path);

    std::vector<CityResult> results;
    // Find "results":[...]
    auto rpos = body.find("\"results\"");
    if (rpos == std::string::npos) return results;
    auto lb = body.find('[', rpos);
    if (lb == std::string::npos) return results;

    // Walk through objects inside the array
    size_t pos = lb + 1;
    while (pos < body.size()) {
        auto ob = body.find('{', pos);
        if (ob == std::string::npos) break;
        // Find matching close brace
        int depth = 1;
        size_t ce = ob + 1;
        while (ce < body.size() && depth > 0) {
            if      (body[ce] == '{') ++depth;
            else if (body[ce] == '}') --depth;
            ++ce;
        }
        std::string obj = body.substr(ob, ce - ob);
        CityResult r;
        r.name    = JsonStr(obj, "name");
        r.country = JsonStr(obj, "country");
        r.lat     = JsonNum(obj, "latitude");
        r.lon     = JsonNum(obj, "longitude");
        if (!r.name.empty())
            results.push_back(r);
        pos = ce;
        if (body[pos] == ']') break;
    }
    return results;
}

static std::optional<WeatherState> FetchWeather(const CityResult& city)
{
    std::string path = std::format(
        "/v1/forecast?"
        "latitude={:.4f}&longitude={:.4f}"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature"
        ",weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum"
        "&timezone=auto&forecast_days=7",
        city.lat, city.lon);

    std::string body = HttpGet("api.open-meteo.com", path);
    if (body.empty()) return std::nullopt;

    WeatherState ws;
    ws.cityName     = city.displayName();
    ws.currentTemp  = JsonNum(body, "temperature_2m");
    ws.feelsLike    = JsonNum(body, "apparent_temperature");
    ws.humidity     = JsonNum(body, "relative_humidity_2m");
    ws.windSpeed    = JsonNum(body, "wind_speed_10m");
    ws.weatherCode  = static_cast<int>(JsonNum(body, "weather_code"));

    auto dates   = JsonArray(body, "time");
    auto codes   = JsonArray(body, "weather_code");
    auto maxTemps = JsonArray(body, "temperature_2m_max");
    auto minTemps = JsonArray(body, "temperature_2m_min");
    auto precip  = JsonArray(body, "precipitation_sum");

    for (size_t i = 0; i < dates.size() && i < 7; ++i) {
        DayForecast df;
        df.date        = dates[i];
        df.weekday     = DateToWeekday(dates[i]);
        df.weatherCode = (i < codes.size())    ? std::stoi(codes[i])          : 0;
        df.tempMax     = (i < maxTemps.size())  ? std::stod(maxTemps[i])       : 0.0;
        df.tempMin     = (i < minTemps.size())  ? std::stod(minTemps[i])       : 0.0;
        df.precipSum   = (i < precip.size())    ? std::stod(precip[i])         : 0.0;
        ws.forecast.push_back(df);
    }
    return ws;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main application state
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
    static constexpr SDL::Point kWinSz = {1280, 720};

    // ── SDL callbacks ─────────────────────────────────────────────────────────

    static SDL::AppResult Init(Main** out, SDL::AppArgs) {
        SDL::SetAppMetadata("SDL3pp Weather", "1.0", "com.example.weather");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        SDL::NET::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }
    static void Quit(Main* m, SDL::AppResult) {
        delete m;
        SDL::NET::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }

    static SDL::Window MakeWindow() {
        return SDL::CreateWindowAndRenderer(
            "SDL3pp — Weather Forecast",
            kWinSz, SDL_WINDOW_RESIZABLE, nullptr);
    }

    // ── Resources ─────────────────────────────────────────────────────────────

    SDL::Window      window  { MakeWindow()         };
    SDL::RendererRef renderer{ window.GetRenderer() };

    SDL::ResourceManager rm;
    SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };

    SDL::ECS::Context ecs_ctx;
    SDL::UI::System   ui{ ecs_ctx, renderer, {}, pool };

    SDL::FrameTimer m_frameTimer{ 60.f };

    // ── UI entity ids ─────────────────────────────────────────────────────────

    SDL::ECS::EntityId id_searchInput   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_searchBtn     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_statusLabel   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cityList      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_mainPanel     = SDL::ECS::NullEntity;

    // Current-weather panel
    SDL::ECS::EntityId id_cwIcon        = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwCity        = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwTemp        = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwDesc        = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwFeels       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwHumidity    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_cwWind        = SDL::ECS::NullEntity;

    // Forecast day cards (7)
    static constexpr int kDays = 7;
    SDL::ECS::EntityId id_dayPanel[kDays] = {};
    SDL::ECS::EntityId id_dayLabel[kDays] = {};
    SDL::ECS::EntityId id_dayIcon[kDays]  = {};
    SDL::ECS::EntityId id_dayMax[kDays]   = {};
    SDL::ECS::EntityId id_dayMin[kDays]   = {};

    // ── Network thread state ──────────────────────────────────────────────────

    enum class NetOp { None, Geocode, Forecast };
    std::atomic<bool>   m_netBusy{false};
    std::thread         m_netThread;

    // Results posted from network thread
    std::mutex                     m_mtx;
    std::vector<CityResult>        m_pendingCities;
    std::optional<WeatherState>    m_pendingWeather;
    bool                           m_hasPendingCities  = false;
    bool                           m_hasPendingWeather = false;

    std::vector<CityResult> m_cities; ///< last geocode result (UI thread)

    // ── Constructor ───────────────────────────────────────────────────────────

    Main() {
        window.StartTextInput();
        _LoadResources();
        _BuildUI();
        m_frameTimer.Begin();
    }

    ~Main() {
        if (m_netThread.joinable())
            m_netThread.join();
        pool.Release();
    }

    // ── Resource loading ──────────────────────────────────────────────────────

    void _LoadResources() {
        const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("font", base + "fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 14.f);

        const std::string icons = base + "textures/icons/";
        const char* names[] = {
            "weather_sun", "weather_cloud", "weather_partly_cloudy",
            "weather_rain", "weather_storm", "weather_snow",
            "weather_wind", "weather_thermometer", "weather_humidity"
        };
        for (auto n : names)
            ui.LoadTexture(n, icons + "icon_" + std::string(n) + ".png");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI construction
    // ─────────────────────────────────────────────────────────────────────────

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f)).Radius(SDL::FCorners(0.f))
            .Children(_BuildHeader(), _BuildBody())
            .AsRoot();
    }

    SDL::ECS::EntityId _BuildHeader() {
        auto hdr = ui.Row("header", 12.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(56.f)
            .PaddingH(16.f).PaddingV(8.f)
            .BgColor(pal::HEADER)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BorderColor(pal::BORDER);

        hdr.Child(ui.Label("app_title", "Weather Forecast")
            .TextColor(pal::ACCENT).Grow(0).PaddingV(0));

        id_searchInput = ui.Input("search_input", "Search city…")
            .Grow(1).H(36.f)
            .WithStyle([](auto& s){ s.radius = SDL::FCorners(6.f); })
            .OnClick([this]{ /* focus handled by UI system */ });
        hdr.Child(id_searchInput);

        id_searchBtn = ui.Button("search_btn", "Search")
            .W(90.f).H(36.f)
            .Style(SDL::UI::Theme::PrimaryButton(pal::ACCENT))
            .OnClick([this]{ _DoSearch(); });
        hdr.Child(id_searchBtn);

        id_statusLabel = ui.Label("status_lbl", "")
            .TextColor(pal::GREY).Grow(0).PaddingV(0);
        hdr.Child(id_statusLabel);

        return hdr;
    }

    SDL::ECS::EntityId _BuildBody() {
        auto body = ui.Row("body", 12.f, 0.f)
            .Grow(1)
            .PaddingH(16.f).PaddingV(12.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f));

        // Left sidebar: city suggestions
        auto sidebar = ui.Column("sidebar", 6.f, 0.f)
            .W(220.f).Grow(0)
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(8.f))
            .PaddingH(8.f).PaddingV(8.f);

        sidebar.Child(ui.Label("sidebar_title", "City results")
            .TextColor(pal::GREY).PaddingV(2.f));

        id_cityList = ui.ListBoxWidget("city_list", {})
            .Grow(1).W(SDL::UI::Value::Pw(100.f))
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(0.f))
            .OnClick([this]{ _OnCitySelected(); });
        sidebar.Child(id_cityList);

        body.Child(sidebar);

        // Main panel: weather display
        id_mainPanel = _BuildMainPanel();
        body.Child(id_mainPanel);

        return body;
    }

    SDL::ECS::EntityId _BuildMainPanel() {
        auto panel = ui.Column("main_panel", 16.f, 0.f)
            .Grow(1)
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(8.f))
            .PaddingH(20.f).PaddingV(16.f);

        // ── Current conditions card ───────────────────────────────────────────
        auto curCard = ui.Row("current_card", 20.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(160.f)
            .BgColor(pal::CARD)
            .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
            .Radius(SDL::FCorners(10.f))
            .PaddingH(24.f).PaddingV(16.f);

        // Big weather icon
        id_cwIcon = ui.ImageWidget("cw_icon", "weather_sun", SDL::ImageFit::Contain)
            .W(96.f).H(96.f);
        curCard.Child(id_cwIcon);

        // City + temperature column
        auto cwLeft = ui.Column("cw_left", 4.f, 0.f)
            .Grow(1).BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));

        id_cwCity = ui.Label("cw_city", "—")
            .TextColor(pal::WHITE).FontKey("font", 20.f);
        cwLeft.Child(id_cwCity);

        id_cwTemp = ui.Label("cw_temp", "—")
            .TextColor(pal::ACCENT).FontKey("font", 42.f);
        cwLeft.Child(id_cwTemp);

        id_cwDesc = ui.Label("cw_desc", "")
            .TextColor(pal::GREY).FontKey("font", 15.f);
        cwLeft.Child(id_cwDesc);

        curCard.Child(cwLeft);

        // Details column (feels, humidity, wind)
        auto cwRight = ui.Column("cw_right", 6.f, 0.f)
            .W(180.f).BgColor({0,0,0,0}).Borders(SDL::FBox(0.f))
            .AlignChildrenV(SDL::UI::Align::Center);

        id_cwFeels = ui.Label("cw_feels", "")
            .TextColor(pal::GREY);
        cwRight.Child(id_cwFeels);

        id_cwHumidity = ui.Label("cw_humidity", "")
            .TextColor(pal::GREY);
        cwRight.Child(id_cwHumidity);

        id_cwWind = ui.Label("cw_wind", "")
            .TextColor(pal::GREY);
        cwRight.Child(id_cwWind);

        curCard.Child(cwRight);
        panel.Child(curCard);

        // ── 7-day forecast row ────────────────────────────────────────────────
        panel.Child(ui.Label("forecast_title", "7-Day Forecast")
            .TextColor(pal::GREY).FontKey("font", 13.f));

        auto forecastRow = ui.Row("forecast_row", 10.f, 0.f)
            .W(SDL::UI::Value::Pw(100.f)).H(150.f)
            .BgColor({0,0,0,0}).Borders(SDL::FBox(0.f));

        for (int i = 0; i < kDays; ++i) {
            std::string idx = std::to_string(i);
            auto card = ui.Column("day_card_" + idx, 4.f, 0.f)
                .Grow(1).H(SDL::UI::Value::Pw(100.f))
                .BgColor(pal::CARD)
                .Borders(SDL::FBox(1.f)).BorderColor(pal::BORDER)
                .Radius(SDL::FCorners(8.f))
                .PaddingH(6.f).PaddingV(8.f)
                .AlignChildrenH(SDL::UI::Align::Center);

            id_dayLabel[i] = ui.Label("day_label_" + idx, "—")
                .TextColor(pal::GREY).FontKey("font", 12.f);
            card.Child(id_dayLabel[i]);

            id_dayIcon[i] = ui.ImageWidget("day_icon_" + idx, "weather_sun",
                                           SDL::ImageFit::Contain)
                .W(40.f).H(40.f);
            card.Child(id_dayIcon[i]);

            id_dayMax[i] = ui.Label("day_max_" + idx, "—")
                .TextColor(pal::WARM).FontKey("font", 13.f);
            card.Child(id_dayMax[i]);

            id_dayMin[i] = ui.Label("day_min_" + idx, "—")
                .TextColor(pal::COLD).FontKey("font", 12.f);
            card.Child(id_dayMin[i]);

            id_dayPanel[i] = card;
            forecastRow.Child(card);
        }

        panel.Child(forecastRow);
        return panel;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Network helpers
    // ─────────────────────────────────────────────────────────────────────────

    void _DoSearch() {
        if (m_netBusy) return;
        std::string query = ui.GetText(id_searchInput);
        if (query.empty()) return;

        m_netBusy = true;
        ui.SetText(id_statusLabel, "Searching…");
        ui.SetEnabled(id_searchBtn, false);

        if (m_netThread.joinable()) m_netThread.join();
        m_netThread = std::thread([this, query]() {
            auto cities = GeocodeCities(query);
            std::lock_guard lock(m_mtx);
            m_pendingCities     = std::move(cities);
            m_hasPendingCities  = true;
            m_netBusy           = false;
        });
    }

    void _FetchForecast(const CityResult& city) {
        if (m_netBusy) return;
        m_netBusy = true;
        ui.SetText(id_statusLabel, "Loading forecast…");

        if (m_netThread.joinable()) m_netThread.join();
        m_netThread = std::thread([this, city]() {
            auto ws = FetchWeather(city);
            std::lock_guard lock(m_mtx);
            m_pendingWeather     = std::move(ws);
            m_hasPendingWeather  = true;
            m_netBusy            = false;
        });
    }

    void _OnCitySelected() {
        int sel = ui.GetListBoxSelection(id_cityList);
        if (sel < 0 || sel >= static_cast<int>(m_cities.size())) return;
        _FetchForecast(m_cities[sel]);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI update from fetched data
    // ─────────────────────────────────────────────────────────────────────────

    void _ApplyCities(const std::vector<CityResult>& cities) {
        m_cities = cities;
        std::vector<std::string> items;
        items.reserve(cities.size());
        for (auto& c : cities) items.push_back(c.displayName());
        ui.SetListBoxItems(id_cityList, std::move(items));
        ui.SetText(id_statusLabel,
            cities.empty() ? "No results." :
            std::format("{} result(s)", cities.size()));
    }

    void _ApplyWeather(const WeatherState& ws) {
        ui.SetText(id_cwCity, ws.cityName);
        ui.SetText(id_cwTemp, std::format("{:.0f}°C", ws.currentTemp));
        ui.SetText(id_cwDesc, WeatherDesc(ws.weatherCode));
        ui.SetText(id_cwFeels,    std::format("Feels like:  {:.1f}°C", ws.feelsLike));
        ui.SetText(id_cwHumidity, std::format("Humidity:    {:.0f}%",  ws.humidity));
        ui.SetText(id_cwWind,     std::format("Wind:        {:.1f} km/h", ws.windSpeed));
        ui.SetImageKey(id_cwIcon, WeatherIcon(ws.weatherCode));

        for (int i = 0; i < kDays; ++i) {
            if (i < static_cast<int>(ws.forecast.size())) {
                const auto& df = ws.forecast[i];
                ui.SetText(id_dayLabel[i], df.weekday);
                ui.SetText(id_dayMax[i],   std::format("{:.0f}°", df.tempMax));
                ui.SetText(id_dayMin[i],   std::format("{:.0f}°", df.tempMin));
                ui.SetImageKey(id_dayIcon[i], WeatherIcon(df.weatherCode));
            }
        }
        ui.SetText(id_statusLabel, "");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Frame callbacks
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        m_frameTimer.Begin();
        const float dt = m_frameTimer.GetDelta();

        // Poll network results
        {
            std::lock_guard lock(m_mtx);
            if (m_hasPendingCities) {
                _ApplyCities(m_pendingCities);
                m_hasPendingCities = false;
                ui.SetEnabled(id_searchBtn, true);
            }
            if (m_hasPendingWeather) {
                if (m_pendingWeather)
                    _ApplyWeather(*m_pendingWeather);
                else
                    ui.SetText(id_statusLabel, "Failed to load forecast.");
                m_hasPendingWeather = false;
                ui.SetEnabled(id_searchBtn, true);
            }
        }

        pool.Update();
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        m_frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            if (ev.key.key == SDL::KEYCODE_ESCAPE) return SDL::APP_SUCCESS;
            if (ev.key.key == SDL::KEYCODE_RETURN ||
                ev.key.key == SDL::KEYCODE_KP_ENTER)
                _DoSearch();
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
