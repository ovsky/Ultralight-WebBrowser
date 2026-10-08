#include "ProfileManager.h"

#include "UI.h"
#include "BookmarkStore.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>

namespace {
constexpr const char kProfilesFileName[] = "profiles.json";
constexpr const char kDefaultProfileName[] = "Default";

std::string GenerateRandomId() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
  std::string id;
  id.reserve(16);
  for (int i = 0; i < 16; ++i) {
    id += charset[dis(gen)];
  }
  return id;
}

std::string GetRandomAvatarColor() {
  static const char* colors[] = {
    "#6C63FF", "#FF6B6B", "#4ECDC4", "#FFD93D", "#A8E6CF",
    "#FF8B94", "#C7CEEA", "#B8E0D2", "#F8B500", "#7DD3FC"
  };
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 9);
  return colors[dis(gen)];
}

uint64_t GetCurrentTimestamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string EscapeJsonString(const std::string& input) {
  std::ostringstream ss;
  for (char c : input) {
    switch (c) {
      case '"': ss << "\\\""; break;
      case '\\': ss << "\\\\"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          ss << c;
        }
    }
  }
  return ss.str();
}
}

ProfileManager::ProfileManager(UI* ui) : ui_(ui) {
  profiles_dir_ = GetProfilesDirectory();
}

ProfileManager::~ProfileManager() = default;

std::filesystem::path ProfileManager::GetProfilesDirectory() {
  std::error_code ec;
  // Get the settings directory from UI (need to make it accessible)
  std::filesystem::path base_dir = ui_->GetSettingsDirectoryForProfile();
  std::filesystem::path profiles_dir = base_dir / "profiles";
  std::filesystem::create_directories(profiles_dir, ec);
  return profiles_dir;
}

