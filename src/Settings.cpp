#include "Settings.h"
#include "UI.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <cctype>

namespace
{
  bool ParseBoolLenient(const std::string &buffer, const std::string &key, bool fallback)
  {
    if (key.empty())
      return fallback;
    std::string needle = std::string("\"") + key + "\"";
    auto pos = buffer.find(needle);
    if (pos == std::string::npos)
      return fallback;
    pos = buffer.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return fallback;
    ++pos;
    while (pos < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[pos])))
      ++pos;
    if (pos >= buffer.size())
      return fallback;
    if (buffer.compare(pos, 4, "true") == 0)
      return true;
    if (buffer.compare(pos, 5, "false") == 0)
      return false;
    if (buffer[pos] == '1')
      return true;
    if (buffer[pos] == '0')
      return false;
    return fallback;
  }

  std::string ParseStringLenient(const std::string &buffer, const std::string &key, const std::string &fallback)
  {
    if (key.empty())
      return fallback;
    std::string needle = std::string("\"") + key + "\"";
    auto pos = buffer.find(needle);
    if (pos == std::string::npos)
      return fallback;
    pos = buffer.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return fallback;
    ++pos;
    while (pos < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[pos])))
      ++pos;
    if (pos >= buffer.size() || buffer[pos] != '"')
      return fallback;
    ++pos; // skip opening quote
    std::string result;
    while (pos < buffer.size() && buffer[pos] != '"')
    {
      if (buffer[pos] == '\\' && pos + 1 < buffer.size())
      {
        ++pos;
        switch (buffer[pos])
        {
        case 'n':
          result += '\n';
          break;
        case 'r':
          result += '\r';
          break;
        case 't':
          result += '\t';
          break;
        case '"':
          result += '"';
          break;
        case '\\':
          result += '\\';
          break;
        default:
          result += buffer[pos];
          break;
        }
      }
      else
      {
        result += buffer[pos];
      }
      ++pos;
    }
    return result;
  }

  double ParseDoubleLenient(const std::string &buffer, const std::string &key, double fallback)
  {
    if (key.empty())
      return fallback;
    std::string needle = std::string("\"") + key + "\"";
    auto pos = buffer.find(needle);
    if (pos == std::string::npos)
      return fallback;
    pos = buffer.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return fallback;
    ++pos;
    while (pos < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[pos])))
      ++pos;
    if (pos >= buffer.size())
      return fallback;
    // Parse number (may be negative, may have decimal point)
    std::string numStr;
    while (pos < buffer.size() && (buffer[pos] == '-' || buffer[pos] == '+' || buffer[pos] == '.' ||
                                   std::isdigit(static_cast<unsigned char>(buffer[pos]))))
    {
      numStr += buffer[pos];
      ++pos;
    }
    if (numStr.empty())
      return fallback;
    try
    {
      return std::stod(numStr);
    }
    catch (...)
    {
      return fallback;
    }
  }
}

void SettingsManager::EnsureDataDirectoryExists()
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(UI::LegacySettingsFilePath().parent_path(), ec);
  fs::create_directories(UI::SettingsDirectory(), ec);
}

void SettingsManager::RestoreSettingsToDefaults(UI &ui)
{
  ui.settings_ = UI::BrowserSettings();
  const auto &catalog = GetSettingsCatalog();
  for (const auto &desc : catalog)
  {
    if (!desc.member)
      continue;
    ui.settings_.*(desc.member) = desc.default_value;
  }
  ui.drm_settings_.SetEnabled(ui.settings_.enable_drm_webview);
  ui.drm_settings_.Save();
}

