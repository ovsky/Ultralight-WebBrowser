#pragma once
#include <string>
#include <vector>
#include <filesystem>

struct BrowserProfile
{
  std::string name;
  std::string directory_name;
  bool is_default = false;
};

class ProfileManager
{
public:
  ProfileManager();
  ~ProfileManager();

  // Initialize: load profiles from disk, create "Default" if none exist
  void Initialize(const std::filesystem::path &base_data_dir);

  // Get all profiles
  const std::vector<BrowserProfile> &GetProfiles() const { return profiles_; }

  // Get the current active profile name
  const std::string &GetCurrentProfileName() const { return current_profile_; }

  // Get the base data directory (parent of all profiles)
  const std::filesystem::path &GetBaseDataDirectory() const { return base_data_dir_; }

  // Get the data directory for a specific profile
  std::filesystem::path GetProfileDataDirectory(const std::string &profile_name) const;

  // Get the data directory for the current profile
  std::filesystem::path GetCurrentProfileDataDirectory() const;

  // Create a new profile
  bool CreateProfile(const std::string &name);

  // Switch to a different profile (returns true if successful)
  bool SwitchProfile(const std::string &name);

  // Delete a profile (cannot delete the default profile)
  bool DeleteProfile(const std::string &name);

  // Check if a profile exists
  bool ProfileExists(const std::string &name) const;

private:
  void SaveProfilesToDisk();
  void LoadProfilesFromDisk();
  std::string SanitizeDirectoryName(const std::string &name) const;

  std::filesystem::path base_data_dir_;
  std::filesystem::path profiles_dir_;
  std::vector<BrowserProfile> profiles_;
  std::string current_profile_;
};