void ProfileManager::LoadProfiles() {
  profiles_.clear();
  current_profile_id_.clear();

  std::filesystem::path profiles_file = profiles_dir_ / kProfilesFileName;
  if (!std::filesystem::exists(profiles_file)) {
    return;
  }

  std::ifstream in(profiles_file);
  if (!in.is_open()) {
    return;
  }

  std::ostringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();
  in.close();

  if (content.empty()) {
    return;
  }

  // Simple JSON parsing for profiles
  size_t profiles_pos = content.find("\"profiles\"");
  if (profiles_pos == std::string::npos) {
    return;
  }

  size_t array_start = content.find('[', profiles_pos);
  if (array_start == std::string::npos) {
    return;
  }

  int depth = 0;
  size_t array_end = std::string::npos;
  for (size_t i = array_start; i < content.size(); ++i) {
    if (content[i] == '[' || content[i] == '{') {
      ++depth;
    } else if (content[i] == ']' || content[i] == '}') {
      --depth;
      if (depth == 0) {
        array_end = i;
        break;
      }
    }
  }

  if (array_end == std::string::npos) {
    return;
  }

  std::string array_content = content.substr(array_start + 1, array_end - array_start - 1);

  // Parse each profile object
  size_t pos = 0;
  while (pos < array_content.size()) {
    size_t obj_start = array_content.find('{', pos);
    if (obj_start == std::string::npos) break;
    int obj_depth = 0;
    size_t obj_end = std::string::npos;
    for (size_t i = obj_start; i < array_content.size(); ++i) {
      if (array_content[i] == '{') ++obj_depth;
      else if (array_content[i] == '}') {
        --obj_depth;
        if (obj_depth == 0) {
          obj_end = i;
          break;
        }
      }
    }
    if (obj_end == std::string::npos) break;

    std::string obj = array_content.substr(obj_start, obj_end - obj_start + 1);

    Profile profile;
    auto extractField = [&obj](const std::string& key, std::string& out) {
      size_t key_pos = obj.find("\"" + key + "\"");
      if (key_pos == std::string::npos) return false;
      size_t colon = obj.find(':', key_pos);
      if (colon == std::string::npos) return false;
      size_t quote1 = obj.find('"', colon);
      if (quote1 == std::string::npos) return false;
      size_t quote2 = obj.find('"', quote1 + 1);
      if (quote2 == std::string::npos) return false;
      out = obj.substr(quote1 + 1, quote2 - quote1 - 1);
      return true;
    };

    auto extractBoolField = [&obj](const std::string& key, bool& out) {
      size_t key_pos = obj.find("\"" + key + "\"");
      if (key_pos == std::string::npos) return false;
      size_t colon = obj.find(':', key_pos);
      if (colon == std::string::npos) return false;
      size_t val_start = colon + 1;
      while (val_start < obj.size() && std::isspace(static_cast<unsigned char>(obj[val_start]))) ++val_start;
      if (obj.compare(val_start, 4, "true") == 0) {
        out = true;
        return true;
      }
      if (obj.compare(val_start, 5, "false") == 0) {
        out = false;
        return true;
      }
      return false;
    };

    auto extractIntField = [&obj](const std::string& key, uint64_t& out) {
      size_t key_pos = obj.find("\"" + key + "\"");
      if (key_pos == std::string::npos) return false;
      size_t colon = obj.find(':', key_pos);
      if (colon == std::string::npos) return false;
      size_t val_start = colon + 1;
      while (val_start < obj.size() && std::isspace(static_cast<unsigned char>(obj[val_start]))) ++val_start;
      size_t val_end = val_start;
      while (val_end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[val_end]))) ++val_end;
      if (val_end > val_start) {
        out = std::stoull(obj.substr(val_start, val_end - val_start));
        return true;
      }
      return false;
    };

    extractField("id", profile.id);
    extractField("name", profile.name);
    extractField("avatar_color", profile.avatar_color);
    extractBoolField("is_default", profile.is_default);
    std::string data_dir_str;
    if (extractField("data_dir", data_dir_str)) {
      profile.data_dir = data_dir_str;
    }
    extractIntField("created_at", profile.created_at);
    extractIntField("last_used_at", profile.last_used_at);

    if (!profile.id.empty()) {
      profiles_.push_back(std::move(profile));
    }

    pos = obj_end + 1;
  }

  // Find current profile
  size_t current_pos = content.find("\"current_profile\"");
  if (current_pos != std::string::npos) {
    size_t colon = content.find(':', current_pos);
    size_t quote1 = content.find('"', colon);
    size_t quote2 = content.find('"', quote1 + 1);
    if (quote1 != std::string::npos && quote2 != std::string::npos) {
      current_profile_id_ = content.substr(quote1 + 1, quote2 - quote1 - 1);
    }
  }

  size_t multi_pos = content.find("\"multi_profile_enabled\"");
  if (multi_pos != std::string::npos) {
    size_t colon = content.find(':', multi_pos);
    if (colon != std::string::npos) {
      size_t val_start = colon + 1;
      while (val_start < content.size() && std::isspace(static_cast<unsigned char>(content[val_start]))) ++val_start;
      if (content.compare(val_start, 4, "true") == 0) {
        multi_profile_enabled_ = true;
      }
    }
  }

  // Ensure there's a default profile
  if (profiles_.empty()) {
    Profile default_profile;
    default_profile.id = "default";
    default_profile.name = kDefaultProfileName;
    default_profile.avatar_color = GetRandomAvatarColor();
    default_profile.is_default = true;
    default_profile.data_dir = GetProfileDataDir("default");
    default_profile.created_at = GetCurrentTimestamp();
    default_profile.last_used_at = GetCurrentTimestamp();
    profiles_.push_back(std::move(default_profile));
    current_profile_id_ = "default";
    SaveProfiles();
  }

  // Ensure current profile exists
  bool found = false;
  for (const auto& p : profiles_) {
    if (p.id == current_profile_id_) {
      found = true;
      break;
    }
  }
  if (!found && !profiles_.empty()) {
    current_profile_id_ = profiles_[0].id;
    SaveProfiles();
  }
}

void ProfileManager::SaveProfiles() const {
  std::filesystem::path profiles_file = profiles_dir_ / kProfilesFileName;
  std::ofstream out(profiles_file);
  if (!out.is_open()) {
    return;
  }

  out << "{\n";
  out << "  \"profiles\": [\n";
  for (size_t i = 0; i < profiles_.size(); ++i) {
    const auto& p = profiles_[i];
    if (i > 0) out << ",\n";
    out << "    {\n";
    out << "      \"id\": \"" << EscapeJsonString(p.id) << "\",\n";
    out << "      \"name\": \"" << EscapeJsonString(p.name) << "\",\n";
    out << "      \"avatar_color\": \"" << EscapeJsonString(p.avatar_color) << "\",\n";
    out << "      \"is_default\": " << (p.is_default ? "true" : "false") << ",\n";
    out << "      \"data_dir\": \"" << EscapeJsonString(p.data_dir.string()) << "\",\n";
    out << "      \"created_at\": " << p.created_at << ",\n";
    out << "      \"last_used_at\": " << p.last_used_at << "\n";
    out << "    }";
  }
  out << "\n  ],\n";
  out << "  \"current_profile\": \"" << EscapeJsonString(current_profile_id_) << "\",\n";
  out << "  \"multi_profile_enabled\": " << (multi_profile_enabled_ ? "true" : "false") << "\n";
  out << "}\n";
  out.flush();
}