bool SettingsManager::LoadSettingsFromDisk(UI &ui)
{
  RestoreSettingsToDefaults(ui);
  auto read_file_to_string = [](const std::filesystem::path &p) -> std::string
  {
    if (p.empty())
      return std::string();
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in.is_open())
      return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };

  const auto primary_path = UI::SettingsFilePath();
  const auto legacy_path = UI::LegacySettingsFilePath();
  std::string content = read_file_to_string(primary_path);
  bool loaded = false;
  bool migrated = false;
  if (!content.empty())
    loaded = true;
  else
  {
    content = read_file_to_string(legacy_path);
    if (!content.empty())
    {
      migrated = true;
      loaded = true;
    }
  }

  if (loaded)
  {
    const auto &catalog = GetSettingsCatalog();
    for (const auto &desc : catalog)
    {
      if (!desc.member)
        continue;
      bool fallback = ui.settings_.*(desc.member);
      ui.settings_.*(desc.member) = ParseBoolLenient(content, desc.key, fallback);
    }
    // Parse string settings
    ui.settings_.custom_user_agent = ParseStringLenient(content, "custom_user_agent", "");
    ui.settings_.dark_theme_excluded_sites = ParseStringLenient(content, "dark_theme_excluded_sites", "");
    // Parse location spoofing coordinates
    ui.settings_.spoofed_latitude = ParseDoubleLenient(content, "spoofed_latitude", 0.0);
    ui.settings_.spoofed_longitude = ParseDoubleLenient(content, "spoofed_longitude", 0.0);
    ui.settings_storage_path_ = (migrated ? legacy_path.string() : primary_path.string());
  }
  else
  {
    ui.settings_storage_path_ = primary_path.string();
  }

  ui.saved_settings_ = ui.settings_;
  ui.settings_dirty_ = false;

  // Load DRM settings file, then sync its enabled state FROM the browser settings
  // (browser settings.json is the source of truth for enable_drm_webview)
  ui.drm_settings_.Load();
  ui.drm_settings_.SetEnabled(ui.settings_.enable_drm_webview);

  if (migrated)
  {
    SaveSettingsToDisk(ui);
  }
  return loaded;
}

bool SettingsManager::SaveSettingsToDisk(UI &ui)
{
  EnsureDataDirectoryExists();
  const auto &catalog = GetSettingsCatalog();

  ui.drm_settings_.SetEnabled(ui.settings_.enable_drm_webview);
  ui.drm_settings_.Save();

  std::ostringstream doc;
  doc << "{\n";
  doc << "  \"values\": " << ui.BuildSettingsJSON() << ",\n";
  doc << "  \"custom_user_agent\": \"" << util::EscapeJsonString(ui.settings_.custom_user_agent) << "\",\n";
  doc << "  \"dark_theme_excluded_sites\": \"" << util::EscapeJsonString(ui.settings_.dark_theme_excluded_sites) << "\",\n";
  doc << "  \"spoofed_latitude\": " << ui.settings_.spoofed_latitude << ",\n";
  doc << "  \"spoofed_longitude\": " << ui.settings_.spoofed_longitude << ",\n";
  doc << "  \"meta\": {\n";
  doc << "    \"updated_at\": \"" << util::ToIso8601UTC(std::chrono::system_clock::now()) << "\",\n";
  doc << "    \"dirty\": false,\n";
  doc << "    \"storage_path\": \"" << util::EscapeJsonString(UI::SettingsFilePath().string()) << "\",\n";
  doc << "    \"settings\": [\n";
  bool first = true;
  for (const auto &desc : catalog)
  {
    if (!desc.member)
      continue;
    if (!first)
      doc << ",\n";
    doc << "      {\"key\":\"" << util::EscapeJsonString(desc.key) << "\",";
    doc << "\"value\": " << (ui.settings_.*(desc.member) ? "true" : "false") << "}";
    first = false;
  }
  doc << "\n    ]\n  }\n}\n";

  std::filesystem::path primary_path = UI::SettingsFilePath();
  std::ofstream out(primary_path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return false;
  out << doc.str();
  out.flush();
  if (!out.good())
    return false;
  out.close();

  ui.saved_settings_ = ui.settings_;
  ui.settings_dirty_ = false;
  ui.settings_storage_path_ = primary_path.string();
  return true;
}
// End of SettingsManager implementation