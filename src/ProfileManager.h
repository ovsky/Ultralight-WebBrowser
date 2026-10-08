#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <map>
#include <ultralight/RefPtr.h>

namespace ultralight {
class Window;
}

class UI;
class BookmarkStore;
class SettingsManager;

struct Profile {
  std::string id;
  std::string name;
  std::string avatar_color; // Hex color for avatar
  bool is_default = false;
  std::filesystem::path data_dir;
  uint64_t created_at = 0;
  uint64_t last_used_at = 0;
};

class ProfileManager {
public:
  explicit ProfileManager(UI* ui);
  ~ProfileManager();

  // Initialize profiles from disk
  bool Initialize();

  // Get all profiles
  const std::vector<Profile>& GetProfiles() const { return profiles_; }

  // Get current profile
  const Profile* GetCurrentProfile() const;

  // Get current profile ID
  std::string GetCurrentProfileId() const;

  // Create a new profile
  Profile* CreateProfile(const std::string& name, const std::string& avatar_color = "");

  // Switch to a profile
  bool SwitchProfile(const std::string& profile_id);

  // Delete a profile
  bool DeleteProfile(const std::string& profile_id);

  // Update profile info
  bool UpdateProfile(const std::string& profile_id, const std::string& name, const std::string& avatar_color);

  // Get profile data directory
  std::filesystem::path GetProfileDataDir(const std::string& profile_id) const;

  // Get profiles directory
  std::filesystem::path GetProfilesDirectory();

  // Check if multi-profile is enabled
  bool IsMultiProfileEnabled() const { return multi_profile_enabled_; }
  void SetMultiProfileEnabled(bool enabled) { multi_profile_enabled_ = enabled; }

  // Export current profile to JSON
  std::string ExportCurrentProfile() const;

  // Save profiles to disk
  void SaveProfiles() const;

private:
  UI* ui_;
  std::vector<Profile> profiles_;
  std::string current_profile_id_;
  bool multi_profile_enabled_ = false;
  std::filesystem::path profiles_dir_;

  void LoadProfiles();
  void SaveProfilesImpl() const;
  std::string GenerateProfileId() const;
  std::string GetDefaultAvatarColor() const;
};