bool ProfileManager::Initialize() {
  LoadProfiles();
  return true;
}

const Profile* ProfileManager::GetCurrentProfile() const {
  for (const auto& p : profiles_) {
    if (p.id == current_profile_id_) {
      return &p;
    }
  }
  return nullptr;
}

std::string ProfileManager::GetCurrentProfileId() const {
  return current_profile_id_;
}

Profile* ProfileManager::CreateProfile(const std::string& name, const std::string& avatar_color) {
  Profile profile;
  profile.id = GenerateRandomId();
  profile.name = name.empty() ? "Profile " + std::to_string(profiles_.size() + 1) : name;
  profile.avatar_color = avatar_color.empty() ? GetRandomAvatarColor() : avatar_color;
  profile.is_default = false;
  profile.data_dir = GetProfileDataDir(profile.id);
  profile.created_at = GetCurrentTimestamp();
  profile.last_used_at = GetCurrentTimestamp();

  profiles_.push_back(std::move(profile));
  SaveProfiles();
  return &profiles_.back();
}

bool ProfileManager::SwitchProfile(const std::string& profile_id) {
  for (auto& p : profiles_) {
    if (p.id == profile_id) {
      if (p.id == current_profile_id_) {
        return true; // Already current
      }

      current_profile_id_ = profile_id;
      p.last_used_at = GetCurrentTimestamp();
      SaveProfiles();

      // Notify UI to reload with new profile data
      if (ui_) {
        // This would trigger a browser restart or profile data reload
        // For now, just save the setting
      }
      return true;
    }
  }
  return false;
}

bool ProfileManager::DeleteProfile(const std::string& profile_id) {
  if (profiles_.size() <= 1) {
    return false; // Cannot delete the last profile
  }

  auto it = std::find_if(profiles_.begin(), profiles_.end(),
    [&profile_id](const Profile& p) { return p.id == profile_id; });

  if (it == profiles_.end()) {
    return false;
  }

  if (it->is_default) {
    return false; // Cannot delete default profile
  }

  bool was_current = (it->id == current_profile_id_);
  profiles_.erase(it);

  if (was_current) {
    current_profile_id_ = profiles_[0].id;
  }

  SaveProfiles();
  return true;
}

bool ProfileManager::UpdateProfile(const std::string& profile_id, const std::string& name, const std::string& avatar_color) {
  for (auto& p : profiles_) {
    if (p.id == profile_id) {
      if (!name.empty()) p.name = name;
      if (!avatar_color.empty()) p.avatar_color = avatar_color;
      SaveProfiles();
      return true;
    }
  }
  return false;
}

std::filesystem::path ProfileManager::GetProfileDataDir(const std::string& profile_id) const {
  return profiles_dir_ / profile_id;
}

std::string ProfileManager::GenerateProfileId() const {
  return GenerateRandomId();
}

std::string ProfileManager::GetDefaultAvatarColor() const {
  return GetRandomAvatarColor();
}

std::string ProfileManager::ExportCurrentProfile() const {
  const Profile* current = GetCurrentProfile();
  if (!current) {
    return "{}";
  }

  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"id\": \"" << EscapeJsonString(current->id) << "\",\n";
  ss << "  \"name\": \"" << EscapeJsonString(current->name) << "\",\n";
  ss << "  \"avatar_color\": \"" << EscapeJsonString(current->avatar_color) << "\",\n";
  ss << "  \"is_default\": " << (current->is_default ? "true" : "false") << ",\n";
  ss << "  \"data_dir\": \"" << EscapeJsonString(current->data_dir.string()) << "\",\n";
  ss << "  \"created_at\": " << current->created_at << ",\n";
  ss << "  \"last_used_at\": " << current->last_used_at << "\n";
  ss << "}";
  return ss.str();
}