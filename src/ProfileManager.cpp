#include "ProfileManager.h"
#include "Utils.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>

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
    ++pos;
    std::string result;
    while (pos < buffer.size() && buffer[pos] != '"')
    {
      if (buffer[pos] == '\\' && pos + 1 < buffer.size())
      {
        ++pos;
        switch (buffer[pos])
        {
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        default: result += buffer[pos]; break;
        }
        ++pos;
      }
      else
      {
        result += buffer[pos];
        ++pos;
      }
    }
    return result;
  }

  std::string EscapeJsonString(const std::string &s)
  {
    std::string result;
    for (char c : s)
    {
      switch (c)
      {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
      }
    }
    return result;
  }
}

ProfileManager::ProfileManager()
  : current_profile_("Default")
{
}

ProfileManager::~ProfileManager()
{
}

void ProfileManager::Initialize(const std::filesystem::path &base_data_dir)
{
  base_data_dir_ = base_data_dir;
  profiles_dir_ = base_data_dir_ / "profiles";

  std::error_code ec;
  std::filesystem::create_directories(profiles_dir_, ec);

  LoadProfilesFromDisk();

  if (profiles_.empty())
  {
    BrowserProfile default_profile;
    default_profile.name = "Default";
    default_profile.directory_name = "Default";
    default_profile.is_default = true;
    profiles_.push_back(default_profile);
    current_profile_ = "Default";
    SaveProfilesToDisk();
  }
  else if (current_profile_.empty() || !ProfileExists(current_profile_))
  {
    current_profile_ = profiles_[0].name;
  }
}

std::filesystem::path ProfileManager::GetProfileDataDirectory(const std::string &profile_name) const
{
  for (const auto &p : profiles_)
  {
    if (p.name == profile_name)
      return profiles_dir_ / p.directory_name;
  }
  return profiles_dir_ / "Default";
}

std::filesystem::path ProfileManager::GetCurrentProfileDataDirectory() const
{
  return GetProfileDataDirectory(current_profile_);
}

bool ProfileManager::CreateProfile(const std::string &name)
{
  if (name.empty() || ProfileExists(name))
    return false;

  std::string dir_name = SanitizeDirectoryName(name);
  if (dir_name.empty())
    dir_name = "Profile";

  // Ensure unique directory name
  std::string unique_dir = dir_name;
  int counter = 1;
  while (true)
  {
    bool exists = false;
    for (const auto &p : profiles_)
    {
      if (p.directory_name == unique_dir)
      {
        exists = true;
        break;
      }
    }
    if (!exists)
      break;
    unique_dir = dir_name + std::to_string(counter);
    counter++;
  }

  std::error_code ec;
  std::filesystem::create_directories(profiles_dir_ / unique_dir, ec);

  BrowserProfile new_profile;
  new_profile.name = name;
  new_profile.directory_name = unique_dir;
  new_profile.is_default = false;
  profiles_.push_back(new_profile);

  SaveProfilesToDisk();
  return true;
}

bool ProfileManager::SwitchProfile(const std::string &name)
{
  if (!ProfileExists(name))
    return false;
  current_profile_ = name;
  SaveProfilesToDisk();
  return true;
}

bool ProfileManager::DeleteProfile(const std::string &name)
{
  if (name == "Default" || !ProfileExists(name))
    return false;

  auto it = std::find_if(profiles_.begin(), profiles_.end(),
    [&name](const BrowserProfile &p) { return p.name == name; });

  if (it != profiles_.end())
  {
    std::error_code ec;
    std::filesystem::remove_all(profiles_dir_ / it->directory_name, ec);
    profiles_.erase(it);

    if (current_profile_ == name && !profiles_.empty())
      current_profile_ = profiles_[0].name;

    SaveProfilesToDisk();
    return true;
  }
  return false;
}

bool ProfileManager::ProfileExists(const std::string &name) const
{
  for (const auto &p : profiles_)
  {
    if (p.name == name)
      return true;
  }
  return false;
}

std::string ProfileManager::SanitizeDirectoryName(const std::string &name) const
{
  std::string result;
  for (char c : name)
  {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
      result += c;
    else
      result += '_';
  }
  // Trim
  size_t start = result.find_first_not_of(" ");
  if (start == std::string::npos)
    return "";
  size_t end = result.find_last_not_of(" ");
  return result.substr(start, end - start + 1);
}

void ProfileManager::SaveProfilesToDisk()
{
  std::string json = "{\n  \"current_profile\": \"" + EscapeJsonString(current_profile_) + "\",\n  \"profiles\": [\n";
  for (size_t i = 0; i < profiles_.size(); ++i)
  {
    json += "    {\"name\": \"" + EscapeJsonString(profiles_[i].name) + "\", ";
    json += "\"directory\": \"" + EscapeJsonString(profiles_[i].directory_name) + "\", ";
    json += "\"is_default\": ";
    json += profiles_[i].is_default ? "true" : "false";
    json += "}";
    if (i + 1 < profiles_.size())
      json += ",";
    json += "\n";
  }
  json += "  ]\n}\n";

  std::ofstream out(profiles_dir_ / "profiles.json");
  if (out.is_open())
    out << json;
}

void ProfileManager::LoadProfilesFromDisk()
{
  auto path = profiles_dir_ / "profiles.json";
  std::ifstream in(path);
  if (!in.is_open())
    return;

  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  current_profile_ = ParseStringLenient(content, "current_profile", "Default");

  // Simple profile array parsing
  size_t arr_start = content.find("\"profiles\"");
  if (arr_start == std::string::npos)
    return;
  arr_start = content.find('[', arr_start);
  if (arr_start == std::string::npos)
    return;

  size_t pos = arr_start + 1;
  while (pos < content.size())
  {
    // Find next object
    pos = content.find('{', pos);
    if (pos == std::string::npos || pos > content.find(']', arr_start))
      break;

    size_t obj_end = content.find('}', pos);
    if (obj_end == std::string::npos)
      break;

    std::string obj = content.substr(pos, obj_end - pos + 1);
    BrowserProfile p;
    p.name = ParseStringLenient(obj, "name", "");
    p.directory_name = ParseStringLenient(obj, "directory", "");
    p.is_default = ParseBoolLenient(obj, "is_default", false);

    if (!p.name.empty() && !p.directory_name.empty())
      profiles_.push_back(p);

    pos = obj_end + 1;
  }
}
